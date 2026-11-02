package io.dnx.wallpaper.companion

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.util.Log
import java.io.File
import org.json.JSONArray
import org.json.JSONObject

/**
 * Catálogo local de wallpapers (almacenamiento interno de la app).
 *
 * Estructura: `filesDir/wallpapers/<id>.<mp4|jpg>` + `<id>.thumb.jpg`, y un
 * índice `index.json` con título, tamaño, favorito y fijado.
 */
data class LocalItem(
    val id: String,
    val title: String,
    val isVideo: Boolean,
    val path: String,
    val thumbPath: String?,
    val size: Long,
    val createdAt: Long,
    val favorite: Boolean,
    val pinned: Boolean,
    val optimized: Boolean,
    val sourceKey: String?,
    /**
     * "wallpaper" = versión preparada para usar (generada/optimizada).
     * "import"   = copia temporal de un vídeo del dispositivo que aún no se ha
     *              ajustado; no se muestra en el catálogo y se puede purgar.
     */
    val kind: String = "wallpaper"
)

class LocalStore(private val ctx: Context) {

    private val dir = File(ctx.filesDir, "wallpapers").apply { mkdirs() }
    private val index = File(dir, "index.json")

    fun list(): List<LocalItem> {
        pruneDuplicateExports()
        // Sólo wallpapers preparados: las copias sin ajustar no se muestran.
        val all = readIndex().filter { File(it.path).isFile && it.kind == "wallpaper" }
        return all.sortedWith(
            compareByDescending<LocalItem> { it.pinned }.thenByDescending { it.createdAt }
        )
    }

    /**
     * Versiones anteriores añadían un item nuevo por cada recorte, así que
     * podían quedar varias copias del mismo wallpaper de origen. Se conserva
     * la más reciente (que es la que tiene el último recorte) y se borran las
     * demás, incluidas sus miniaturas.
     */
    private fun pruneDuplicateExports() {
        val all = readIndex()
        val byKey = all.filter { !it.sourceKey.isNullOrBlank() }.groupBy { it.sourceKey }
        val doomed = mutableListOf<LocalItem>()
        byKey.values.forEach { group ->
            if (group.size <= 1) return@forEach
            val keep = group.maxByOrNull { it.createdAt } ?: return@forEach
            group.filter { it.id != keep.id }.forEach { doomed.add(it) }
        }
        if (doomed.isEmpty()) return
        doomed.forEach { it ->
            runCatching { File(it.path).delete() }
            it.thumbPath?.let { p -> runCatching { File(p).delete() } }
        }
        val ids = doomed.map { it.id }.toSet()
        writeIndex(all.filter { it.id !in ids })
        Log.i("DNXWallpaper", "duplicados de recorte borrados: ${doomed.size}")
    }

    fun favorites(): List<LocalItem> =
        list().filter { it.favorite }.sortedWith(
            compareByDescending<LocalItem> { it.pinned }.thenByDescending { it.createdAt }
        )

    fun totalSize(): Long = list().sumOf { it.size }

    private fun readIndex(): List<LocalItem> {
        if (!index.isFile) return emptyList()
        return try {
            val arr = JSONArray(index.readText())
            (0 until arr.length()).map { i ->
                val o = arr.getJSONObject(i)
                LocalItem(
                    id = o.getString("id"),
                    title = o.optString("title", o.getString("id")),
                    isVideo = o.optBoolean("video", true),
                    path = o.getString("path"),
                    thumbPath = o.optString("thumb").ifBlank { null },
                    size = o.optLong("size", 0),
                    createdAt = o.optLong("createdAt", 0),
                    favorite = o.optBoolean("favorite", false),
                    pinned = o.optBoolean("pinned", false),
                    optimized = o.optBoolean("optimized", false),
                    sourceKey = o.optString("sourceKey").ifBlank { null },
                    kind = o.optString("kind").ifBlank {
                        if (o.optBoolean("optimized", false)) "wallpaper" else "import"
                    }
                )
            }
        } catch (e: Exception) {
            Log.e("DNXWallpaper", "readIndex", e)
            emptyList()
        }
    }

