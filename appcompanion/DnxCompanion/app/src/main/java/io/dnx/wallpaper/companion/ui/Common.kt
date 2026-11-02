package io.dnx.wallpaper.companion.ui

import android.graphics.BitmapFactory
import android.graphics.ImageDecoder
import android.graphics.drawable.AnimatedImageDrawable
import android.graphics.drawable.Drawable
import android.net.Uri
import android.os.Build
import android.util.Log
import android.view.TextureView
import android.widget.ImageView
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.exoplayer.ExoPlayer
import io.dnx.wallpaper.companion.decodeBitmap
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.nio.ByteBuffer

/** Caché de bytes de miniaturas animadas (la descarga es lo caro). */
private val animBytes = object : LinkedHashMap<String, ByteArray>(4, 0.75f, true) {
    override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, ByteArray>?) = size > 4
}

private fun loadThumbBytes(url: String): ByteArray? = try {
    synchronized(animBytes) { animBytes[url] } ?: run {
        val b = if (url.startsWith("http")) {
            java.net.URL(url).openStream().use { it.readBytes() }
        } else {
            java.io.File(url).readBytes()
        }
        synchronized(animBytes) { animBytes[url] = b }
        b
    }
} catch (e: Exception) {
    Log.w("DNXWallpaper", "thumb bytes $url", e)
    null
}

/**
 * Miniatura que se anima si el fichero es un WebP animado (o GIF).
 * `Thumb` sólo pinta el primer fotograma; aquí se usa ImageDecoder, que en
 * API 28+ devuelve un AnimatedImageDrawable.
 */
@Composable
fun AnimatedThumb(
    url: String,
    modifier: Modifier = Modifier,
    contentScale: ContentScale = ContentScale.Fit
) {
    if (Build.VERSION.SDK_INT < 28 || url.isBlank()) {
        Thumb(url, modifier, contentScale)
        return
    }
    var drawable by remember(url) { mutableStateOf<Drawable?>(null) }
    var failed by remember(url) { mutableStateOf(false) }
    LaunchedEffect(url) {
        drawable = withContext(Dispatchers.IO) {
            try {
                val b = loadThumbBytes(url) ?: return@withContext null
                ImageDecoder.decodeDrawable(ImageDecoder.createSource(ByteBuffer.wrap(b)))
            } catch (e: Exception) {
                Log.w("DNXWallpaper", "AnimatedThumb $url", e)
                null
            }
        }
        failed = drawable == null
    }
    val d = drawable
    if (d == null) {
        // Mientras carga (o si falla) se pinta la versión estática.
        Thumb(url, modifier, contentScale)
        return
    }
    AndroidView(
        factory = { ctx ->
            ImageView(ctx).apply {
                scaleType = when (contentScale) {
                    ContentScale.Fit -> ImageView.ScaleType.FIT_CENTER
                    else -> ImageView.ScaleType.CENTER_CROP
                }
            }
        },
        update = { iv ->
            if (iv.drawable !== d) {
                iv.setImageDrawable(d)
                (d as? AnimatedImageDrawable)?.let {
                    it.repeatCount = AnimatedImageDrawable.REPEAT_INFINITE
                    it.start()
                }
            }
        },
        modifier = modifier
    )
}

/** Miniatura desde una URL del PC. */
@Composable
fun Thumb(
    url: String,
    modifier: Modifier = Modifier,
    contentScale: ContentScale = ContentScale.Crop
) {
    var bmp by remember(url) { mutableStateOf<android.graphics.Bitmap?>(null) }
    LaunchedEffect(url) {
        bmp = withContext(Dispatchers.IO) { decodeBitmap(url) }
    }
    Box(modifier.background(MaterialTheme.colorScheme.surfaceVariant)) {
        bmp?.let {
            Image(
                bitmap = it.asImageBitmap(),
                contentDescription = null,
                contentScale = contentScale,
                modifier = Modifier.fillMaxSize()
            )
        }
    }
}

/**
 * Reproduce un vídeo local en bucle, sin sonido ni controles.
 * Se usa como preview animado en fichas de items locales: sus miniaturas son
 * JPEG estáticos y no hay WebP animado que pintar.
 */
@Composable
fun VideoLoop(path: String, modifier: Modifier = Modifier) {
    val ctx = LocalContext.current
    var ratio by remember(path) { mutableFloatStateOf(16f / 9f) }
    LaunchedEffect(path) {
        ratio = withContext(Dispatchers.IO) {
            try {
                val r = android.media.MediaMetadataRetriever()
                r.setDataSource(path)
                val w = r.extractMetadata(
                    android.media.MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH
                )?.toFloatOrNull() ?: 0f
                val h = r.extractMetadata(
                    android.media.MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT
                )?.toFloatOrNull() ?: 0f
                r.release()
                if (w > 0f && h > 0f) w / h else 16f / 9f
            } catch (e: Exception) {
                16f / 9f
            }
        }
    }
    val player = remember(path) {
        ExoPlayer.Builder(ctx).build().apply {
            setMediaItem(MediaItem.fromUri(Uri.fromFile(File(path))))
            repeatMode = Player.REPEAT_MODE_ONE
            volume = 0f
            playWhenReady = true
            prepare()  // sin prepare() no se decodifica nada
        }
    }
    DisposableEffect(player) {
        onDispose { player.release() }
    }
    // El contenedor toma la proporción real del vídeo (un recorte vertical no
    // debe verse aplastado) y dentro va un TextureView, que sí respeta el
    // recorte redondeado (SurfaceView no). Se limita la altura para que la
    // ficha no se coma la pantalla.
    Box(modifier, contentAlignment = androidx.compose.ui.Alignment.Center) {
        Box(
            Modifier
                .heightIn(max = 300.dp)
                .aspectRatio(ratio)
        ) {
            AndroidView(
                factory = { c -> TextureView(c).also { player.setVideoTextureView(it) } },
                modifier = Modifier.fillMaxSize()
            )
        }
    }
}

/** Miniatura desde un fichero local. */
@Composable
fun LocalThumb(
    path: String?,
    modifier: Modifier = Modifier,
    contentScale: ContentScale = ContentScale.Crop
) {
    var bmp by remember(path) { mutableStateOf<android.graphics.Bitmap?>(null) }
    LaunchedEffect(path) {
        bmp = withContext(Dispatchers.IO) {
            path?.let { runCatching { BitmapFactory.decodeFile(it) }.getOrNull() }
        }
    }
    Box(modifier.background(MaterialTheme.colorScheme.surfaceVariant)) {
        bmp?.let {
            Image(
                bitmap = it.asImageBitmap(),
                contentDescription = null,
                contentScale = contentScale,
                modifier = Modifier.fillMaxSize()
            )
        }
    }
}

@Composable
fun SectionTitle(text: String) {
    Text(
        text,
        fontSize = 12.sp,
        fontWeight = FontWeight.SemiBold,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.padding(top = 10.dp, bottom = 4.dp)
    )
}
