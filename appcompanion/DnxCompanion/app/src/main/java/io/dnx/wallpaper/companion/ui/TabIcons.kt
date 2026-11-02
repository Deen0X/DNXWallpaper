package io.dnx.wallpaper.companion.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

/**
 * Iconos de pestaña dibujados a mano (líneas simples, sin dependencias).
 * 0 = catálogo local (cuadrícula), 1 = remoto (globo), 2 = favoritos (estrella),
 * 3 = ajustes (deslizadores).
 */
@Composable
fun TabGlyph(kind: Int, color: Color) {
    Canvas(Modifier.size(22.dp)) {
        val s = size.minDimension
        val st = 1.6.dp.toPx()
        val stroke = Stroke(width = st)
        when (kind) {
            0 -> {
                drawRoundRect(
                    color = color,
                    size = Size(s, s),
                    cornerRadius = CornerRadius(4.dp.toPx()),
                    style = stroke
                )
                drawLine(color, Offset(s / 2, 0f), Offset(s / 2, s), st)
                drawLine(color, Offset(0f, s / 2), Offset(s, s / 2), st)
            }

            1 -> {
                drawCircle(color, radius = s * 0.42f, center = center, style = stroke)
                drawLine(
                    color, Offset(s * 0.5f, s * 0.08f),
                    Offset(s * 0.5f, s * 0.92f), st
                )
                drawLine(
                    color, Offset(s * 0.1f, s * 0.5f),
                    Offset(s * 0.9f, s * 0.5f), st
                )
            }

            2 -> {
                val rOut = s * 0.46f
                val rIn = s * 0.19f
                val path = Path()
                for (i in 0 until 10) {
                    val r = if (i % 2 == 0) rOut else rIn
                    val a = (-PI / 2 + i * PI / 5).toFloat()
                    val x = center.x + r * cos(a)
                    val y = center.y + r * sin(a)
                    if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
                }
                path.close()
                drawPath(path, color, style = stroke)
            }

            else -> {
                val xs = listOf(0.18f, 0.5f, 0.82f)
                xs.forEachIndexed { i, fx ->
                    val y = s * (0.22f + i * 0.28f)
                    drawLine(
                        color, Offset(s * 0.08f, y), Offset(s * 0.92f, y), st
                    )
                    drawCircle(
                        color, radius = s * 0.08f,
                        center = Offset(s * fx, y)
                    )
                }
            }
        }
    }
}
