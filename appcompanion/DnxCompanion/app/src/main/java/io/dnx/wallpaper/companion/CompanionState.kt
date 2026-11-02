package io.dnx.wallpaper.companion

import android.content.Context
import android.net.Uri
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject

/** Rectángulo de recorte normalizado (0..1) sobre el vídeo original. */
data class CropRect(val x: Double, val y: Double, val w: Double, val h: Double)

/**
 * Estado compartido de la app: conexión con el PC, catálogo remoto, catálogo
 * local, favoritos y editor de encuadre.
 */
class CompanionState(val ctx: Context, private val scope: CoroutineScope) {

    private val prefs = ctx.getSharedPreferences("dnxwallpaper", Context.MODE_PRIVATE)
    val store = LocalStore(ctx)

    // ---- conexión
    var base by mutableStateOf(prefs.getString("base", "") ?: "")
        private set

    init {
        // Copias de vídeos que se abrieron para ajustar y nunca se generaron.
        try {
            store.purgeImports()
        } catch (_: Exception) {
        }
    }

    var pcName by mutableStateOf(prefs.getString("pc", "") ?: "")
        private set

    // ---- UI
    var status by mutableStateOf("")
    var busy by mutableStateOf(false)
        private set
    var progress by mutableFloatStateOf(-1f)
        private set

    // ---- catálogo remoto
    var lib by mutableStateOf<List<LibItem>>(emptyList())
        private set
    var libLoading by mutableStateOf(false)
        private set
    var query by mutableStateOf("")
    var filter by mutableStateOf("all")
    var detail by mutableStateOf<LibItem?>(null)
    // ---- catálogo local
    var local by mutableStateOf(store.list())
        private set
    /** Id del item local cuya ficha debe abrirse (p. ej. tras generar un recorte). */
    var openLocalId by mutableStateOf<String?>(null)
    var favorites by mutableStateOf(store.favorites())
        private set

    // ---- ajustes
    var fit by mutableStateOf(prefs.getString("fit", "cover") ?: "cover")
        private set
    var maxMB by mutableIntStateOf(prefs.getInt("maxMB", 8))
        private set

    // ---- editor
    var editorItem by mutableStateOf<LibItem?>(null)
        private set

    // ---- plugins (filtro por proveedor)
    var plugins by mutableStateOf<List<Pair<String, String>>>(emptyList())
    var provider by mutableStateOf("")

    val visibleLib: List<LibItem>
        get() = lib.filter { li ->
            if (provider.isNotEmpty() && li.provider != provider) return@filter false
            val byType = when (filter) {
                "video" -> li.isVideo
                "image" -> !li.isVideo
                "portrait" -> li.isPortrait
                else -> true
            }
            byType && (query.isBlank() || li.title.contains(query, true))
        }

    fun refreshLocal() {
        local = store.list()
        favorites = store.favorites()
    }

    // ------------------------------------------------------------ conexión

    private fun rememberBase(b: String, name: String? = null) {
        base = b
        prefs.edit().putString("base", b).apply()
        if (!name.isNullOrBlank()) {
            pcName = name
            prefs.edit().putString("pc", name).apply()
        }
    }

    fun findPc() {
        if (busy || libLoading) return
        scope.launch {
            libLoading = true
            status = "Buscando el PC en la red…"
            val f = withContext(Dispatchers.IO) { discoverPc() }
            if (f.error != null || f.base.isEmpty()) {
                libLoading = false
                status = "No se encontró el PC: ${f.error ?: "red"}"
                return@launch
            }
            rememberBase(f.base, f.name)
            val r = withContext(Dispatchers.IO) { loadLibrary(f.base, 0, 24) }
            libLoading = false
            if (r.second == null) {
                lib = r.first
                status = if (r.first.isEmpty()) "La biblioteca del PC está vacía"
                else "PC encontrado: ${f.name}"
            } else {
                status = "PC encontrado, pero la biblioteca falló: ${r.second}"
            }
        }
    }

    fun setBaseManual(url: String) {
        val clean = url.trim().trimEnd('/')
        if (!clean.startsWith("http")) {
            status = "La dirección debe empezar por http://"
            return
        }
        rememberBase(clean)
        status = "PC: $clean"
    }

