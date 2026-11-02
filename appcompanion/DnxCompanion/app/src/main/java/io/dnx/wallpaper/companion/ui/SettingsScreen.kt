package io.dnx.wallpaper.companion.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.dnx.wallpaper.companion.CompanionState
import io.dnx.wallpaper.companion.humanSize

@Composable
fun SettingsScreen(
    state: CompanionState,
    onScan: () -> Unit,
    onPaste: () -> Unit,
    suiteIcon: android.graphics.Bitmap?,
    androidIcon: android.graphics.Bitmap?,
    versionName: String
) {
    var manual by remember { mutableStateOf(state.base) }
    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 14.dp)
    ) {
        SectionTitle("Conexión con el PC")
        Card(
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surface
            )
        ) {
            Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    if (state.base.isEmpty()) "Sin conexión" else state.base,
                    fontSize = 12.sp
                )
                if (state.pcName.isNotEmpty()) {
                    Text(
                        "Equipo: ${state.pcName}",
                        fontSize = 11.sp,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Button(
                    onClick = { state.findPc() },
                    modifier = Modifier.fillMaxWidth(),
                    enabled = !state.libLoading
                ) { Text(if (state.libLoading) "Buscando…" else "Buscar PC en la red") }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(onClick = onScan, modifier = Modifier.weight(1f)) {
                        Text("Escanear QR", fontSize = 12.sp)
                    }
                    OutlinedButton(onClick = onPaste, modifier = Modifier.weight(1f)) {
                        Text("Pegar URL", fontSize = 12.sp)
                    }
                }
                OutlinedTextField(
                    value = manual,
                    onValueChange = { manual = it },
                    label = { Text("Dirección del PC", fontSize = 12.sp) },
                    placeholder = { Text("http://192.168.1.10:8477", fontSize = 12.sp) },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
                OutlinedButton(
                    onClick = { state.setBaseManual(manual) },
                    modifier = Modifier.fillMaxWidth()
                ) { Text("Guardar dirección") }
            }
        }

        SectionTitle("Vídeo en el dispositivo")
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(
                "cover" to "Recortar",
                "fit" to "Ajustar",
                "stretch" to "Estirar"
            ).forEach { (value, label) ->
                FilterChip(
                    selected = state.fit == value,
                    onClick = { state.applyFit(value) },
                    label = { Text(label, fontSize = 11.sp) }
                )
            }
        }
        SectionTitle("Almacenamiento")
        Card(
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surface
            )
        ) {
            Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(
                    "Catálogo local: ${state.local.size} wallpapers · " +
                        humanSize(state.local.sumOf { it.size }),
                    fontSize = 12.sp
                )
                Text(
                    "Tope por vídeo generado",
                    fontSize = 11.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    listOf(4, 8, 16, 32).forEach { mb ->
                        FilterChip(
                            selected = state.maxMB == mb,
                            onClick = { state.changeMaxMB(mb) },
                            label = { Text("$mb MB", fontSize = 11.sp) }
                        )
                    }
                }
            }
        }

        SectionTitle("Acerca de")
        Card(
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surface
            )
        ) {
            Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    suiteIcon?.let {
                        Image(
                            bitmap = it.asImageBitmap(),
                            contentDescription = null,
                            modifier = Modifier.size(56.dp).clip(RoundedCornerShape(14.dp))
                        )
                        Spacer(Modifier.width(12.dp))
                    }
                    Column {
                        Text(
                            "DNXWallpaper v3",
                            fontSize = 15.sp,
                            fontWeight = FontWeight.SemiBold
                        )
                        Text(
                            "Suite de wallpapers · escritorio",
                            fontSize = 11.sp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    androidIcon?.let {
                        Image(
                            bitmap = it.asImageBitmap(),
                            contentDescription = null,
                            modifier = Modifier.size(56.dp).clip(RoundedCornerShape(14.dp))
                        )
                        Spacer(Modifier.width(12.dp))
                    }
                    Column {
                        Text(
                            "DNXWallpaper v3 Companion",
                            fontSize = 14.sp,
                            fontWeight = FontWeight.SemiBold
                        )
                        Text(
                            "Móvil · $versionName",
                            fontSize = 11.sp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
                Text(
                    "Todo ocurre en tu red local: no se envía nada a Internet.",
                    fontSize = 10.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}
