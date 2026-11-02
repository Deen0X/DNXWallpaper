package io.dnx.wallpaper.companion

import android.content.Context
import android.graphics.SurfaceTexture
import android.net.Uri
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLSurface
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.opengl.Matrix
import android.os.Handler
import android.os.Looper
import android.service.wallpaper.WallpaperService
import android.view.Surface
import android.view.SurfaceHolder
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.common.VideoSize
import androidx.media3.exoplayer.ExoPlayer
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer

/**
 * Fondo animado: ExoPlayer decodifica a un SurfaceTexture y un hilo GL lo
 * pinta en la superficie del wallpaper con el encuadre elegido.
 *
 * Modos (preferencia "fit" en "dnxwallpaper"):
 *  - cover  : llena la pantalla recortando (sin deformar)  [por defecto]
 *  - fit    : se ve completo, con bandas negras
 *  - stretch: estira (deforma); solo si el usuario lo elige
 *
 * El GL también deja preparado el camino para parallax (onOffsetsChanged).
 */
class VideoWallpaperService : WallpaperService() {

    override fun onCreateEngine(): Engine = VideoEngine()

    inner class VideoEngine : Engine() {
        private val mainHandler = Handler(Looper.getMainLooper())
        private var player: ExoPlayer? = null
        private var videoSurface: Surface? = null
        private var lastTexture: SurfaceTexture? = null
        private var renderThread: RenderThread? = null
        private var visible = true
        private var fit = "cover"

        // Si la app cambia el wallpaper mientras este motor sigue vivo (lo
        // normal: el fondo ya estaba puesto), hay que recargar el vídeo. Antes
        // solo se leía `current_path` al crear la superficie, así que el
        // cambio no se veía hasta reiniciar.
        private val prefListener =
            android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
                if (key == "current_path" || key == "fit") reloadVideo()
            }

        private fun prefs() = getSharedPreferences("dnxwallpaper", Context.MODE_PRIVATE)

        override fun onCreate(surfaceHolder: SurfaceHolder) {
            super.onCreate(surfaceHolder)
            fit = prefs().getString("fit", "cover") ?: "cover"
            setOffsetNotificationsEnabled(true)
            prefs().registerOnSharedPreferenceChangeListener(prefListener)
        }

        private fun reloadVideo() {
            val t = lastTexture ?: return
            mainHandler.post {
                releasePlayer()
                val s = Surface(t)
                videoSurface = s
                startPlayer(t, s)
                renderThread?.setFit(fitMode())
            }
        }

        override fun onSurfaceCreated(holder: SurfaceHolder) {
            releasePlayer()
            renderThread?.release()
            renderThread = RenderThread(holder, this).also { it.start() }
            renderThread?.setFit(fitMode())
        }

        override fun onSurfaceChanged(
            holder: SurfaceHolder,
            format: Int,
            width: Int,
            height: Int
        ) {
            renderThread?.setViewport(width, height)
        }

        override fun onSurfaceDestroyed(holder: SurfaceHolder) {
            releasePlayer()
            renderThread?.release()
            renderThread = null
        }

        override fun onVisibilityChanged(visible: Boolean) {
            this.visible = visible
            // El ajuste puede haber cambiado en la app mientras el fondo
            // estaba oculto; se relee al volver a mostrarse.
            renderThread?.setFit(fitMode())
            player?.playWhenReady = visible
            renderThread?.setVisible(visible)
        }

        override fun onOffsetsChanged(
            xOffset: Float,
            yOffset: Float,
            xOffsetStep: Float,
            yOffsetStep: Float,
            xPixelOffset: Int,
            yPixelOffset: Int
        ) {
            // Fase posterior: parallax con master cuadrado.
        }

        override fun onDestroy() {
            prefs().unregisterOnSharedPreferenceChangeListener(prefListener)
            releasePlayer()
            renderThread?.release()
            renderThread = null
            super.onDestroy()
        }

