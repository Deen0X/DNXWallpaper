package io.dnx.wallpaper.companion

import android.app.DownloadManager
import android.app.WallpaperManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.graphics.BitmapFactory
import android.net.Uri
import android.os.Environment
import android.util.Log
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import org.json.JSONArray
import org.json.JSONObject

// ---------------------------------------------------------------- modelos

data class Item(
    val base: String,
    val key: String,
    val title: String,
    val isVideo: Boolean,
    val width: Int,
    val height: Int,
    val size: Long,
    val thumbUrl: String,
    val fileUrl: String
) {
    val ext: String get() = if (isVideo) ".mp4" else ".jpg"
    val fileName: String get() = title.replace(Regex("[\\\\/:*?\"<>|]"), "_") + ext
    val isPortrait: Boolean get() = height >= width
}

data class LibItem(
    val key: String,
    val title: String,
    val isVideo: Boolean,
    val width: Int,
    val height: Int,
    val thumbUrl: String,
    val provider: String = ""
) {
    // Sin dimensiones no se puede afirmar que sea vertical (antes, 0>=0 daba
    // true para todo y el filtro no filtraba nada).
    val isPortrait: Boolean get() = width > 0 && height > 0 && height > width
}

data class LoadResult(val item: Item?, val error: String?)
data class DownloadResult(val file: File?, val error: String?)
data class FoundPc(val base: String, val name: String, val error: String?)

// ---------------------------------------------------------------- red

/** De una URL /dl/<key> o /m/<id> obtiene los metadatos del PC. */
fun loadItem(u: String): LoadResult {
    return try {
        val uri = URL(u)
        val base = "${uri.protocol}://${uri.host}:${uri.port}"
        val path = uri.path ?: ""
        val isMobile = path.contains("/m/")
        val dlIdx = path.indexOf("/dl/")
        val mIdx = path.indexOf("/m/")
        if (!isMobile && dlIdx < 0) {
            LoadResult(null, "URL no válida")
        } else {
            val id = if (isMobile) {
                path.substring(mIdx + 3).trim('/')
            } else {
                java.net.URLDecoder.decode(path.substring(dlIdx + 4), "UTF-8")
            }
            val endpoint = if (isMobile) {
                "$base/api/mobile/" + URLEncoder.encode(id, "UTF-8")
            } else {
                "$base/api/item/" + URLEncoder.encode(id, "UTF-8")
            }
            val conn = (URL(endpoint).openConnection() as HttpURLConnection)
            conn.connectTimeout = 6000
            conn.readTimeout = 8000
            val code = conn.responseCode
            if (code !in 200..299) {
                val detail = try {
                    conn.errorStream?.bufferedReader()?.use { it.readText() } ?: ""
                } catch (_: Exception) {
                    ""
                }
                conn.disconnect()
                return LoadResult(null, "HTTP $code ${detail.take(80)}")
            }
            val text = conn.inputStream.bufferedReader().use { it.readText() }
            conn.disconnect()
            val j = JSONObject(text)
            val isVideo = j.optString("type", "video") == "video"
            val media = if (isMobile) {
                // La exportación ya está optimizada: no se pide proxy ?m=1.
                base + j.optString("url", "/m/" + id)
            } else {
                base + j.optString("url", "") + if (isVideo) "?m=1" else ""
            }
            LoadResult(
                Item(
                    base = base,
                    key = id,
                    title = j.optString("title", id),
                    isVideo = isVideo,
                    width = j.optInt("width", 0),
                    height = j.optInt("height", 0),
                    size = j.optLong("size", 0),
                    thumbUrl = base + j.optString("thumb", ""),
                    fileUrl = media
                ),
                null
            )
        }
    } catch (e: Exception) {
        Log.e("DNXWallpaper", "loadItem $u", e)
        LoadResult(null, e.message ?: e.javaClass.simpleName)
    }
}

/** Página de la biblioteca del PC (/api/library). */
fun loadLibrary(base: String, offset: Int, limit: Int, q: String = ""): Pair<List<LibItem>, String?> {
    return try {
        var u = "$base/api/library?offset=$offset&limit=$limit"
        if (q.isNotBlank()) u += "&q=" + URLEncoder.encode(q, "UTF-8")
        val conn = (URL(u).openConnection() as HttpURLConnection)
        conn.connectTimeout = 6000
        conn.readTimeout = 15000
        val code = conn.responseCode
        if (code !in 200..299) {
            conn.disconnect()
            return Pair(emptyList(), "HTTP $code")
        }
        val text = conn.inputStream.bufferedReader().use { it.readText() }
        conn.disconnect()
        Pair(parseLibrary(base, text), null)
    } catch (e: Exception) {
        Log.e("DNXWallpaper", "loadLibrary", e)
        Pair(emptyList(), e.message ?: e.javaClass.simpleName)
    }
}

