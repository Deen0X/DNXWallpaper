package io.dnx.wallpaper.companion.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val Bg = Color(0xFF14161A)
private val Surface = Color(0xFF1B1F26)
private val Surface2 = Color(0xFF232833)
private val Accent = Color(0xFF7C5CFF)
private val Accent2 = Color(0xFF3EA6FF)
private val Text = Color(0xFFE8EAED)
private val Muted = Color(0xFF9AA3AF)

val DnxColors = darkColorScheme(
    primary = Accent,
    onPrimary = Color.White,
    secondary = Accent2,
    onSecondary = Color.White,
    background = Bg,
    onBackground = Text,
    surface = Surface,
    onSurface = Text,
    surfaceVariant = Surface2,
    onSurfaceVariant = Muted,
    outline = Color(0xFF2E3542),
    error = Color(0xFFFF6B6B),
)

@Composable
fun DnxTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = DnxColors, content = content)
}
