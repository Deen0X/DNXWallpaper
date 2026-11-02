package io.dnx.wallpaper.companion.ui

import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.graphics.drawable.AnimatedImageDrawable
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.Drawable
import android.os.Build
import android.widget.ImageView
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import io.dnx.wallpaper.companion.CompanionState
import io.dnx.wallpaper.companion.CropRect
import io.dnx.wallpaper.companion.LibItem
import java.io.ByteArrayOutputStream
import java.net.URL
import java.nio.ByteBuffer
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

private data class PadChoice(val label: String, val hex: String, val color: Color)

private val PALETTE = listOf(
    "#000000", "#101418", "#1B1F26", "#0B1220",
    "#3A0CA3", "#7209B7", "#F72585", "#FF7B00",
    "#FFD400", "#2EC4B6", "#3A86FF", "#FFFFFF"
)

/**
 * Editor de encuadre. La pantalla del móvil es el viewport.
 *
 * La vista previa usa WebP animado cuando el PC lo ofrece (Android 8.0+), así
 * se ve el movimiento real del wallpaper mientras se elige el encuadre.
 */
@Composable
fun EditorScreen(state: CompanionState, li: LibItem, onClose: () -> Unit) {
    val density = LocalDensity.current
    var drawable by remember { mutableStateOf<Drawable?>(null) }
    var bitmap by remember { mutableStateOf<android.graphics.Bitmap?>(null) }
    var box by remember { mutableStateOf(IntSize.Zero) }
    var zoom by remember { mutableFloatStateOf(1f) }
    var offset by remember { mutableStateOf(Offset.Zero) }
    var fade by remember { mutableFloatStateOf(0.5f) }
    var pad by remember { mutableStateOf(PadChoice("Negro", "#000000", Color.Black)) }
    var custom by remember { mutableStateOf(false) }
    var target by remember { mutableIntStateOf(0) } // 0 = auto

    val screen = state.screenSize()
    val autoTarget = min(screen.first, screen.second).coerceAtMost(1080)

    LaunchedEffect(li.thumbUrl) {
        val bytes = withContext(Dispatchers.IO) {
            runCatching {
                URL(li.thumbUrl).openStream().use { input ->
                    val out = ByteArrayOutputStream()
                    input.copyTo(out)
                    out.toByteArray()
                }
            }.getOrNull()
        } ?: return@LaunchedEffect
        if (Build.VERSION.SDK_INT >= 28) {
            drawable = runCatching {
                android.graphics.ImageDecoder.decodeDrawable(
                    android.graphics.ImageDecoder.createSource(ByteBuffer.wrap(bytes))
                )
            }.getOrNull()
        }
        if (drawable == null) {
            bitmap = runCatching {
                BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
            }.getOrNull()
        }
    }

    val imgW = if (li.width > 0) li.width.toFloat()
    else (bitmap?.width ?: drawable?.intrinsicWidth ?: 0).toFloat()
        .takeIf { it > 0f } ?: 1f
    val imgH = if (li.height > 0) li.height.toFloat()
    else (bitmap?.height ?: drawable?.intrinsicHeight ?: 0).toFloat()
        .takeIf { it > 0f } ?: 1f
    val ready = drawable != null || bitmap != null

    val bw = box.width.toFloat()
    val bh = box.height.toFloat()
    val screenAspect = screen.first.toFloat() / screen.second.toFloat()
    var vpW = 0f
    var vpH = 0f
    if (bw > 0f && bh > 0f) {
        if (bw / bh > screenAspect) {
            vpH = bh
            vpW = bh * screenAspect
        } else {
            vpW = bw
            vpH = bw / screenAspect
        }
    }
    val originX = (bw - vpW) / 2f
    val originY = (bh - vpH) / 2f

    fun coverScale(z: Float): Float =
        if (!ready || vpW <= 0f || vpH <= 0f) 1f
        else max(vpW / imgW, vpH / imgH) * z

    fun minZoom(): Float {
        if (!ready || vpW <= 0f || vpH <= 0f) return 1f
        val cover = max(vpW / imgW, vpH / imgH)
        val fit = min(vpW / imgW, vpH / imgH)
        return (fit / cover).coerceIn(0.2f, 1f)
    }

    fun clampOffset(o: Offset, z: Float): Offset {
        if (!ready || vpW <= 0f || vpH <= 0f) return o
        val s = coverScale(z)
        val dw = imgW * s
        val dh = imgH * s
        val x = if (dw <= vpW) (vpW - dw) / 2f else o.x.coerceIn(vpW - dw, 0f)
        val y = if (dh <= vpH) (vpH - dh) / 2f else o.y.coerceIn(vpH - dh, 0f)
        return Offset(x, y)
    }

    Column(Modifier.fillMaxSize()) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 2.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(Modifier.weight(1f)) {
                Text("Encuadre", fontSize = 15.sp, fontWeight = FontWeight.SemiBold)
                Text(
                    "La zona marcada es tu pantalla.",
                    fontSize = 10.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            TextButton(
                onClick = { zoom = 1f; offset = Offset.Zero },
                enabled = !state.busy
            ) { Text("Ajustar") }
            TextButton(onClick = onClose, enabled = !state.busy) { Text("Cerrar") }
        }

        Box(
            Modifier
                .weight(1f)
                .fillMaxWidth()
                .onSizeChanged { box = it }
                .pointerInput(ready, vpW, vpH) {
                    detectTransformGestures { _, pan, gestureZoom, _ ->
                        val nz = (zoom * gestureZoom).coerceIn(minZoom(), 6f)
                        zoom = nz
                        offset = clampOffset(offset + pan, nz)
                    }
                }
        ) {
            if (!ready) {
                Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Text("Cargando vista previa…", fontSize = 12.sp)
                }
            } else if (vpW > 0f && vpH > 0f) {
                Box(
                    Modifier
                        .offset {
                            IntOffset(originX.roundToInt(), originY.roundToInt())
                        }
                        .size(
                            with(density) { vpW.toDp() },
                            with(density) { vpH.toDp() }
                        )
                        .clipToBounds()
                        .background(pad.color)
                ) {
                    AndroidView(
                        factory = { ctx ->
                            ImageView(ctx).apply {
                                scaleType = ImageView.ScaleType.MATRIX
                            }
                        },
                        update = { iv ->
                            val d = drawable
                            if (d != null) {
                                if (iv.drawable !== d) {
                                    iv.setImageDrawable(d)
                                    if (Build.VERSION.SDK_INT >= 28 &&
                                        d is AnimatedImageDrawable
                                    ) {
                                        d.repeatCount = AnimatedImageDrawable.REPEAT_INFINITE
                                        d.start()
                                    }
                                }
                            } else if (bitmap != null && iv.drawable == null) {
                                iv.setImageDrawable(
                                    BitmapDrawable(iv.resources, bitmap)
                                )
                            }
                            // El drawable (miniatura WebP) no tiene por qué
                            // medir lo mismo que el vídeo: se escala a la
                            // superficie que le corresponde en pantalla.
                            val s = coverScale(zoom)
                            val dw = imgW * s
                            val dh = imgH * s
                            val dr = iv.drawable
                            val iw = (dr?.intrinsicWidth ?: 0).toFloat()
                                .takeIf { it > 0f } ?: imgW
                            val ih = (dr?.intrinsicHeight ?: 0).toFloat()
                                .takeIf { it > 0f } ?: imgH
                            iv.imageMatrix = Matrix().apply {
                                setScale(dw / iw, dh / ih)
                                postTranslate(offset.x, offset.y)
                            }
                        },
                        modifier = Modifier.fillMaxSize()
                    )
                    Canvas(Modifier.fillMaxSize()) {
                        val c = Color.White.copy(alpha = 0.18f)
                        val st = 1.dp.toPx()
                        for (i in 1..2) {
                            val x = size.width * i / 3f
                            drawLine(c, Offset(x, 0f), Offset(x, size.height), st)
                            val y = size.height * i / 3f
                            drawLine(c, Offset(0f, y), Offset(size.width, y), st)
                        }
                        drawRect(
                            color = Color.White.copy(alpha = 0.35f),
                            size = size,
                            style = Stroke(st)
                        )
                    }
                }
            }
        }

        Column(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 4.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Text("Bandas:", fontSize = 11.sp)
                listOf(
                    PadChoice("Negro", "#000000", Color.Black),
                    PadChoice("Gris", "#101418", Color(0xFF101418)),
                    PadChoice("Azul", "#0B1220", Color(0xFF0B1220))
                ).forEach { p ->
                    FilterChip(
                        selected = !custom && pad.label == p.label,
                        onClick = { custom = false; pad = p },
                        label = { Text(p.label, fontSize = 10.sp) }
                    )
                }
                FilterChip(
                    selected = custom,
                    onClick = { custom = true },
                    label = { Text("Custom", fontSize = 10.sp) }
                )
                if (custom) {
                    PALETTE.forEach { hex ->
                        val col = Color(android.graphics.Color.parseColor(hex))
                        Box(
                            Modifier
                                .size(24.dp)
                                .clip(CircleShape)
                                .background(col)
                                .clickable {
                                    pad = PadChoice(hex, hex, col)
                                }
                        )
                    }
                }
            }

            Row(
                modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Text("Resolución:", fontSize = 11.sp)
                listOf(
                    0 to "Auto",
                    720 to "720",
                    1080 to "1080",
                    1440 to "1440"
                ).forEach { (v, label) ->
                    FilterChip(
                        selected = target == v,
                        onClick = { target = v },
                        label = { Text(label, fontSize = 10.sp) }
                    )
                }
            }

            Text("Fade ${String.format("%.2f", fade)} s", fontSize = 11.sp)
            Slider(
                value = fade,
                onValueChange = { fade = it },
                valueRange = 0f..2f,
                steps = 7,
                modifier = Modifier.height(28.dp)
            )

            if (state.status.isNotEmpty()) {
                Text(
                    if (state.progress >= 0f)
                        "${state.status} (${(state.progress * 100).toInt()}%)"
                    else
                        state.status,
                    fontSize = 10.sp,
                    color = MaterialTheme.colorScheme.primary,
                    maxLines = 2
                )
            }
            Spacer(Modifier.height(4.dp))
            Button(
                onClick = {
                    if (!ready || vpW <= 0f || vpH <= 0f) return@Button
                    val s = coverScale(zoom)
                    state.generateFromEditor(
                        CropRect(
                            x = ((-offset.x / s) / imgW).toDouble(),
                            y = ((-offset.y / s) / imgH).toDouble(),
                            w = ((vpW / s) / imgW).toDouble(),
                            h = ((vpH / s) / imgH).toDouble()
                        ),
                        fade.toDouble(),
                        pad.hex,
                        if (target == 0) autoTarget else target
                    )
                },
                modifier = Modifier.fillMaxWidth(),
                enabled = !state.busy && ready
            ) { Text(if (state.busy) "Generando…" else "Generar para mi móvil") }
        }
    }
}