/** Búsqueda en vivo: el PC pregunta a los plugins por el término. */
fun searchPlugins(base: String, q: String): Pair<List<LibItem>, String?> {
    return try {
        val u = "$base/api/search?q=" + URLEncoder.encode(q, "UTF-8")
        val conn = (URL(u).openConnection() as HttpURLConnection)
        conn.connectTimeout = 8000
        // Cada plugin consulta a su proveedor: puede tardar.
        conn.readTimeout = 150000
        val code = conn.responseCode
        if (code !in 200..299) {
            conn.disconnect()
            return Pair(emptyList(), "HTTP $code")
        }
        val text = conn.inputStream.bufferedReader().use { it.readText() }
        conn.disconnect()
        Pair(parseLibrary(base, text), null)
    } catch (e: Exception) {
        Log.e("DNXWallpaper", "searchPlugins", e)
        Pair(emptyList(), e.message ?: e.javaClass.simpleName)
    }
}

/** Barajado: el PC recarga cada plugin en una página distinta. */
fun randomizeLibrary(base: String): Pair<List<LibItem>, String?> {
    return try {
        val conn = (URL("$base/api/library/randomize").openConnection() as HttpURLConnection)
        conn.connectTimeout = 8000
        // Tarda: cada plugin arranca su proceso y pide una página nueva.
        conn.readTimeout = 120000
        val code = conn.responseCode
        if (code !in 200..299) {
            conn.disconnect()
            return Pair(emptyList(), "HTTP $code")
        }
        val text = conn.inputStream.bufferedReader().use { it.readText() }
        conn.disconnect()
        Pair(parseLibrary(base, text), null)
    } catch (e: Exception) {
        Log.e("DNXWallpaper", "randomizeLibrary", e)
        Pair(emptyList(), e.message ?: e.javaClass.simpleName)
    }
}

private fun parseLibrary(base: String, text: String): List<LibItem> {
    val arr = JSONArray(text)
    val out = ArrayList<LibItem>(arr.length())
    for (i in 0 until arr.length()) {
        val o = arr.getJSONObject(i)
        val key = o.optString("workshopid", "")
        if (key.isEmpty()) continue
        val res = o.optJSONObject("resolution")
        out.add(
            LibItem(
                key = key,
                title = o.optString("title", key).replace("  [Animado]", ""),
                isVideo = o.optString("type", "Video") != "Image",
                width = res?.optInt("width", 0) ?: 0,
                height = res?.optInt("height", 0) ?: 0,
                thumbUrl = base + "/api/thumb/" + URLEncoder.encode(key, "UTF-8"),
                provider = o.optString("provider", "")
            )
        )
    }
    return out
}

/** Busca el PC en la LAN: UDP 7884 con "WPX/Ping". */
fun discoverPc(timeoutMs: Int = 3000): FoundPc {
    var socket: java.net.DatagramSocket? = null
    return try {
        socket = java.net.DatagramSocket()
        socket.broadcast = true
        socket.soTimeout = 700
        val msg = "WPX/Ping".toByteArray()
        val bcast = java.net.InetAddress.getByName("255.255.255.255")
        socket.send(java.net.DatagramPacket(msg, msg.size, bcast, 7884))

        val deadline = System.currentTimeMillis() + timeoutMs
        val buf = ByteArray(1024)
        var found: FoundPc? = null
        while (System.currentTimeMillis() < deadline && found == null) {
            val pkt = java.net.DatagramPacket(buf, buf.size)
            try {
                socket.receive(pkt)
            } catch (_: java.net.SocketTimeoutException) {
                socket.send(java.net.DatagramPacket(msg, msg.size, bcast, 7884))
                continue
            }
            val text = String(pkt.data, 0, pkt.length, Charsets.UTF_8)
            val ip = pkt.address?.hostAddress ?: continue
            val j = runCatching { JSONObject(text) }.getOrNull() ?: continue
            found = FoundPc(
                "http://$ip:${j.optInt("port", 8477)}",
                j.optString("name", ip),
                null
            )
        }
        found ?: FoundPc("", "", "No se encontró ningún PC en la red")
    } catch (e: Exception) {
        Log.e("DNXWallpaper", "discoverPc", e)
        FoundPc("", "", e.message ?: e.javaClass.simpleName)
    } finally {
        try {
            socket?.close()
        } catch (_: Exception) {
        }
    }
}

