package io.dnx.wallpaper.companion.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.OutlinedIconButton
import androidx.compose.material3.AlertDialog
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.dnx.wallpaper.companion.CompanionState
import io.dnx.wallpaper.companion.LibItem

/** Catálogo del PC (remoto). */
@Composable
fun RemoteScreen(
    state: CompanionState,
    onScan: () -> Unit,
    onPaste: () -> Unit
) {
    var showPlugins by remember { mutableStateOf(false) }
    val ctx = LocalContext.current
    Column(Modifier.fillMaxSize().padding(horizontal = 14.dp)) {
        LaunchedEffect(state.base) {
            if (state.base.isNotEmpty()) {
                state.loadPlugins()
                if (state.lib.isEmpty() && !state.libLoading) state.loadPage(true)
            }
        }
        if (state.base.isEmpty()) {
            ConnectCard(state, onScan, onPaste)
        } else {
            Row(
                Modifier.fillMaxWidth().padding(top = 8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                OutlinedTextField(
                    value = state.query,
                    onValueChange = { state.updateQuery(it) },
                    label = { Text("Buscar en el catálogo", fontSize = 12.sp) },
                    singleLine = true,
                    keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(
                        imeAction = androidx.compose.ui.text.input.ImeAction.Search
                    ),
                    keyboardActions = androidx.compose.foundation.text.KeyboardActions(
                        onSearch = { state.searchPluginsLive() }
                    ),
                    modifier = Modifier.weight(1f)
                )
                Spacer(Modifier.width(6.dp))
                OutlinedIconButton(
                    onClick = {
                        state.loadPlugins()
                        showPlugins = true
                    },
                    modifier = Modifier.size(52.dp)
                ) { Text("🧩", fontSize = 18.sp) }
            }
            Row(
                Modifier.fillMaxWidth().padding(vertical = 4.dp),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Emojis: no necesitan traducción y no parten la línea.
                listOf(
                    "all" to ("✨" to "Todo"),
                    "video" to ("🎬" to "Vídeos"),
                    "image" to ("🖼️" to "Imágenes"),
                    "portrait" to ("📱" to "Vertical")
                ).forEach { (value, pair) ->
                    FilterChip(
                        selected = state.filter == value,
                        onClick = {
                            state.filter = value
                            toast(ctx, pair.second)
                        },
                        label = { Text(pair.first, fontSize = 15.sp) }
                    )
                }
                Spacer(Modifier.weight(1f))
                // Barajado: cada plugin trae una página distinta.
                OutlinedButton(
                    onClick = { state.randomizeLibrary() },
                    enabled = !state.libLoading,
                    contentPadding = PaddingValues(horizontal = 10.dp),
                    modifier = Modifier.height(32.dp)
                ) {
                    Text(if (state.libLoading) "…" else "🎲", fontSize = 15.sp)
                }
            }
            LazyVerticalGrid(
                columns = GridCells.Fixed(3),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp),
                // Deja hueco para la barra de navegación: antes "Cargar más"
                // quedaba detrás y no se podía pulsar.
                contentPadding = PaddingValues(bottom = 96.dp),
                modifier = Modifier.fillMaxSize().padding(top = 6.dp)
            ) {
                if (state.lib.isNotEmpty() && state.visibleLib.isEmpty()) {
                    item(span = { GridItemSpan(maxLineSpan) }) {
                        Column(Modifier.fillMaxWidth().padding(top = 40.dp)) {
                            Text(
                                if (state.provider.isNotEmpty())
                                    "Este plugin no tiene resultados con el filtro actual."
                                else
                                    "Ningún resultado con el filtro actual.",
                                fontSize = 12.sp,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                            OutlinedButton(
                                onClick = {
                                    state.filter = "all"
                                    state.provider = ""
                                },
                                modifier = Modifier.padding(top = 8.dp)
                            ) { Text("Quitar filtros") }
                        }
                    }
                }
                if (state.lib.isEmpty() && !state.libLoading) {
                    item(span = { GridItemSpan(maxLineSpan) }) {
                        Column(Modifier.fillMaxWidth().padding(top = 40.dp)) {
                            Text(
                                "Sin wallpapers cargados todavía.",
                                fontSize = 12.sp,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                            OutlinedButton(
                                onClick = { state.loadPage(true) },
                                modifier = Modifier.padding(top = 8.dp)
                            ) { Text("Cargar catálogo") }
                        }
                    }
                }
                items(state.visibleLib) { li ->
                    RemoteCard(li) { state.detail = li }
                }
                if (state.lib.isNotEmpty() && state.lib.size >= 24) {
                    item(span = { GridItemSpan(maxLineSpan) }) {
                        TextButton(
                            onClick = { state.loadPage(false) },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(if (state.libLoading) "Cargando…" else "Cargar más")
                        }
                    }
                }
            }
        }
    }
    state.detail?.let { DetailSheet(state, it) }
    if (showPlugins) {
        AlertDialog(
            onDismissRequest = { showPlugins = false },
            title = { Text("Plugins", fontSize = 16.sp) },
            text = {
                Column(Modifier.verticalScroll(rememberScrollState())) {
                    val options = listOf("" to "Todos los plugins") + state.plugins
                    options.forEach { (id, name) ->
                        TextButton(
                            onClick = {
                                state.provider = id
                                showPlugins = false
                                toast(ctx, name)
                            },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(
                                (if (state.provider == id) "✓ " else "") + name,
                                fontSize = 13.sp,
                                modifier = Modifier.fillMaxWidth()
                            )
                        }
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { showPlugins = false }) { Text("Cerrar") }
            }
        )
    }
}

/** Aviso breve abajo (1-2 s). */
private fun toast(ctx: android.content.Context, msg: String) {
    android.widget.Toast.makeText(ctx, msg, android.widget.Toast.LENGTH_SHORT).show()
}

@Composable
private fun RemoteCard(li: LibItem, onClick: () -> Unit) {
    Column(Modifier.clip(RoundedCornerShape(10.dp)).clickable(onClick = onClick)) {
        Box {
            Thumb(li.thumbUrl, Modifier.fillMaxWidth().aspectRatio(0.66f))
            if (li.isVideo) {
                Text(
                    "▶",
                    fontSize = 10.sp,
                    color = Color.White,
                    modifier = Modifier
                        .align(Alignment.BottomStart)
                        .padding(4.dp)
                )
            }
        }
        Text(
            li.title,
            fontSize = 10.sp,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(4.dp)
        )
    }
}

@Composable
private fun ConnectCard(
    state: CompanionState,
    onScan: () -> Unit,
    onPaste: () -> Unit
) {
    Card(
        Modifier.fillMaxWidth().padding(top = 12.dp),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surface
        )
    ) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text("Conecta con tu PC", fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
            Text(
                "El PC hace de biblioteca y de editor. Todo va por tu red local.",
                fontSize = 11.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Button(
                onClick = { state.findPc() },
                modifier = Modifier.fillMaxWidth(),
                enabled = !state.libLoading
            ) { Text(if (state.libLoading) "Buscando…" else "Buscar PC en la red") }
            OutlinedButton(onClick = onScan, modifier = Modifier.fillMaxWidth()) {
                Text("Escanear QR del PC")
            }
            OutlinedButton(onClick = onPaste, modifier = Modifier.fillMaxWidth()) {
                Text("Pegar URL")
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun DetailSheet(state: CompanionState, li: LibItem) {
    val sheet = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    ModalBottomSheet(
        onDismissRequest = { state.detail = null },
        sheetState = sheet,
        containerColor = MaterialTheme.colorScheme.surface
    ) {
        Column(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = 18.dp)
                .padding(bottom = 22.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            AnimatedThumb(
                li.thumbUrl,
                Modifier
                    .fillMaxWidth()
                    .height(200.dp)
                    .clip(RoundedCornerShape(12.dp)),
                contentScale = ContentScale.Fit
            )
            Text(li.title, fontSize = 17.sp, fontWeight = FontWeight.SemiBold)
            Text(
                (if (li.isVideo) "Vídeo" else "Imagen") +
                    if (li.width > 0) " · ${li.width}×${li.height}" else "",
                fontSize = 12.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            if (li.isVideo) {
                Button(
                    onClick = { state.openEditor(li) },
                    modifier = Modifier.fillMaxWidth(),
                    enabled = !state.busy
                ) { Text("Adaptar al móvil (elegir encuadre)") }
                OutlinedButton(
                    onClick = { state.downloadRemote(li, mobile = true) },
                    modifier = Modifier.fillMaxWidth(),
                    enabled = !state.busy
                ) { Text("Descargar versión móvil (1080p)") }
                OutlinedButton(
                    onClick = { state.downloadRemote(li, mobile = false) },
                    modifier = Modifier.fillMaxWidth(),
                    enabled = !state.busy
                ) { Text("Descargar original") }
            } else {
                Button(
                    onClick = { state.downloadRemote(li, mobile = false) },
                    modifier = Modifier.fillMaxWidth(),
                    enabled = !state.busy
                ) { Text("Descargar al catálogo") }
            }
            Spacer(Modifier.height(8.dp))
        }
    }
}