        /** Ajuste actual (cover/fit/stretch) para el hilo GL. */
        fun fitMode(): String =
            getSharedPreferences("dnxwallpaper", Context.MODE_PRIVATE)
                .getString("fit", "cover") ?: "cover"

        /** Llamado por el hilo GL cuando el SurfaceTexture ya existe. */
        fun onSurfaceTextureReady(texture: SurfaceTexture, surface: Surface) {
            lastTexture = texture
            mainHandler.post {
                videoSurface?.release()
                videoSurface = surface
                startPlayer(texture, surface)
            }
        }

        private fun startPlayer(texture: SurfaceTexture, surface: Surface) {
            val f = videoFile()
            if (!f.exists() || f.length() == 0L) return
            val p = player
            if (p != null) {
                p.setVideoSurface(surface)
                return
            }
            try {
                val np = ExoPlayer.Builder(this@VideoWallpaperService).build()
                np.setMediaItem(MediaItem.fromUri(Uri.fromFile(f)))
                np.repeatMode = Player.REPEAT_MODE_ONE
                np.volume = 0f
                np.setVideoSurface(surface)
                np.addListener(object : Player.Listener {
                    override fun onVideoSizeChanged(videoSize: VideoSize) {
                        renderThread?.setVideoSize(videoSize.width, videoSize.height)
                    }
                })
                np.prepare()
                np.playWhenReady = visible
                player = np
            } catch (_: Exception) {
                player = null
            }
        }

        /** Vídeo activo: ruta guardada por la app (o el antiguo current.mp4). */
        private fun videoFile(): File {
            val pref = getSharedPreferences("dnxwallpaper", MODE_PRIVATE)
                .getString("current_path", null)
            val f = if (!pref.isNullOrBlank()) File(pref) else null
            if (f != null && f.isFile && f.length() > 0) return f
            return File(filesDir, "current.mp4")
        }