/** Descarga el fichero a filesDir con progreso (0..1). */
fun download(ctx: Context, m: Item, onProgress: (Float) -> Unit): DownloadResult {
    return try {
        val target = File(ctx.filesDir, if (m.isVideo) "current.mp4" else "current.img")
        val tmp = File(ctx.filesDir, "dl.tmp")
        val conn = (URL(m.fileUrl).openConnection() as HttpURLConnection)
        conn.connectTimeout = 10000
        conn.readTimeout = 300000
        conn.requestMethod = "GET"
        val code = conn.responseCode
        if (code !in 200..299) {
            val detail = try {
                conn.errorStream?.bufferedReader()?.use { it.readText() } ?: ""
            } catch (_: Exception) {
                ""
            }
            conn.disconnect()
            return DownloadResult(null, "HTTP $code ${detail.take(120)}")
        }
        val total = conn.contentLengthLong
        conn.inputStream.use { input ->
            tmp.outputStream().use { out ->
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
        if (tmp.length() <= 0) {
            tmp.delete()
            DownloadResult(null, "fichero vacío")
        } else {
            if (target.exists()) target.delete()
            if (!tmp.renameTo(target)) {
                tmp.copyTo(target, overwrite = true)
                tmp.delete()
            }
            if (target.length() > 0) DownloadResult(target, null)
            else DownloadResult(null, "no se pudo mover el fichero")
        }
    } catch (e: Exception) {
        Log.e("DNXWallpaper", "download ${m.fileUrl}", e)
        DownloadResult(null, e.message ?: e.javaClass.simpleName)
    }
}

/** Copia un vídeo elegido con SAF a filesDir/current.mp4 (con progreso). */
fun copyLocalVideo(ctx: Context, uri: Uri, onProgress: (Float) -> Unit): File? {
    return try {
        val resolver = ctx.contentResolver
        val total = try {
            resolver.openAssetFileDescriptor(uri, "r")?.use { it.length } ?: -1L
        } catch (_: Exception) {
            -1L
        }
        val target = File(ctx.filesDir, "current.mp4")
        val tmp = File(ctx.filesDir, "local.tmp")
        val input = resolver.openInputStream(uri) ?: return null
        input.use { ins ->
            tmp.outputStream().use { out ->
                val buf = ByteArray(128 * 1024)
                var read: Int
                var done = 0L
                while (ins.read(buf).also { read = it } > 0) {
                    out.write(buf, 0, read)
                    done += read
                    if (total > 0) onProgress((done.toFloat() / total).coerceIn(0f, 1f))
                }
            }
        }
        if (tmp.length() <= 0) {
            tmp.delete()
            null
        } else {
            if (target.exists()) target.delete()
            if (!tmp.renameTo(target)) {
                tmp.copyTo(target, overwrite = true)
                tmp.delete()
            }
            if (target.length() > 0) target else null
        }
    } catch (e: Exception) {
        Log.e("DNXWallpaper", "copyLocalVideo", e)
        null
    }
}

fun applyImage(ctx: Context, f: File): Boolean = try {
    val wm = WallpaperManager.getInstance(ctx)
    if (!wm.isWallpaperSupported || !wm.isSetWallpaperAllowed) {
        false
    } else {
        f.inputStream().use { wm.setStream(it) }
        true
    }
} catch (e: Exception) {
    Log.e("DNXWallpaper", "applyImage", e)
    false
}

fun prepareVideo(ctx: Context, f: File): Boolean = f.length() > 0

fun launchLiveWallpaper(ctx: Context): Boolean {
    return try {
        val i = Intent(WallpaperManager.ACTION_CHANGE_LIVE_WALLPAPER).apply {
            putExtra(
                WallpaperManager.EXTRA_LIVE_WALLPAPER_COMPONENT,
                ComponentName(ctx, VideoWallpaperService::class.java)
            )
        }
        // No usar resolveActivity: Android 11+ oculta el selector del sistema
        // por package visibility y da falsos negativos. Se intenta y se captura.
        ctx.startActivity(i)
        true
    } catch (e: Exception) {
        Log.e("DNXWallpaper", "launchLiveWallpaper", e)
        false
    }
}

fun enqueueDownload(ctx: Context, m: Item) {
    val req = DownloadManager.Request(Uri.parse(m.fileUrl))
    req.setTitle(m.fileName)
    req.setDescription("DNX Wallpaper")
    req.setNotificationVisibility(
        DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED
    )
    req.setDestinationInExternalPublicDir(Environment.DIRECTORY_DOWNLOADS, m.fileName)
    (ctx.getSystemService(Context.DOWNLOAD_SERVICE) as DownloadManager).enqueue(req)
}

fun humanSize(b: Long): String = when {
    b >= 1_048_576 -> String.format("%.1f MB", b / 1_048_576.0)
    b >= 1024 -> String.format("%.0f KB", b / 1024.0)
    else -> "$b B"
}

fun decodeBitmap(u: String): android.graphics.Bitmap? = try {
    URL(u).openStream().use { BitmapFactory.decodeStream(it) }
} catch (_: Exception) {
    null
}

/** Nombre visible de un fichero elegido con SAF (sin extensión). */
fun uriDisplayName(ctx: Context, uri: Uri): String {
    return try {
        ctx.contentResolver.query(uri, null, null, null, null)?.use { c ->
            val i = c.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
            if (i >= 0 && c.moveToFirst()) {
                val n = c.getString(i) ?: ""
                n.substringBeforeLast('.').ifBlank { "Wallpaper local" }
            } else {
                "Wallpaper local"
            }
        } ?: "Wallpaper local"
    } catch (_: Exception) {
        "Wallpaper local"
    }
}
