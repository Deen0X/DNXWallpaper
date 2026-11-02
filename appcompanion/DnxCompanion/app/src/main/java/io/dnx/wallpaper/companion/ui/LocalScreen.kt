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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedIconButton
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Icon
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.PushPin
import androidx.compose.material.icons.filled.Star
import androidx.compose.material.icons.outlined.StarBorder
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.dnx.wallpaper.companion.CompanionState
import io.dnx.wallpaper.companion.LocalItem
import io.dnx.wallpaper.companion.humanSize

/**
 * Catálogo local. Con [favoritesOnly] muestra solo los favoritos.
 * Tocar un elemento abre la hoja de acciones.
 */
@Composable
fun LocalScreen(
    state: CompanionState,
    favoritesOnly: Boolean,
    onAdd: () -> Unit
) {
    val items = if (favoritesOnly) state.favorites else state.local
    var selected by remember { mutableStateOf<LocalItem?>(null) }

    // Ficha pedida desde otra pantalla (p. ej. al terminar de generar).
    LaunchedEffect(state.openLocalId, items) {
        val id = state.openLocalId ?: return@LaunchedEffect
        val found = items.firstOrNull { it.id == id }
        if (found != null) {
            selected = found
            state.openLocalId = null
        }
    }

    Column(Modifier.fillMaxSize().padding(horizontal = 14.dp)) {
        Row(
            Modifier.fillMaxWidth().padding(top = 6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                if (favoritesOnly) "${items.size} favoritos" else "${items.size} wallpapers",
                fontSize = 11.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.weight(1f)
            )
            Text(
                humanSize(state.local.sumOf { it.size }),
                fontSize = 10.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        Button(
            onClick = onAdd,
            modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp),
            enabled = !state.busy
        ) { Text("Añadir wallpaper") }

        if (items.isEmpty()) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text(
                    if (favoritesOnly)
                        "Marca favoritos con la ★ en el catálogo local."
                    else
                        "Aún no hay wallpapers. Añade uno o descárgalo del catálogo remoto.",
                    fontSize = 12.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(24.dp)
                )
            }
        } else {
            LazyVerticalGrid(
                columns = GridCells.Fixed(2),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.fillMaxSize()
            ) {
                items(items, key = { it.id }) { li ->
                    LocalCard(li) { selected = li }
                }
            }
        }
    }

    selected?.let { li ->
        LocalSheet(state = state, itemArg = li, onClose = { selected = null })
    }
}

@Composable
private fun LocalCard(item: LocalItem, onClick: () -> Unit) {
    Column(Modifier.clip(RoundedCornerShape(12.dp)).clickable(onClick = onClick)) {
        Box {
            LocalThumb(item.thumbPath, Modifier.fillMaxWidth().aspectRatio(0.68f))
            if (item.isVideo) {
                Text(
                    "▶",
                    fontSize = 10.sp,
                    color = Color.White,
                    modifier = Modifier.align(Alignment.BottomStart).padding(6.dp)
                )
            }
            val badges = buildList {
                if (item.favorite) add("★")
                if (item.pinned) add("📌")
                if (item.optimized) add("ópt")
            }
            if (badges.isNotEmpty()) {
                Text(
                    badges.joinToString(" "),
                    fontSize = 9.sp,
                    color = Color.White,
                    modifier = Modifier.align(Alignment.TopEnd).padding(4.dp)
                )
            }
        }
        Text(
            item.title,
            fontSize = 10.sp,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(horizontal = 4.dp, vertical = 2.dp)
        )
        Text(
            humanSize(item.size),
            fontSize = 9.sp,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(horizontal = 4.dp, vertical = 2.dp)
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun LocalSheet(state: CompanionState, itemArg: LocalItem, onClose: () -> Unit) {
    val sheet = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    var confirmDelete by remember { mutableStateOf(false) }
    // Se lee siempre del estado: si no, tras marcar favorito/pin la ficha
    // seguía mostrando el objeto antiguo hasta cerrarla y abrirla.
    val item = state.local.firstOrNull { it.id == itemArg.id } ?: itemArg

    ModalBottomSheet(
        onDismissRequest = onClose,
        sheetState = sheet,
        containerColor = MaterialTheme.colorScheme.surface
    ) {
        Column(
            Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 18.dp)
                .padding(bottom = 20.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            if (item.isVideo) {
                VideoLoop(
                    item.path,
                    Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(12.dp))
                )
            } else {
                LocalThumb(
                    item.thumbPath,
                    Modifier
                        .fillMaxWidth()
                        .height(180.dp)
                        .clip(RoundedCornerShape(12.dp))
                )
            }
            Text(item.title, fontSize = 17.sp, fontWeight = FontWeight.SemiBold)
            Text(
                (if (item.isVideo) "Vídeo" else "Imagen") + " · " +
                    humanSize(item.size) +
                    if (item.optimized) " · optimizado" else " · sin optimizar",
                fontSize = 12.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Row(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Button(
                    onClick = { onClose(); state.applyLocal(item) },
                    modifier = Modifier.weight(1f).height(52.dp),
                    enabled = !state.busy
                ) { Text("Establecer fondo", fontSize = 15.sp) }
                OutlinedIconButton(
                    onClick = { state.toggleFavorite(item) },
                    modifier = Modifier.size(52.dp)
                ) {
                    Icon(
                        imageVector = if (item.favorite) Icons.Filled.Star
                        else Icons.Outlined.StarBorder,
                        contentDescription = if (item.favorite) "Quitar favorito" else "Favorito",
                        tint = if (item.favorite) Color(0xFFFFC44D)
                        else MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                OutlinedIconButton(
                    onClick = { state.togglePinned(item) },
                    modifier = Modifier.size(52.dp)
                ) {
                    Icon(
                        imageVector = Icons.Filled.PushPin,
                        contentDescription = if (item.pinned) "Quitar fijo" else "Fijar",
                        tint = if (item.pinned) Color(0xFFE5484D) else Color.White
                    )
                }
            }

            if (item.isVideo) {
                OutlinedButton(
                    onClick = { onClose(); state.optimizeLocal(item) },
                    modifier = Modifier.fillMaxWidth(),
                    enabled = !state.busy
                ) { Text("Optimizar en el PC (elegir encuadre)") }
            }
            Text(
                "Se guarda una copia en la app para usarla como fondo. " +
                    "Tu vídeo original no se toca.",
                fontSize = 10.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            TextButton(
                onClick = { confirmDelete = true },
                modifier = Modifier.fillMaxWidth()
            ) { Text("Quitar del catálogo") }
            Spacer(Modifier.height(4.dp))
        }
    }

    if (confirmDelete) {
        AlertDialog(
            onDismissRequest = { confirmDelete = false },
            title = { Text("Quitar wallpaper") },
            text = {
                Text(
                    "Se borra la copia de la app (${humanSize(item.size)}). " +
                        "El vídeo original de tu dispositivo no se modifica."
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    confirmDelete = false
                    onClose()
                    state.deleteLocal(item)
                }) { Text("Quitar") }
            },
            dismissButton = {
                TextButton(onClick = { confirmDelete = false }) { Text("Cancelar") }
            }
        )
    }
}
