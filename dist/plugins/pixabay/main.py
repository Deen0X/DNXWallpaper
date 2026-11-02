#!/usr/bin/env python3
"""Pixabay (v2, servidor). API oficial; requiere apiKey en config."""

from __future__ import annotations

import re
import time
import urllib.parse
from pathlib import Path

import dnxlib as d

PLUGIN_ID = "pixabay"
SITE = "https://pixabay.com"
API = "https://pixabay.com/api"
PAGE_SIZE_MAX = 200
LIST_TTL = 24 * 3600
ASSET_TTL = 7 * 24 * 3600

CATEGORIES = [
    ("backgrounds", "Fondos"),
    ("nature", "Naturaleza"),
    ("science", "Ciencia"),
    ("education", "Educación"),
    ("feelings", "Emociones"),
    ("health", "Salud"),
    ("people", "Personas"),
    ("religion", "Religión"),
    ("places", "Lugares"),
    ("animals", "Animales"),
    ("industry", "Industria"),
    ("computer", "Informática"),
    ("food", "Comida"),
    ("sports", "Deportes"),
    ("transportation", "Transporte"),
    ("travel", "Viajes"),
    ("buildings", "Edificios"),
    ("business", "Negocios"),
    ("music", "Música"),
    ("fashion", "Moda"),
]
CATEGORY_IDS = {item[0] for item in CATEGORIES}

CONFIG_FIELDS = [
    {
        "key": "apiKey",
        "label": "API key de Pixabay",
        "type": "secret",
        "required": True,
        "default": "",
        "help": "https://pixabay.com/api/docs/",
    }
]

ID_RE = re.compile(r"^px-(?P<kind>[vi])(?P<num>\d+)$")
PLUGIN_DIR = Path(__file__).resolve().parent


def _http():
    return d.HttpClient(PLUGIN_ID, d.VERSION, SITE)


def _cache(args):
    return d.DiskCache(PLUGIN_DIR, args.cache_dir or None)


def _api_key(args):
    values = d.config_values_with_defaults(d.load_config_values(args, PLUGIN_DIR), CONFIG_FIELDS)
    key = str(values.get("apiKey") or "").strip()
    if not key:
        d.config_required("apiKey", "falta la API key de Pixabay")
    return key


def _redact(exc, key):
    if key and key in exc.message:
        exc.message = exc.message.replace(key, "***")
        exc.args = (exc.message,)
    return exc


def _get_json(http, url, key):
    try:
        return http.get_json(url)
    except d.PluginError as exc:
        raise _redact(exc, key)


def _parse_id(value):
    match = ID_RE.match(value or "")
    if not match:
        raise d.PluginError("BAD_ARGS", "id invalido: %s" % value)
    return ("video" if match.group("kind") == "v" else "image", match.group("num"))


def _video_renditions(hit):
    videos = hit.get("videos") if isinstance(hit.get("videos"), dict) else {}
    result = []
    for name in ("large", "medium", "small", "tiny"):
        node = videos.get(name) if isinstance(videos.get(name), dict) else None
        if not node or not node.get("url"):
            continue
        try:
            width = int(node.get("width") or 0)
            height = int(node.get("height") or 0)
        except (TypeError, ValueError):
            width = height = 0
        result.append({
            "url": node["url"],
            "width": width,
            "height": height,
            "size": int(node.get("size") or 0),
            "thumbnail": node.get("thumbnail"),
        })
    return result


def _best_video(hit):
    renditions = _video_renditions(hit)
    return max(renditions, key=lambda item: item["width"] * item["height"], default=None)


def _smallest_video(hit):
    renditions = _video_renditions(hit)
    return min(renditions, key=lambda item: item["width"] * item["height"], default=None)


def _video_thumbnail(hit):
    videos = hit.get("videos") if isinstance(hit.get("videos"), dict) else {}
    for name in ("tiny", "small", "medium", "large"):
        node = videos.get(name) if isinstance(videos.get(name), dict) else None
        if node and node.get("thumbnail"):
            return node["thumbnail"]
    return None


def _first_url(hit, keys):
    for key in keys:
        value = hit.get(key)
        if isinstance(value, str) and value.strip():
            return value
    return None


def _best_image(hit):
    return _first_url(hit, ("imageURL", "fullHDURL", "largeImageURL", "webformatURL"))


def _image_thumbnail(hit):
    return _first_url(hit, ("webformatURL", "previewURL", "largeImageURL"))


def _content_type_from_url(url):
    if not url:
        return "application/octet-stream"
    suffix = Path(urllib.parse.urlparse(url).path).suffix.lower()
    return {
        ".jpg": "image/jpeg",
        ".jpeg": "image/jpeg",
        ".png": "image/png",
        ".webp": "image/webp",
        ".gif": "image/gif",
    }.get(suffix, "image/jpeg")