    private fun writeIndex(items: List<LocalItem>) {
        val arr = JSONArray()
        items.forEach { it ->
            arr.put(
                JSONObject()
                    .put("id", it.id)
                    .put("title", it.title)
                    .put("video", it.isVideo)
                    .put("path", it.path)
                    .put("thumb", it.thumbPath ?: "")
                    .put("size", it.size)
                    .put("createdAt", it.createdAt)
                    .put("favorite", it.favorite)
                    .put("pinned", it.pinned)
                    .put("optimized", it.optimized)
                    .put("sourceKey", it.sourceKey ?: "")
                    .put("kind", it.kind)
            )
        }
        index.writeText(arr.toString())
    }

    private fun upsert(item: LocalItem) {
        val all = readIndex().toMutableList()
        val i = all.indexOfFirst { it.id == item.id }
        if (i >= 0) all[i] = item else all.add(item)
        writeIndex(all)
    }

    // ------------------------------------------------------------ alta

    /** Copia un fichero local (SAF) al catálogo. */
    fun importUri(uri: Uri, title: String, isVideo: Boolean): LocalItem? {
        return try {
            val id = "loc" + System.currentTimeMillis().toString(36)
            val target = File(dir, id + if (isVideo) ".mp4" else ".img")
            val input = ctx.contentResolver.openInputStream(uri) ?: return null
            input.use { ins -> target.outputStream().use { ins.copyTo(it) } }
            if (target.length() <= 0) {
                target.delete()
                return null
            }
            finishAdd(id, target, title, isVideo, optimized = false, sourceKey = null, kind = "wallpaper")
        } catch (e: Exception) {
            Log.e("DNXWallpaper", "importUri", e)
            null
        }
    }

    /** Registra un fichero ya descargado (p. ej. una exportación del PC). */
    fun importFile(
        src: File,
        title: String,
        isVideo: Boolean,
        optimized: Boolean,
        sourceKey: String?
    ): LocalItem? {
        return try {
            val id = "loc" + System.currentTimeMillis().toString(36)
            val target = File(dir, id + if (isVideo) ".mp4" else ".img")
            src.copyTo(target, overwrite = true)
            finishAdd(id, target, title, isVideo, optimized, sourceKey)
        } catch (e: Exception) {
            Log.e("DNXWallpaper", "importFile", e)
            null
        }
    }

    private fun finishAdd(
        id: String,
        file: File,
        title: String,
        isVideo: Boolean,
        optimized: Boolean,
        sourceKey: String?,
        kind: String = "wallpaper"
    ): LocalItem {
        val thumb = makeThumb(id, file, isVideo)
        val item = LocalItem(
            id = id,
            title = title.ifBlank { id },
            isVideo = isVideo,
            path = file.absolutePath,
            thumbPath = thumb,
            size = file.length(),
            createdAt = System.currentTimeMillis(),
            favorite = false,
            pinned = false,
            optimized = optimized,
            sourceKey = sourceKey,
            kind = kind
        )
        upsert(item)
        return item
    }

    /**
     * Borra las copias de vídeos importados que nunca llegaron a prepararse
     * (el usuario abrió el editor y no generó nada). Nunca toca ficheros del
     * dispositivo: `filesDir/wallpapers` son copias de la app.
     */
    fun purgeImports() {
        val prefs = ctx.getSharedPreferences("dnxwallpaper", Context.MODE_PRIVATE)
        val current = prefs.getString("current_path", null)
        val all = readIndex()
        val imports = all.filter { it.kind == "import" && it.path != current }
        if (imports.isEmpty()) return
        imports.forEach {
            runCatching { File(it.path).delete() }
            it.thumbPath?.let { p -> runCatching { File(p).delete() } }
        }
        val ids = imports.map { it.id }.toSet()
        writeIndex(all.filter { it.id !in ids })
        Log.i("DNXWallpaper", "imports purgados: ${imports.size}")
    }

