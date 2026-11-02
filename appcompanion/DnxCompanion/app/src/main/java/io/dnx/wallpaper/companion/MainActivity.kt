package io.dnx.wallpaper.companion

import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.BitmapFactory
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.journeyapps.barcodescanner.ScanContract
import com.journeyapps.barcodescanner.ScanOptions
import io.dnx.wallpaper.companion.ui.DnxTheme
import io.dnx.wallpaper.companion.ui.EditorScreen
import io.dnx.wallpaper.companion.ui.LocalScreen
import io.dnx.wallpaper.companion.ui.RemoteScreen
import io.dnx.wallpaper.companion.ui.SettingsScreen
import io.dnx.wallpaper.companion.ui.TabGlyph

/**
 * DNXWallpaper v3 Companion.
 *
 * Pestañas: Catálogo local · Catálogo remoto · Favoritos · Ajustes.
 * El editor de encuadre ocupa la pantalla completa cuando está abierto.
 */
class MainActivity : ComponentActivity() {

    private var pendingUrl by mutableStateOf<String?>(null)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        handleIntent(intent)
        setContent { DnxTheme { DnxApp(pendingUrl) } }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleIntent(intent)
    }

    private fun handleIntent(intent: Intent?) {
        val data = intent?.data ?: return
        pendingUrl = if (data.scheme == "dnxwallpaper") {
            data.getQueryParameter("u")
        } else {
            data.toString()
        }
    }
}