def _image_dimensions(hit, media_url):
    original_width = int(hit.get("imageWidth") or 0)
    original_height = int(hit.get("imageHeight") or 0)
    if original_width <= 0 or original_height <= 0 or not media_url:
        return None, None, None
    original = hit.get("imageURL")
    if original and media_url == original:
        return original_width, original_height, hit.get("imageSize")
    full_hd = hit.get("fullHDURL")
    large = hit.get("largeImageURL")
    if full_hd and media_url == full_hd:
        target = 1920
    elif large and media_url == large:
        target = 1280
    else:
        target = 640
    scale = float(target) / max(original_width, original_height)
    width = max(1, int(round(original_width * scale)))
    height = max(1, int(round(original_height * scale)))
    return width, height, None


def _split_tags(value):
    if not value:
        return []
    result = []
    for tag in str(value).split(","):
        text = tag.strip().lower()
        if text and text not in result:
            result.append(text)
    return result


def _map_item(hit, kind, category_id=None):
    numeric = str(hit.get("id") or "")
    if not numeric:
        raise d.PluginError("PARSE", "Pixabay devolvio un item sin id")
    item_id = "px-%s%s" % ("v" if kind == "video" else "i", numeric)
    external = "%s-%s" % (kind, numeric)
    tags = _split_tags(hit.get("tags"))
    title = ", ".join(tags[:3]) if tags else "Pixabay %s" % numeric
    user = hit.get("user")
    user_id = hit.get("user_id")
    author = None
    if user:
        author = {"name": user}
        if user_id:
            author["url"] = "%s/users/%s-%s/" % (SITE, user, user_id)
    categories = []
    if category_id:
        categories.append(category_id)

    if kind == "video":
        best = _best_video(hit)
        smallest = _smallest_video(hit)
        if not best:
            raise d.PluginError("PARSE", "Pixabay devolvio un video sin rendition")
        width, height, size = best["width"], best["height"], best["size"]
        duration = hit.get("duration")
        thumbnail = _video_thumbnail(hit)
        animated_thumb = smallest["url"] if smallest else None
        media_url = best["url"]
        content_type = "video/mp4"
    else:
        media_url = _best_image(hit)
        if not media_url:
            raise d.PluginError("PARSE", "Pixabay devolvio una imagen sin URL")
        width, height, size = _image_dimensions(hit, media_url)
        duration = None
        thumbnail = _image_thumbnail(hit)
        animated_thumb = None
        content_type = _content_type_from_url(media_url)

    if width and width >= 3840 and "4k" not in categories:
        categories.append("4k")

    # El poster de video de Pixabay suele ser 16:9. Si el video es vertical
    # o panoramico, se marca para que la app use/regenera la miniatura.
    static_cropped = kind == "video" and d.aspect_mismatch(width, height, 16, 9)
    return d.make_item(
        item_id,
        title,
        "video" if kind == "video" else "image",
        media_url,
        external_id=external,
        animated=(kind == "video"),
        width=width,
        height=height,
        size_bytes=size,
        duration_sec=duration,
        source_url=hit.get("pageURL"),
        license="pixabay-content-license",
        attribution="Pixabay · %s" % user if user else "Pixabay",
        tags=tags,
        categories=categories,
        static_thumb=thumbnail,
        animated_thumb=animated_thumb,
        static_cropped=static_cropped,
        content_type=content_type,
        popularity=hit.get("downloads"),
        author=author,
    )


def _build_filter(args, sort):
    parts = []
    q_parts = []
    if args.query and args.query.strip():
        q_parts.append(args.query.strip())
    q_parts.extend(tag.strip() for tag in (args.tags or "").split(",") if tag.strip())
    if q_parts:
        parts.append("q=" + urllib.parse.quote(" ".join(q_parts), safe=""))

    category = (args.category or "").strip().lower()
    if category:
        if category not in CATEGORY_IDS:
            raise d.PluginError("NOT_FOUND", "categoria desconocida: %s" % category)
        parts.append("category=" + category)

    if sort == "newest":
        order = "latest"
    elif sort == "popular":
        order = "popular"
    else:
        order = "latest"
    parts.append("order=" + order)
    parts.append("safesearch=true")
    return "&".join(parts)


def _search(http, key, kind, filter_value, page, per_page):
    path = "/videos/" if kind == "video" else "/"
    url = "%s%s?key=%s&%s&page=%d&per_page=%d" % (
        API, path, urllib.parse.quote(key, safe=""), filter_value, page, per_page,
    )
    data = _get_json(http, url, key)
    hits = data.get("hits") if isinstance(data, dict) else []
    if not isinstance(hits, list):
        raise d.PluginError("PARSE", "Pixabay devolvio hits invalidos")
    try:
        total = int(data.get("totalHits") or 0)
    except (TypeError, ValueError):
        total = 0
    return hits, total


def _get_by_id(http, key, kind, numeric):
    path = "/videos/" if kind == "video" else "/"
    url = "%s%s?key=%s&id=%s&safesearch=true" % (
        API, path, urllib.parse.quote(key, safe=""), numeric,
    )
    data = _get_json(http, url, key)
    hits = data.get("hits") if isinstance(data, dict) else []
    if not isinstance(hits, list) or not hits:
        raise d.PluginError("NOT_FOUND", "no existe el id '%s-%s'" % (kind, numeric))
    return hits[0]