    fun loadPlugins() {
        if (base.isEmpty()) return
        scope.launch {
            val list = withContext(Dispatchers.IO) {
                try {
                    val conn = (java.net.URL("$base/api/plugins").openConnection()
                        as java.net.HttpURLConnection)
                    conn.connectTimeout = 6000
                    conn.readTimeout = 15000
                    val text = conn.inputStream.bufferedReader().use { it.readText() }
                    conn.disconnect()
                    val arr = org.json.JSONArray(text)
                    (0 until arr.length()).mapNotNull { i ->
                        val o = arr.getJSONObject(i)
                        val id = o.optString("id", "")
                        if (id.isEmpty()) null else id to o.optString("name", id)
                    }
                } catch (e: Exception) {
                    emptyList()
                }
            }
            plugins = list
        }
    }

    fun randomizeLibrary() {        if (base.isEmpty()) {
            status = "Conecta con el PC primero"
            return
        }
        if (libLoading || busy) return
        scope.launch {
            libLoading = true
            status = "Barajando el catálogo… (puede tardar)"
            val r = withContext(Dispatchers.IO) {
                io.dnx.wallpaper.companion.randomizeLibrary(base)
            }
            libLoading = false
            if (r.second != null) {
                status = "Barajado: ${r.second}"
            } else {
                lib = r.first
                status = "${lib.size} wallpapers al azar"
            }
        }
    }

    fun loadPage(reset: Boolean) {
        if (base.isEmpty()) {
            status = "Conecta con el PC primero"
            return
        }
        if (libLoading || busy) return
        scope.launch {
            libLoading = true
            val off = if (reset) 0 else lib.size
            // Al entrar se trae una página grande: con 24 items los filtros
            // (imágenes/vertical) salían vacíos hasta pulsar "Cargar más".
            val r = withContext(Dispatchers.IO) {
                loadLibrary(base, off, if (reset) 500 else 100, query)
            }
            libLoading = false
            if (r.second != null) {
                status = "Biblioteca: ${r.second}"
            } else {
                if (status.startsWith("Biblioteca:")) status = ""
                lib = if (reset) r.first else lib + r.first
                if (reset && r.first.isEmpty()) status = "La biblioteca del PC está vacía"
            }
        }
    }

    private var searchJob: kotlinx.coroutines.Job? = null

    /** Texto de búsqueda: filtra ya en local y recarga del servidor con retardo. */
    fun updateQuery(q: String) {
        query = q
        if (base.isEmpty() || busy) return
        searchJob?.cancel()
        searchJob = scope.launch {
            delay(500)
            loadPage(reset = true)
        }
    }

    /** Enter en el buscador: pregunta a los plugins del PC (búsqueda en vivo). */
    fun searchPluginsLive() {
        val q = query.trim()
        if (q.isEmpty() || base.isEmpty() || busy || libLoading) return
        scope.launch {
            libLoading = true
            status = "Buscando «$q» en los plugins…"
            val r = withContext(Dispatchers.IO) { searchPlugins(base, q) }
            libLoading = false
            if (r.second != null) {
                status = "Búsqueda en plugins: ${r.second}"
            } else {
                lib = r.first
                status = if (r.first.isEmpty()) "Sin resultados en los plugins"
                         else "${r.first.size} resultados en los plugins"
            }
        }
    }

    fun openUrl(url: String) {
        scope.launch {
            busy = true
            status = "Leyendo datos…"
            val r = withContext(Dispatchers.IO) { loadItem(url) }
            busy = false
            if (r.item == null) {
                status = "No se pudo leer: ${r.error ?: "PC encendido y misma Wi-Fi"}"
                return@launch
            }
            if (base.isEmpty()) rememberBase(r.item.base)
            detail = LibItem(
                key = r.item.key,
                title = r.item.title,
                isVideo = r.item.isVideo,
                width = r.item.width,
                height = r.item.height,
                thumbUrl = r.item.thumbUrl
            )
            status = ""
        }
    }

    // ------------------------------------------------------------ descarga