    /**
     * Como [importFile], pero si ya existe un item del mismo origen
     * (`sourceKey`) se actualiza en su sitio: así al volver a recortar un
     * wallpaper no aparecen dos entradas con el mismo nombre y la miniatura
     * pasa a ser la del nuevo recorte.
     */
    fun replaceOrImport(
        src: File,
        title: String,
        isVideo: Boolean,
        optimized: Boolean,
        sourceKey: String?
    ): LocalItem? {
        val prev = sourceKey?.let { key -> readIndex().firstOrNull { it.sourceKey == key } }
        if (prev == null) {
            // Recorte nuevo (no reemplaza a nada): se distingue en la lista.
            val name = if (optimized && title.isNotBlank()) "$title · recortado" else title
            return importFile(src, name, isVideo, optimized, sourceKey)
        }
        return try {
            val target = File(prev.path)
            src.copyTo(target, overwrite = true)
            // La miniatura se regenera desde el fichero nuevo (ya recortado).
            prev.thumbPath?.let { File(it).delete() }
            val thumb = makeThumb(prev.id, target, isVideo)
            val upd = prev.copy(
                title = title.ifBlank { prev.title },
                thumbPath = thumb,
                size = target.length(),
                optimized = optimized,
                createdAt = System.currentTimeMillis()
            )
            upsert(upd)
            upd
        } catch (e: Exception) {
            Log.e("DNXWallpaper", "replaceOrImport", e)
            null
        }
    }

    private fun makeThumb(id: String, file: File, isVideo: Boolean): String? {
        return try {
            var bmp: Bitmap? = null
            if (isVideo) {
                val r = MediaMetadataRetriever()
                r.setDataSource(file.absolutePath)
                bmp = r.getFrameAtTime(0)
                r.release()
            } else {
                bmp = BitmapFactory.decodeFile(file.absolutePath)
            }
            bmp ?: return null
            val w = 480
            val h = (bmp.height.toFloat() * w / bmp.width).toInt().coerceAtLeast(1)
            val small = Bitmap.createScaledBitmap(bmp, w, h, true)
            val out = File(dir, "$id.thumb.jpg")
            out.outputStream().use { small.compress(Bitmap.CompressFormat.JPEG, 82, it) }
            if (small !== bmp) small.recycle()
            bmp.recycle()
            out.absolutePath
        } catch (e: Exception) {
            Log.w("DNXWallpaper", "thumb", e)
            null
        }
    }

    // ------------------------------------------------------------ cambios

    fun toggleFavorite(item: LocalItem): LocalItem {
        val upd = item.copy(favorite = !item.favorite)
        upsert(upd)
        return upd
    }

    fun togglePinned(item: LocalItem): LocalItem {
        val upd = item.copy(pinned = !item.pinned)
        upsert(upd)
        return upd
    }

    fun delete(item: LocalItem) {
        try {
            // Si es el fondo en uso, el servicio lo tiene abierto: borrar el
            // fichero bajo ExoPlayer puede tumbar el proceso. Se quita del
            // catálogo y el fichero se conserva hasta que deje de estar activo.
            val prefs = ctx.getSharedPreferences("dnxwallpaper", Context.MODE_PRIVATE)
            val current = prefs.getString("current_path", null)
            if (item.path != current) {
                File(item.path).delete()
                item.thumbPath?.let { File(it).delete() }
            }
            val rest = readIndex().filter { it.id != item.id }
            writeIndex(rest)
        } catch (e: Exception) {
            Log.e("DNXWallpaper", "delete", e)
        }
    }

    /** Aplica el wallpaper (imagen o vídeo) y devuelve un mensaje. */
    fun apply(item: LocalItem): Pair<Boolean, Boolean> {
        val f = File(item.path)
        if (!f.isFile) return Pair(false, false)
        return if (item.isVideo) {
            val prefs = ctx.getSharedPreferences("dnxwallpaper", Context.MODE_PRIVATE)
            prefs.edit().putString("current_path", f.absolutePath).apply()
            val ok = prepareVideo(ctx, f)
            Pair(ok, true) // necesita selector de fondos animados
        } else {
            Pair(applyImage(ctx, f), false)
        }
    }
}