def cmd_health(args):
    started = time.perf_counter()
    values = d.config_values_with_defaults(d.load_config_values(args, PLUGIN_DIR), CONFIG_FIELDS)
    configured = bool(str(values.get("apiKey") or "").strip())
    payload = d.health_payload(started, configured=configured, authRequired=True)
    if not configured:
        payload["warnings"] = ["sin apiKey: las llamadas a list/detail/fetch devolveran CONFIG_REQUIRED"]
    return payload


def cmd_categories(_args):
    return {
        "ok": True,
        "categories": [{"id": item[0], "name": item[1]} for item in CATEGORIES],
    }


def cmd_config(args):
    return d.config_command(args, CONFIG_FIELDS, PLUGIN_DIR)


def cmd_list(args):
    limit, requested = d.request_limit(args)
    offset = d.page_offset(args, limit)
    sort = d.sort_value(args)
    animated = d.parse_bool(args.animated, True)
    kind = "video" if animated else "image"

    if args.since or args.until:
        d.log("aviso: Pixabay no soporta filtros de fecha; se ignoran")
    if args.min_rating:
        d.log("aviso: Pixabay no publica rating; se ignora --min-rating")

    key = _api_key(args)
    filter_value = _build_filter(args, sort)
    sorted_flag = sort != "relevance"
    per_page = min(max(3, limit), PAGE_SIZE_MAX)
    page = offset // per_page + 1
    start = offset % per_page

    http = _http()
    hits, total = _search(http, key, kind, filter_value, page, per_page)
    items = []
    for hit in hits[start:]:
        items.append(_map_item(hit, kind, args.category or None))
        if len(items) >= limit:
            break

    return d.list_response(items, total, offset, limit, requested, sorted_flag=sorted_flag)


def cmd_detail(args):
    kind, numeric = _parse_id(args.id)
    key = _api_key(args)
    hit = _get_by_id(_http(), key, kind, numeric)
    return {"ok": True, "item": _map_item(hit, kind)}


def cmd_thumb(args):
    if args.kind not in ("static", "animated"):
        raise d.PluginError("BAD_ARGS", "--kind debe ser static|animated")
    kind, numeric = _parse_id(args.id)
    key = _api_key(args)
    hit = _get_by_id(_http(), key, kind, numeric)
    item = _map_item(hit, kind)
    animated = args.animated or args.kind == "animated"

    if kind == "video":
        smallest = _smallest_video(hit)
        if smallest:
            if animated:
                result = d.save_animated_thumb(
                    _http(), smallest["url"], args.out, size=args.size,
                    cache=_cache(args), cache_key="anim-%s.mp4" % numeric, ttl=ASSET_TTL,
                )
                if result:
                    return d.thumbnail_response(animated_result=result)
            # Fotograma completo del rendition pequeno (misma proporcion que
            # el video), no el poster 16:9 del proveedor.
            result = d.save_frame_url(
                _http(), smallest["url"], args.out, size=args.size or 640,
                cache=_cache(args), cache_key="frame-%s.mp4" % numeric, ttl=ASSET_TTL,
            )
            if result:
                return d.thumbnail_response(result)

    thumbnail = _image_thumbnail(hit) if kind == "image" else _video_thumbnail(hit)
    if not thumbnail:
        raise d.PluginError("NO_THUMB", "Pixabay no ofrece miniatura")
    result = d.save_thumbnail_url(
        _http(), thumbnail, args.out, size=args.size,
        cache=_cache(args), cache_key="thumb-%s-%s" % (kind, numeric), ttl=ASSET_TTL,
    )
    return d.thumbnail_response(result)


def cmd_fetch(args):
    if args.kind not in ("media", "preview"):
        raise d.PluginError("BAD_ARGS", "--kind debe ser media|preview")
    kind, numeric = _parse_id(args.id)
    key = _api_key(args)
    hit = _get_by_id(_http(), key, kind, numeric)

    if kind == "video":
        rendition = _smallest_video(hit) if args.kind == "preview" else _best_video(hit)
        if not rendition:
            raise d.PluginError("NOT_FOUND", "no hay %s para '%s-%s'" % (args.kind, kind, numeric))
        url = rendition["url"]
        width, height, duration = rendition["width"], rendition["height"], hit.get("duration")
        content_type = "video/mp4"
    else:
        url = _best_image(hit)
        if not url:
            raise d.PluginError("NOT_FOUND", "no hay media para '%s-%s'" % (kind, numeric))
        width, height, duration = _image_dimensions(hit, url)
        content_type = _content_type_from_url(url)

    result = _http().download(url, args.out, max_bytes=args.max_bytes, timeout=300)
    return d.fetch_response(
        result, width=width, height=height, duration=duration,
        probe=(kind == "video"),
    )


def main():
    return d.run({
        "health": cmd_health,
        "categories": cmd_categories,
        "list": cmd_list,
        "detail": cmd_detail,
        "thumb": cmd_thumb,
        "fetch": cmd_fetch,
        "config": cmd_config,
    })


if __name__ == "__main__":
    raise SystemExit(main())