    /**
     * Descarga un elemento remoto al catálogo local.
     * mobile = true pide el proxy 1080p H.264 del PC.
     */
    fun downloadRemote(li: LibItem, mobile: Boolean) {
        if (busy) return
        if (base.isEmpty()) {
            status = "Conecta con el PC primero"
            return
        }
        scope.launch {
            busy = true
            progress = 0f
            status = if (mobile) "Descargando versión móvil…" else "Descargando…"
            val url = base + "/dl/" + URLEncoder.encode(li.key, "UTF-8") +
                if (mobile && li.isVideo) "?m=1" else ""
            val tmp = File(ctx.cacheDir, "remote.tmp")
            val err = withContext(Dispatchers.IO) {
                fetchToFile(url, tmp) { p -> progress = p }
            }
            if (err != null) {
                busy = false
                progress = -1f
                status = "Error al descargar: $err"
                return@launch
            }
            val item = withContext(Dispatchers.IO) {
                store.importFile(
                    tmp, li.title, li.isVideo,
                    optimized = mobile && li.isVideo, sourceKey = li.key
                )
            }
            tmp.delete()
            busy = false
            progress = -1f
            refreshLocal()
            if (item == null) {
                status = "No se pudo guardar en el catálogo"
            } else {
                detail = null
                status = "Guardado en el catálogo: ${item.title}"
            }
        }
    }

    // ------------------------------------------------------------ local

    fun importLocal(uri: Uri, isVideo: Boolean, title: String) {
        if (busy) return
        scope.launch {
            busy = true
            status = "Añadiendo «$title»…"
            val item = try {
                withContext(Dispatchers.IO) { store.importUri(uri, title, isVideo) }
            } catch (e: Exception) {
                null
            } finally {
                busy = false
            }
            if (item == null) {
                status = "No se pudo leer el fichero"
                return@launch
            }
            refreshLocal()
            // Se abre su ficha: desde ahí se aplica (inicio/bloqueo/ambas) o se
            // manda al PC para recortar. Sin PC también funciona.
            openLocalId = item.id
            status = "Listo: ${item.title}"
        }
    }

    fun toggleFavorite(it: LocalItem) {
        store.toggleFavorite(it)
        refreshLocal()
    }

    fun togglePinned(it: LocalItem) {
        store.togglePinned(it)
        refreshLocal()
    }

    fun deleteLocal(it: LocalItem) {
        store.delete(it)
        refreshLocal()
        status = "Borrado: ${it.title}"
    }

    /** ¿Está activo nuestro servicio de fondos animados? */
    fun ourWallpaperActive(): Boolean {
        return try {
            val wm = android.app.WallpaperManager.getInstance(ctx)
            val info = wm.wallpaperInfo ?: return false
            info.serviceInfo?.packageName == ctx.packageName
        } catch (_: Exception) {
            false
        }
    }

    /** Se llama al volver a la app: confirma si el fondo quedó aplicado. */
    fun refreshWallpaperState() {
        if (!busy && ourWallpaperActive()) {
            status = "Fondo animado aplicado"
        }
    }

    fun applyLocal(it: LocalItem) {
        if (busy) return
        scope.launch {
            busy = true
            status = "Aplicando ${it.title}…"
            val r = withContext(Dispatchers.IO) { store.apply(it) }
            busy = false
            if (!r.first) {
                status = "No se pudo aplicar el fondo"
            } else if (r.second) {
                status = if (launchLiveWallpaper(ctx))
                    "Elige «DNXWallpaper v3» y confirma"
                else
                    "Este dispositivo no admite fondos animados (FydeOS/ChromeOS)"
            } else {
                status = "Fondo aplicado"
            }
        }
    }

    // ------------------------------------------------------------ ajustes

    fun applyFit(v: String) {
        fit = v
        prefs.edit().putString("fit", v).apply()
    }

    fun changeMaxMB(v: Int) {
        maxMB = v
        prefs.edit().putInt("maxMB", v).apply()
    }

    fun clearWallpaper() {
        if (busy) return
        scope.launch {
            busy = true
            status = "Restaurando el fondo del sistema…"
            val msg = withContext(Dispatchers.IO) {
                try {
                    val wm = android.app.WallpaperManager.getInstance(ctx)
                    wm.clear()
                    if (wm.wallpaperInfo != null) {
                        val w = if (wm.desiredMinimumWidth > 0) wm.desiredMinimumWidth else 1080
                        val h = if (wm.desiredMinimumHeight > 0) wm.desiredMinimumHeight else 1920
                        val bmp = android.graphics.Bitmap.createBitmap(
                            w, h, android.graphics.Bitmap.Config.ARGB_8888
                        )
                        bmp.eraseColor(0xFF101418.toInt())
                        wm.setBitmap(bmp)
                        bmp.recycle()
                    }
                    "Fondo restablecido al predeterminado"
                } catch (e: Exception) {
                    Log.e("DNXWallpaper", "clear wallpaper", e)
                    "No se pudo restablecer: ${e.message ?: "error"}"
                }
            }
            busy = false
            status = msg
        }
    }