        private fun releasePlayer() {
            player?.let {
                try {
                    it.stop()
                    it.release()
                } catch (_: Exception) {
                }
            }
            player = null
            videoSurface?.release()
            videoSurface = null
        }
    }

    /**
     * Hilo GL: EGL sobre el SurfaceHolder, SurfaceTexture OES y quad con
     * transformación de encuadre.
     */
    private class RenderThread(
        private val holder: SurfaceHolder,
        private val engine: VideoEngine
    ) : Thread("dnx-video-gl") {

        private val lock = Object()
        private var running = true
        private var frameAvailable = false
        private var visible = true

        @Volatile private var videoW = 0
        @Volatile private var videoH = 0
        @Volatile private var surfaceW = 0
        @Volatile private var surfaceH = 0
        @Volatile private var fit = "cover"

        private var eglDisplay: EGLDisplay? = null
        private var eglContext: EGLContext? = null
        private var eglSurface: EGLSurface? = null
        private var textureId = 0
        private var surfaceTexture: SurfaceTexture? = null
        private var program = 0
        private var aPos = 0
        private var aTex = 0
        private var uSTMatrix = 0
        private val stMatrix = FloatArray(16)
        private val pos: FloatBuffer =
            ByteBuffer.allocateDirect(8 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer()
        private val tex: FloatBuffer =
            ByteBuffer.allocateDirect(8 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer()

        init {
            fit = engine.fitMode()
            tex.put(floatArrayOf(0f, 0f, 1f, 0f, 0f, 1f, 1f, 1f))
            tex.position(0)
        }

        fun setViewport(w: Int, h: Int) {
            surfaceW = w
            surfaceH = h
        }

        fun setVideoSize(w: Int, h: Int) {
            videoW = w
            videoH = h
        }

        fun setFit(mode: String) {
            fit = mode
        }

        fun setVisible(v: Boolean) {
            visible = v
            synchronized(lock) { lock.notifyAll() }
        }

        fun release() {
            running = false
            synchronized(lock) { lock.notifyAll() }
            try {
                join(1200)
            } catch (_: InterruptedException) {
            }
        }

        override fun run() {
            try {
                if (!initEgl()) return
                if (!initGl()) return
                val st = SurfaceTexture(textureId)
                st.setOnFrameAvailableListener {
                    synchronized(lock) {
                        frameAvailable = true
                        lock.notifyAll()
                    }
                }
                surfaceTexture = st
                engine.onSurfaceTextureReady(st, Surface(st))
                Matrix.setIdentityM(stMatrix, 0)

                while (running) {
                    synchronized(lock) {
                        while (running && (!frameAvailable || !visible)) {
                            lock.wait(250)
                        }
                        frameAvailable = false
                    }
                    if (!running) break
                    try {
                        st.updateTexImage()
                        st.getTransformMatrix(stMatrix)
                        draw()
                        EGL14.eglSwapBuffers(eglDisplay, eglSurface)
                    } catch (_: Exception) {
                        // Surface destruido o codec reiniciado: se reintenta.
                    }
                }
            } finally {
                releaseGl()
            }
        }

        private fun initEgl(): Boolean {
            val display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
            if (display == EGL14.EGL_NO_DISPLAY) return false
            val version = IntArray(2)
            if (!EGL14.eglInitialize(display, version, 0, version, 1)) return false
            eglDisplay = display
            val attribs = intArrayOf(
                EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
                EGL14.EGL_SURFACE_TYPE, EGL14.EGL_WINDOW_BIT,
                EGL14.EGL_RED_SIZE, 8,
                EGL14.EGL_GREEN_SIZE, 8,
                EGL14.EGL_BLUE_SIZE, 8,
                EGL14.EGL_ALPHA_SIZE, 8,
                EGL14.EGL_NONE
            )
            val configs = arrayOfNulls<EGLConfig>(1)
            val num = IntArray(1)
            if (!EGL14.eglChooseConfig(
                    display, attribs, 0, configs, 0, 1, num, 0
                ) || num[0] == 0
            ) return false
            val ctxAttribs = intArrayOf(
                EGL14.EGL_CONTEXT_CLIENT_VERSION, 2,
                EGL14.EGL_NONE
            )
            val config = configs[0] ?: return false
            val ctx = EGL14.eglCreateContext(
                display, config, EGL14.EGL_NO_CONTEXT, ctxAttribs, 0
            )
            if (ctx == null || ctx == EGL14.EGL_NO_CONTEXT) return false
            val surf = EGL14.eglCreateWindowSurface(
                display, config, holder.surface, intArrayOf(EGL14.EGL_NONE), 0
            )
            if (surf == null || surf == EGL14.EGL_NO_SURFACE) return false
            if (!EGL14.eglMakeCurrent(display, surf, surf, ctx)) return false
            eglContext = ctx
            eglSurface = surf
            return true
        }

        private fun initGl(): Boolean {
            val vs = """
                attribute vec4 aPos;
                attribute vec4 aTex;
                uniform mat4 uSTMatrix;
                varying vec2 vTex;
                void main() {
                    gl_Position = aPos;
                    vTex = (uSTMatrix * aTex).xy;
                }
            """.trimIndent()
            val fs = """
                #extension GL_OES_EGL_image_external : require
                precision mediump float;
                varying vec2 vTex;
                uniform samplerExternalOES uTex;
                void main() {
                    gl_FragColor = texture2D(uTex, vTex);
                }
            """.trimIndent()
            val v = compile(GLES20.GL_VERTEX_SHADER, vs) ?: return false
            val f = compile(GLES20.GL_FRAGMENT_SHADER, fs) ?: return false
            program = GLES20.glCreateProgram()
            GLES20.glAttachShader(program, v)
            GLES20.glAttachShader(program, f)
            GLES20.glLinkProgram(program)
            val status = IntArray(1)
            GLES20.glGetProgramiv(program, GLES20.GL_LINK_STATUS, status, 0)
            GLES20.glDeleteShader(v)
            GLES20.glDeleteShader(f)
            if (status[0] != GLES20.GL_TRUE) return false
            aPos = GLES20.glGetAttribLocation(program, "aPos")
            aTex = GLES20.glGetAttribLocation(program, "aTex")
            uSTMatrix = GLES20.glGetUniformLocation(program, "uSTMatrix")

            val texIds = IntArray(1)
            GLES20.glGenTextures(1, texIds, 0)
            textureId = texIds[0]
            GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureId)
            GLES20.glTexParameteri(
                GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MIN_FILTER,
                GLES20.GL_LINEAR
            )
            GLES20.glTexParameteri(
                GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MAG_FILTER,
                GLES20.GL_LINEAR
            )
            GLES20.glTexParameteri(
                GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_S,
                GLES20.GL_CLAMP_TO_EDGE
            )
            GLES20.glTexParameteri(
                GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_T,
                GLES20.GL_CLAMP_TO_EDGE
            )
            return true
        }

        private fun compile(type: Int, src: String): Int? {
            val sh = GLES20.glCreateShader(type)
            GLES20.glShaderSource(sh, src)
            GLES20.glCompileShader(sh)
            val ok = IntArray(1)
            GLES20.glGetShaderiv(sh, GLES20.GL_COMPILE_STATUS, ok, 0)
            if (ok[0] != GLES20.GL_TRUE) {
                GLES20.glDeleteShader(sh)
                return null
            }
            return sh
        }

        private fun draw() {
            val w = surfaceW
            val h = surfaceH
            if (w <= 0 || h <= 0) return
            GLES20.glViewport(0, 0, w, h)
            GLES20.glClearColor(0f, 0f, 0f, 1f)
            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)

            val vw = if (videoW > 0) videoW else 16
            val vh = if (videoH > 0) videoH else 9
            var sx = 1f
            var sy = 1f
            val videoAspect = vw.toFloat() / vh.toFloat()
            val screenAspect = w.toFloat() / h.toFloat()
            val mode = fit
            if (mode == "stretch") {
                // El quad llena exactamente la pantalla (deforma).
                sx = 1f
                sy = 1f
            } else {
                val scale = if (mode == "fit") {
                    if (videoAspect > screenAspect) w.toFloat() / vw
                    else h.toFloat() / vh
                } else {
                    // cover
                    if (videoAspect > screenAspect) h.toFloat() / vh
                    else w.toFloat() / vw
                }
                sx = (vw * scale) / w
                sy = (vh * scale) / h
            }
            pos.clear()
            pos.put(floatArrayOf(-sx, -sy, sx, -sy, -sx, sy, sx, sy))
            pos.position(0)

            GLES20.glUseProgram(program)
            GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
            GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureId)
            GLES20.glUniform1i(GLES20.glGetUniformLocation(program, "uTex"), 0)
            GLES20.glUniformMatrix4fv(uSTMatrix, 1, false, stMatrix, 0)

            GLES20.glEnableVertexAttribArray(aPos)
            GLES20.glVertexAttribPointer(aPos, 2, GLES20.GL_FLOAT, false, 0, pos)
            GLES20.glEnableVertexAttribArray(aTex)
            GLES20.glVertexAttribPointer(aTex, 2, GLES20.GL_FLOAT, false, 0, tex)
            GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
            GLES20.glDisableVertexAttribArray(aPos)
            GLES20.glDisableVertexAttribArray(aTex)
        }

        private fun releaseGl() {
            try {
                surfaceTexture?.setOnFrameAvailableListener(null)
                surfaceTexture?.release()
            } catch (_: Exception) {
            }
            surfaceTexture = null
            if (textureId != 0) {
                GLES20.glDeleteTextures(1, intArrayOf(textureId), 0)
                textureId = 0
            }
            if (program != 0) {
                GLES20.glDeleteProgram(program)
                program = 0
            }
            val d = eglDisplay ?: return
            EGL14.eglMakeCurrent(
                d, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE,
                EGL14.EGL_NO_CONTEXT
            )
            eglSurface?.let { EGL14.eglDestroySurface(d, it) }
            eglContext?.let { EGL14.eglDestroyContext(d, it) }
            EGL14.eglTerminate(d)
            eglSurface = null
            eglContext = null
            eglDisplay = null
        }
    }
}
