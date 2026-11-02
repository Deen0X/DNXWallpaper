package io.dnx.wallpaper.companion

import android.app.WallpaperManager
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.drawable.Drawable
import android.net.Uri
import android.util.Log
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Copia del fondo que había antes de aplicar uno nuevo.
 *
 * Android no expone los frames de un fondo animado de otra app, y desde
 * Android 13 tampoco deja leer el fondo estático sin permisos especiales. Por
 * eso hay dos copias posibles:
 *   - `prev_video.mp4`: el vídeo que aplicó DNXWallpaper (siempre legible);
 *   - `prev_static.png`: la imagen del sistema (Android <= 12 con permiso) o
 *     una imagen elegida a mano por el usuario.
 *
 * Sólo se restaura la copia más reciente.
 */
object WallpaperKeep {
    private const val STATIC = "prev_static.png"
    private const val VIDEO = "prev_video.mp4"

    fun staticFile(ctx: Context): File = File(ctx.filesDir, STATIC)
    fun videoFile(ctx: Context): File = File(ctx.filesDir, VIDEO)

    private fun usable(f: File): Boolean = f.isFile && f.length() > 0L

    /** Copia más reciente (o null si no hay ninguna). */
    fun newest(ctx: Context): File? {
        val v = videoFile(ctx).takeIf { usable(it) }
        val s = staticFile(ctx).takeIf { usable(it) }
        return when {
            v != null && s != null -> if (v.lastModified() >= s.lastModified()) v else s
            else -> v ?: s
        }
    }

    /** Texto para la UI: tipo y fecha de la copia ("" = nada). */
    fun label(ctx: Context): String {
        val f = newest(ctx) ?: return ""
        val kind = if (f == videoFile(ctx)) "vídeo de DNXWallpaper" else "imagen"
        val date = SimpleDateFormat("dd/MM HH:mm", Locale.getDefault())
            .format(Date(f.lastModified()))
        return "$kind · $date"
    }

    /**
     * Guarda el fondo activo. Devuelve la etiqueta guardada o "" si el sistema
     * no permite leerlo (Android 13+ o falta de permiso).
     */
    fun save(ctx: Context): String {
        return try {
            val wm = WallpaperManager.getInstance(ctx)
            val ours = wm.wallpaperInfo?.serviceInfo?.packageName == ctx.packageName
            if (ours) {
                val cur = File(ctx.filesDir, "current.mp4")
                if (!usable(cur)) return ""
                if (videoFile(ctx).exists()) videoFile(ctx).delete()
                cur.copyTo(videoFile(ctx), overwrite = true)
                staticFile(ctx).delete()
            } else {
                val d: Drawable = wm.drawable ?: return ""
                val w = if (d.intrinsicWidth > 0) d.intrinsicWidth else 1080
                val h = if (d.intrinsicHeight > 0) d.intrinsicHeight else 1920
                val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
                val canvas = Canvas(bmp)
                d.setBounds(0, 0, w, h)
                d.draw(canvas)
                val f = staticFile(ctx)
                f.outputStream().use { out ->
                    bmp.compress(Bitmap.CompressFormat.PNG, 100, out)
                }
                bmp.recycle()
                videoFile(ctx).delete()
            }
            label(ctx)
        } catch (e: Exception) {
            // Sin permiso de lectura no hay forma de copiar el fondo estático.
            Log.w("DNXWallpaper", "fondo actual no guardado", e)
            ""
        }
    }

    /**
     * Guarda una imagen elegida por el usuario como "fondo anterior".
     * Es la alternativa en Android 13+, donde el sistema no deja leer el fondo.
     */
    fun saveImage(ctx: Context, uri: Uri): String {
        return try {
            val tmp = File(ctx.filesDir, "prev.tmp")
            val input = ctx.contentResolver.openInputStream(uri) ?: return ""
            input.use { ins -> tmp.outputStream().use { out -> ins.copyTo(out) } }
            if (!usable(tmp)) {
                tmp.delete()
                return ""
            }
            val f = staticFile(ctx)
            if (f.exists()) f.delete()
            if (!tmp.renameTo(f)) {
                tmp.copyTo(f, overwrite = true)
                tmp.delete()
            }
            videoFile(ctx).delete()
            label(ctx)
        } catch (e: Exception) {
            Log.e("DNXWallpaper", "saveImage", e)
            ""
        }
    }

    /**
     * Restaura la copia más reciente.
     * needPicker = true cuando hay que abrir el selector de fondos animados.
     */
    fun restore(ctx: Context): Triple<Boolean, String, Boolean> {
        return try {
            val f = newest(ctx)
                ?: return Triple(false, "No hay ningún fondo guardado todavía", false)
            if (f == videoFile(ctx)) {
                val cur = File(ctx.filesDir, "current.mp4")
                if (cur.exists()) cur.delete()
                f.copyTo(cur, overwrite = true)
                Triple(true, "Restaurando el vídeo anterior…", true)
            } else {
                WallpaperManager.getInstance(ctx).setStream(f.inputStream())
                Triple(true, "Fondo anterior restaurado", false)
            }
        } catch (e: Exception) {
            Log.e("DNXWallpaper", "restore", e)
            Triple(false, "No se pudo restaurar: ${e.message ?: "error"}", false)
        }
    }
}