    // ------------------------------------------------------------ editor

    fun openEditor(li: LibItem) {
        if (!li.isVideo) {
            status = "El recorte con viewport es para vídeos; las imágenes se aplican tal cual"
            return
        }
        editorItem = li
        detail = null
    }

    fun closeEditor() {
        editorItem = null
    }

    /**
     * Pide al PC la exportación con el encuadre elegido y guarda el resultado
     * en el catálogo local.
     */
    fun generateFromEditor(rect: CropRect, fade: Double, padColor: String, target: Int) {
        val li = editorItem ?: return
        if (busy) return
        scope.launch {
            busy = true
            progress = 0f
            status = "El PC está generando el vídeo…"
            val id = withContext(Dispatchers.IO) {
                startMobileExport(
                    base = base,
                    key = li.key,
                    rect = rect,
                    target = target,
                    quality = 4,
                    fade = fade,
                    maxMB = maxMB,
                    padColor = padColor
                )
            }
            if (id == null) {
                busy = false
                progress = -1f
                status = "No se pudo iniciar la exportación"
                return@launch
            }
            var done = false
            var error: String? = null
            while (!done) {
                delay(1200)
                val st = withContext(Dispatchers.IO) { exportStatus(base, id) }
                when {
                    st == null -> {
                        error = "sin respuesta del PC"
                        done = true
                    }

                    st.optString("state") == "error" -> {
                        error = st.optString("error", "error")
                        done = true
                    }

                    st.optString("state") == "done" -> {
                        progress = 1f
                        done = true
                    }

                    else -> {
                        val pc = st.optInt("percent", 0)
                        progress = (pc / 100f).coerceIn(0f, 0.95f)
                    }
                }
            }
            if (error != null) {
                busy = false
                progress = -1f
                status = "Error al generar: $error"
                return@launch
            }
            status = "Descargando la versión móvil…"
            val tmp = File(ctx.cacheDir, "export.mp4")
            val err = withContext(Dispatchers.IO) {
                fetchToFile("$base/m/$id", tmp) { p -> progress = 0.95f + p * 0.05f }
            }
            if (err != null) {
                busy = false
                progress = -1f
                status = "No se pudo descargar: $err"
                return@launch
            }
            val item = withContext(Dispatchers.IO) {
                store.replaceOrImport(
                    tmp, li.title, isVideo = true, optimized = true, sourceKey = li.key
                )
            }
            tmp.delete()
            busy = false
            progress = -1f
            refreshLocal()
            editorItem = null
            // Llevar al usuario a la ficha del vídeo generado: desde ahí puede
            // aplicarlo directamente (antes volvía al catálogo y tenía que
            // buscarlo).
            if (item != null) openLocalId = item.id
            status = if (item == null) "No se pudo guardar el vídeo"
            else "Generado: ${item.title}"
        }
    }

    /**
     * Envía un vídeo del catálogo local al PC y abre el editor de encuadre.
     * El PC devuelve el id de subida y las dimensiones reales.
     */
    fun optimizeLocal(it: LocalItem) {
        if (busy) return
        if (!it.isVideo) {
            status = "Solo se optimizan vídeos"
            return
        }
        if (base.isEmpty()) {
            status = "Conecta con el PC primero"
            return
        }
        scope.launch {
            busy = true
            progress = 0f
            status = "Enviando «${it.title}» al PC…"
            val r = withContext(Dispatchers.IO) {
                uploadFile(
                    base = base,
                    file = File(it.path),
                    onProgress = { p -> progress = p }
                )
            }
            busy = false
            progress = -1f
            if (r.error != null || r.id == null) {
                status = "No se pudo enviar: ${r.error ?: "error"}"
                return@launch
            }
            status = "En el PC. Elige el encuadre."
            editorItem = LibItem(
                key = "up:" + r.id,
                title = it.title,
                isVideo = true,
                width = r.width,
                height = r.height,
                thumbUrl = base + "/up/" + r.id + ".jpg"
            )
        }
    }

    fun screenSize(): Pair<Int, Int> {
        val m = ctx.resources.displayMetrics
        return Pair(m.widthPixels, m.heightPixels)
    }
}

// ---------------------------------------------------------------- descargas