/** Recorta el margen transparente de un bitmap (icono adaptativo). */
private fun trimTransparent(src: android.graphics.Bitmap): android.graphics.Bitmap {
    val w = src.width
    val h = src.height
    var left = w
    var top = h
    var right = -1
    var bottom = -1
    for (y in 0 until h) {
        for (x in 0 until w) {
            if (android.graphics.Color.alpha(src.getPixel(x, y)) > 8) {
                if (x < left) left = x
                if (x > right) right = x
                if (y < top) top = y
                if (y > bottom) bottom = y
            }
        }
    }
    if (right <= left || bottom <= top) return src
    return android.graphics.Bitmap.createBitmap(src, left, top, right - left + 1, bottom - top + 1)
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DnxApp(initialUrl: String?) {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val state = remember { CompanionState(ctx, scope) }
    var tab by remember { mutableIntStateOf(0) }
    var addDialog by remember { mutableStateOf(false) }
    var addPickedAsVideo by remember { mutableStateOf(true) }

    val suiteIcon = remember {
        try {
            BitmapFactory.decodeResource(ctx.resources, R.drawable.about)
        } catch (_: Exception) {
            null
        }
    }
    val androidIcon = remember {
        try {
            // El foreground de un icono adaptativo lleva mucho margen
            // transparente (zona segura), por eso se veía pequeño. Se recorta
            // al contenido real antes de pintarlo.
            val raw = BitmapFactory.decodeResource(ctx.resources, R.mipmap.ic_launcher_foreground)
            raw?.let { trimTransparent(it) }
        } catch (_: Exception) {
            null
        }
    }
    val versionName = remember {
        try {
            ctx.packageManager.getPackageInfo(ctx.packageName, 0).versionName ?: ""
        } catch (_: Exception) {
            ""
        }
    }

    val scanLauncher = rememberLauncherForActivityResult(ScanContract()) { res ->
        val text = res.contents
        if (!text.isNullOrBlank()) {
            state.openUrl(text.trim())
            tab = 1
        }
    }
    val pickLocal = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri ->
        if (uri != null) {
            state.importLocal(
                uri,
                isVideo = addPickedAsVideo,
                title = uriDisplayName(ctx, uri)
            )
            tab = 0
        }
    }

    val scan: () -> Unit = {
        val opts = ScanOptions().apply {
            setDesiredBarcodeFormats(ScanOptions.QR_CODE)
            setPrompt("Escanea el QR de DNXWallpaper v3")
            setBeepEnabled(false)
            setOrientationLocked(false)
            captureActivity = PortraitCaptureActivity::class.java
        }
        scanLauncher.launch(opts)
    }
    val paste: () -> Unit = {
        try {
            val cm = ctx.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
            val text = cm.primaryClip?.getItemAt(0)?.text?.toString()
            if (!text.isNullOrBlank()) {
                state.openUrl(text.trim())
                tab = 1
            } else {
                state.status = "El portapapeles está vacío"
            }
        } catch (_: Exception) {
            state.status = "No se pudo leer el portapapeles"
        }
    }

    LaunchedEffect(initialUrl) {
        if (!initialUrl.isNullOrBlank()) {
            state.openUrl(initialUrl)
            tab = 1
        }
    }

    // Editor de encuadre a pantalla completa.
    val editing = state.editorItem
    if (editing != null) {
        Surface(
            modifier = Modifier.fillMaxSize(),
            color = MaterialTheme.colorScheme.background
        ) {
            Box(Modifier.fillMaxSize().systemBarsPadding()) {
                EditorScreen(state, editing, onClose = { state.closeEditor() })
            }
        }
        return
    }

    Scaffold(
        containerColor = MaterialTheme.colorScheme.background,
        topBar = {
          Column {
            // Cabecera propia (más control que TopAppBar: icono grande y dos
            // líneas de texto sin recortes).
            Row(
                Modifier
                    .fillMaxWidth()
                    .statusBarsPadding()
                    .padding(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                androidIcon?.let {
                    Image(
                        bitmap = it.asImageBitmap(),
                        contentDescription = null,
                        modifier = Modifier
                            .size(56.dp)
                            .clip(RoundedCornerShape(14.dp))
                    )
                    Spacer(Modifier.width(12.dp))
                }
                Column(Modifier.weight(1f)) {
                    Row(verticalAlignment = Alignment.Bottom) {
                        Text(
                            "DNXWallpaper v3",
                            fontSize = 17.sp,
                            fontWeight = FontWeight.Bold,
                            maxLines = 1
                        )
                        Spacer(Modifier.width(6.dp))
                        Text(
                            "Companion",
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Medium,
                            color = MaterialTheme.colorScheme.primary,
                            maxLines = 1,
                            modifier = Modifier.padding(bottom = 1.dp)
                        )
                    }
                    Text(
                        when {
                            state.pcName.isNotEmpty() -> state.pcName
                            state.base.isNotEmpty() ->
                                state.base.removePrefix("http://")
                            else -> "Sin PC conectado"
                        },
                        fontSize = 11.sp,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
            TabRow(
                selectedTabIndex = tab,
                containerColor = MaterialTheme.colorScheme.background
            ) {
                repeat(4) { i ->
                    Tab(
                        selected = tab == i,
                        onClick = { tab = i },
                        text = {
                            TabGlyph(
                                kind = i,
                                color = if (tab == i)
                                    MaterialTheme.colorScheme.primary
                                else
                                    MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    )
                }
            }
          }
        },
        bottomBar = {
            // Con targetSdk 35+ la app va a sangre (edge-to-edge): sin este
            // inset, el estado y la barra de progreso quedaban debajo de la
            // barra de navegación de Android.
            Column(Modifier.navigationBarsPadding()) {
                if (state.status.isNotEmpty()) {
                    Text(
                        state.status,
                        fontSize = 11.sp,
                        color = MaterialTheme.colorScheme.primary,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 14.dp, vertical = 4.dp)
                    )
                }
                if (state.busy) {
                    if (state.progress < 0f) {
                        LinearProgressIndicator(Modifier.fillMaxWidth())
                    } else {
                        LinearProgressIndicator(
                            progress = { state.progress.coerceIn(0f, 1f) },
                            modifier = Modifier.fillMaxWidth()
                        )
                    }
                }
            }
        }
    ) { pad ->
        // Al terminar de generar un recorte se abre su ficha en la pestaña local.
        androidx.compose.runtime.LaunchedEffect(state.openLocalId) {
            if (state.openLocalId != null) tab = 0
        }
        Box(Modifier.fillMaxSize().padding(pad)) {
            when (tab) {
                0 -> LocalScreen(
                    state = state,
                    favoritesOnly = false,
                    onAdd = { addDialog = true }
                )

                1 -> RemoteScreen(state = state, onScan = scan, onPaste = paste)

                2 -> LocalScreen(
                    state = state,
                    favoritesOnly = true,
                    onAdd = { addDialog = true }
                )

                else -> SettingsScreen(
                    state = state,
                    onScan = scan,
                    onPaste = paste,
                    suiteIcon = suiteIcon,
                    androidIcon = androidIcon,
                    versionName = versionName
                )
            }
        }
    }

    if (addDialog) {
        AlertDialog(
            onDismissRequest = { addDialog = false },
            title = { Text("Añadir wallpaper local") },
            text = { Text("¿Qué quieres añadir al catálogo del dispositivo?") },
            confirmButton = {
                TextButton(onClick = {
                    addDialog = false
                    addPickedAsVideo = true
                    pickLocal.launch(arrayOf("video/*"))
                }) { Text("Vídeo") }
            },
            dismissButton = {
                TextButton(onClick = {
                    addDialog = false
                    addPickedAsVideo = false
                    pickLocal.launch(arrayOf("image/*"))
                }) { Text("Imagen") }
            }
        )
    }
}