/** Descarga una URL a `dest` con progreso. Devuelve null si fue bien. */
fun fetchToFile(url: String, dest: File, onProgress: (Float) -> Unit): String? {
    return try {
        val conn = (URL(url).openConnection() as HttpURLConnection)
        conn.connectTimeout = 10000
        conn.readTimeout = 300000
        val code = conn.responseCode
        if (code !in 200..299) {
            conn.disconnect()
            return "HTTP $code"
        }
        val total = conn.contentLengthLong
        conn.inputStream.use { input ->
            dest.outputStream().use { out ->
                val buf = ByteArray(128 * 1024)
                var read: Int
                var done = 0L
                while (input.read(buf).also { read = it } > 0) {
                    out.write(buf, 0, read)
                    done += read
                    if (total > 0) onProgress((done.toFloat() / total).coerceIn(0f, 1f))
                }
            }
        }
        conn.disconnect()
        if (dest.length() > 0) null else "fichero vacío"
    } catch (e: Exception) {
        Log.e("DNXWallpaper", "fetchToFile $url", e)
        e.message ?: e.javaClass.simpleName
    }
}

/** POST /api/editor/export con la clave del catálogo. Devuelve el id del job. */
fun startMobileExport(
    base: String,
    key: String,
    rect: CropRect,
    target: Int,
    quality: Int,
    fade: Double,
    maxMB: Int,
    padColor: String
): String? {
    return try {
        val body = JSONObject()
            .put("key", key)
            .put("title", key.substringAfter(':').replace('-', ' '))
            .put("device", "Móvil")
            .put(
                "rect", JSONObject()
                    .put("x", rect.x)
                    .put("y", rect.y)
                    .put("w", rect.w)
                    .put("h", rect.h)
            )
            .put("target", target)
            .put("quality", quality)
            .put("fade", fade)
            .put("maxSizeMB", maxMB)
            .put("padColor", padColor)
            .toString()
        val conn = (URL("$base/api/editor/export").openConnection() as HttpURLConnection)
        conn.requestMethod = "POST"
        conn.doOutput = true
        conn.connectTimeout = 15000
        // El servidor puede tardar en descargar el medio la primera vez.
        conn.readTimeout = 60000
        conn.setRequestProperty("Content-Type", "application/json")
        conn.outputStream.use { it.write(body.toByteArray()) }
        val text = conn.inputStream.bufferedReader().use { it.readText() }
        conn.disconnect()
        val j = JSONObject(text)
        if (j.optBoolean("ok")) j.optString("id").ifBlank { null } else null
    } catch (e: Exception) {
        Log.e("DNXWallpaper", "startMobileExport", e)
        null
    }
}

fun exportStatus(base: String, id: String): JSONObject? {
    return try {
        val text = URL("$base/api/editor/status/$id").openStream()
            .bufferedReader().use { it.readText() }
        JSONObject(text)
    } catch (e: Exception) {
        Log.e("DNXWallpaper", "exportStatus $id", e)
        null
    }
}

data class UploadResult(
    val id: String?,
    val width: Int,
    val height: Int,
    val error: String?
)

/** Sube un fichero al PC (POST /api/upload) en streaming. */
fun uploadFile(
    base: String,
    file: File,
    onProgress: (Float) -> Unit
): UploadResult {
    return try {
        val conn = (URL("$base/api/upload").openConnection() as HttpURLConnection)
        conn.requestMethod = "POST"
        conn.doOutput = true
        conn.connectTimeout = 8000
        conn.readTimeout = 600000
        conn.setFixedLengthStreamingMode(file.length())
        conn.setRequestProperty("Content-Type", "application/octet-stream")
        conn.setRequestProperty("X-Filename", file.name)
        val total = file.length().toFloat()
        var done = 0L
        conn.outputStream.use { out ->
            file.inputStream().use { input ->
                val buf = ByteArray(256 * 1024)
                var read: Int
                while (input.read(buf).also { read = it } > 0) {
                    out.write(buf, 0, read)
                    done += read
                    if (total > 0) onProgress((done / total).coerceIn(0f, 1f))
                }
            }
        }
        val text = conn.inputStream.bufferedReader().use { it.readText() }
        conn.disconnect()
        val j = JSONObject(text)
        if (!j.optBoolean("ok")) {
            UploadResult(null, 0, 0, j.optString("error", "error"))
        } else {
            UploadResult(
                j.optString("id").ifBlank { null },
                j.optInt("width", 0),
                j.optInt("height", 0),
                null
            )
        }
    } catch (e: Exception) {
        Log.e("DNXWallpaper", "uploadFile", e)
        UploadResult(null, 0, 0, e.message ?: e.javaClass.simpleName)
    }
}
