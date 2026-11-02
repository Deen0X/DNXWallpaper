#!/usr/bin/env python3
"""Wikimedia Commons (v2, servidor). API MediaWiki, sin API key."""

from __future__ import annotations

import re
import time
import urllib.parse
from pathlib import Path

import dnxlib as d

PLUGIN_ID = "wikimedia"
SITE = "https://commons.wikimedia.org"
API = "https://commons.wikimedia.org/w/api.php"
API_LIMIT = 50
MAX_OFFSET = 10000
LIST_TTL = 10 * 60
DETAIL_TTL = 60 * 60
ASSET_TTL = 7 * 24 * 3600
ANIMATED_MAX_BYTES = 20 * 1024 * 1024

CATEGORIES = [
    ("space", "Espacio", "space"),
    ("astronomy", "Astronomía", "astronomy"),
    ("earth", "Tierra", "earth"),
    ("nature", "Naturaleza", "nature"),
    ("landscape", "Paisajes", "landscape"),
    ("mountains", "Montañas", "mountains"),
    ("ocean", "Océano", "ocean"),
    ("animals", "Animales", "animals"),
    ("flowers", "Flores", "flowers"),
    ("city", "Ciudad", "city"),
    ("cars", "Coches", "cars"),
    ("abstract", "Abstracto", "abstract"),
    ("art", "Arte", "art"),
    ("nebula", "Nebulosa", "nebula"),
    ("galaxy", "Galaxia", "galaxy"),
]
CATEGORY_QUERY = {item[0]: item[2] for item in CATEGORIES}
ID_RE = re.compile(r"^wm-\d{1,20}$")
PLUGIN_DIR = Path(__file__).resolve().parent


def _http():
    return d.HttpClient(PLUGIN_ID, d.VERSION, SITE)


def _cache(args):
    return d.DiskCache(PLUGIN_DIR, args.cache_dir or None)


def _external_id(item_id):
    if not ID_RE.match(item_id or ""):
        raise d.PluginError("BAD_ARGS", "id invalido: %s" % item_id)
    return item_id[3:]


def _cache_key(prefix, value):
    return "%s-%s" % (prefix, value)


def _meta(metadata, key):
    if not isinstance(metadata, dict):
        return None
    node = metadata.get(key)
    if isinstance(node, dict):
        value = node.get("value")
    else:
        value = node
    if value is None:
        return None
    return d.clean_text(str(value))


def _title(page):
    raw = page.get("title") or ""
    if raw.lower().startswith("file:"):
        raw = raw[5:]
    raw = Path(raw).stem.replace("_", " ").strip()
    return raw or str(page.get("pageid") or "")


def _map_page(page, category_id=None):
    page_id = page.get("pageid")
    if page_id is None:
        return None
    infos = page.get("imageinfo")
    if not isinstance(infos, list) or not infos:
        return None
    info = infos[0] if isinstance(infos[0], dict) else {}
    mime = str(info.get("mime") or "")
    mediatype = str(info.get("mediatype") or "").upper()
    animated = mediatype == "VIDEO" or mime.startswith("video/")
    url = info.get("url")
    if not url:
        return None
    width = int(info.get("width") or 0)
    height = int(info.get("height") or 0)
    size = info.get("size")
    duration = info.get("duration")
    thumbnail = info.get("thumburl")
    if not thumbnail and not animated:
        thumbnail = url
    metadata = info.get("extmetadata") if isinstance(info.get("extmetadata"), dict) else {}
    artist = _meta(metadata, "Artist") or info.get("user") or "Wikimedia Commons"
    license_name = (
        _meta(metadata, "LicenseShortName")
        or _meta(metadata, "UsageTerms")
        or _meta(metadata, "License")
        or "unknown"
    )
    attribution = _meta(metadata, "Attribution") or _meta(metadata, "Credit")
    if not attribution:
        attribution = "%s · %s" % (artist, license_name)
    tags = []
    categories_text = _meta(metadata, "Categories")
    if categories_text:
        for tag in categories_text.split("|"):
            tag = tag.strip()
            if tag and tag not in tags:
                tags.append(tag)
            if len(tags) >= 20:
                break
    categories = []
    if category_id:
        categories.append(category_id)
    if width >= 3840 and "4k" not in categories:
        categories.append("4k")
    external = str(page_id)
    return d.make_item(
        "wm-" + external,
        _title(page),
        "video" if animated else "image",
        url,
        external_id=external,
        animated=animated,
        width=width,
        height=height,
        size_bytes=size,
        duration_sec=duration,
        published_at=info.get("timestamp"),
        source_url=info.get("descriptionurl") or "%s/wiki/File:%s" % (SITE, urllib.parse.quote(_title(page))),
        license=license_name,
        attribution=attribution,
        tags=tags,
        categories=categories,
        static_thumb=thumbnail,
        animated_thumb=url if animated else None,
        static_cropped=False,
        description=_meta(metadata, "ImageDescription"),
        content_type=mime or None,
        author={"name": artist} if artist else None,
    )


def _search_url(query, animated, offset, per_page, sort):
    file_filter = "filetype:video filemime:video/webm" if animated else "filetype:bitmap"
    search = ("%s %s" % (query, file_filter)).strip()
    props = "url|size|mime|extmetadata|user|timestamp|mediatype"
    params = [
        ("action", "query"),
        ("generator", "search"),
        ("gsrsearch", search),
        ("gsrnamespace", "6"),
        ("gsrlimit", str(per_page)),
        ("gsroffset", str(offset)),
        ("gsrinfo", "totalhits"),
    ]
    if sort == "newest":
        params.append(("gsrsort", "create_timestamp_desc"))
    elif sort == "relevance":
        params.append(("gsrsort", "relevance"))
    params.extend([
        ("prop", "imageinfo"),
        ("iiprop", props),
        ("iiurlwidth", "1280"),
        ("format", "json"),
        ("formatversion", "2"),
    ])
    return API + "?" + urllib.parse.urlencode(params)


def _search(http, cache, query, animated, offset, per_page, sort):
    url = _search_url(query, animated, offset, per_page, sort)
    key = _cache_key("search", url) + ".json"
    data = cache.get_json(key, LIST_TTL, lambda: http.get_json(url))
    pages = []
    query_data = data.get("query") if isinstance(data, dict) else {}
    raw_pages = (query_data or {}).get("pages") if isinstance(query_data, dict) else []
    if isinstance(raw_pages, list):
        pages = [page for page in raw_pages if isinstance(page, dict)]
        pages.sort(key=lambda page: int(page.get("index") or 0))
    total = int(((query_data or {}).get("searchinfo") or {}).get("totalhits") or 0)
    return pages, total


def _detail(http, cache, external):
    url = API + "?" + urllib.parse.urlencode({
        "action": "query",
        "pageids": external,
        "prop": "imageinfo",
        "iiprop": "url|size|mime|extmetadata|user|timestamp|mediatype",
        "iiurlwidth": "1280",
        "format": "json",
        "formatversion": "2",
    })
    key = _cache_key("detail", external) + ".json"
    data = cache.get_json(key, DETAIL_TTL, lambda: http.get_json(url))
    pages = ((data.get("query") or {}).get("pages") if isinstance(data, dict) else None) or []
    if not isinstance(pages, list) or not pages:
        raise d.PluginError("NOT_FOUND", "no existe el id '%s'" % external)
    item = _map_page(pages[0])
    if item is None:
        raise d.PluginError("NOT_FOUND", "no hay media para '%s'" % external)
    return item


def cmd_health(_args):
    started = time.perf_counter()
    return d.health_payload(started)


def cmd_categories(_args):
    return {
        "ok": True,
        "categories": [{"id": item[0], "name": item[1]} for item in CATEGORIES],
    }


def cmd_config(args):
    return d.config_command(args, [], PLUGIN_DIR)


def cmd_list(args):
    limit, requested = d.request_limit(args)
    offset = d.page_offset(args, limit)
    sort = d.sort_value(args)
    animated = d.parse_bool(args.animated, False)

    if args.since or args.until:
        d.log("aviso: Wikimedia no soporta filtros de fecha; se ignoran")
    if args.min_rating:
        d.log("aviso: Wikimedia no publica rating; se ignora --min-rating")

    terms = []
    if args.query and args.query.strip():
        terms.append(args.query.strip())
    terms.extend(tag.strip() for tag in (args.tags or "").split(",") if tag.strip())

    category_id = None
    category = (args.category or "").strip().lower()
    if category:
        if category not in CATEGORY_QUERY:
            raise d.PluginError("NOT_FOUND", "categoria desconocida: %s" % category)
        category_id = category
        terms.append(CATEGORY_QUERY[category])

    query = " ".join(terms) if terms else ("space" if animated else "landscape")
    sorted_flag = sort in ("newest", "relevance")

    http = _http()
    cache = _cache(args)
    if offset >= MAX_OFFSET:
        _, total = _search(http, cache, query, animated, 0, 1, sort)
        return d.list_response([], total, offset, limit, requested, sorted_flag=sorted_flag)

    items = []
    total = 0
    next_offset = offset
    api_offset = offset
    while len(items) < limit and api_offset < MAX_OFFSET:
        per_page = min(API_LIMIT, limit - len(items), MAX_OFFSET - api_offset)
        pages, page_total = _search(http, cache, query, animated, api_offset, per_page, sort)
        if not pages:
            break
        total = page_total
        for page in pages:
            item = _map_page(page, category_id)
            if item is None:
                continue
            items.append(item)
            if len(items) >= limit:
                break
        api_offset += len(pages)
        next_offset = api_offset
        if len(pages) < per_page:
            break

    return d.list_response(
        items, total, offset, limit, requested,
        next_offset=next_offset if len(items) >= limit else None,
        sorted_flag=sorted_flag,
    )


def cmd_detail(args):
    external = _external_id(args.id)
    return {"ok": True, "item": _detail(_http(), _cache(args), external)}


def cmd_thumb(args):
    if args.kind not in ("static", "animated"):
        raise d.PluginError("BAD_ARGS", "--kind debe ser static|animated")
    external = _external_id(args.id)
    item = _detail(_http(), _cache(args), external)
    animated_requested = args.animated or args.kind == "animated"

    if animated_requested and item.get("mediaType") == "video":
        size = int(item.get("sizeBytes") or 0)
        if not size or size <= ANIMATED_MAX_BYTES:
            result = d.save_animated_thumb(
                _http(), item["media"]["url"], args.out, size=args.size,
                cache=_cache(args), cache_key="anim-%s.webm" % external, ttl=ASSET_TTL,
            )
            if result:
                return d.thumbnail_response(animated_result=result)
        else:
            d.log("aviso: video > 20 MB; se devuelve la miniatura estatica")

    thumbnail = (item.get("thumbs") or {}).get("staticUrl") or (item.get("media") or {}).get("url")
    if not thumbnail:
        raise d.PluginError("NO_THUMB", "Wikimedia no ofrece miniatura")
    if item.get("mediaType") == "video" and not (item.get("thumbs") or {}).get("staticUrl"):
        result = d.save_frame_url(
            _http(), item["media"]["url"], args.out, size=args.size or 640,
            cache=_cache(args), cache_key="frame-%s.webm" % external, ttl=ASSET_TTL,
        )
        if result:
            return d.thumbnail_response(result)
    result = d.save_thumbnail_url(
        _http(), thumbnail, args.out, size=args.size,
        cache=_cache(args), cache_key="thumb-%s.jpg" % external, ttl=ASSET_TTL,
    )
    return d.thumbnail_response(result)


def cmd_fetch(args):
    if args.kind not in ("media", "preview"):
        raise d.PluginError("BAD_ARGS", "--kind debe ser media|preview")
    external = _external_id(args.id)
    item = _detail(_http(), _cache(args), external)
    if args.kind == "preview" and item.get("mediaType") != "video":
        raise d.PluginError("BAD_ARGS", "esta imagen no tiene preview en video")
    if args.kind == "preview" and int(item.get("sizeBytes") or 0) > ANIMATED_MAX_BYTES:
        raise d.PluginError("BAD_ARGS", "el video supera 20 MB; no se usa como preview")
    result = _http().download(
        item["media"]["url"], args.out, max_bytes=args.max_bytes, timeout=300,
    )
    return d.fetch_response(
        result,
        width=item.get("width"),
        height=item.get("height"),
        duration=item.get("durationSec"),
        probe=item.get("mediaType") == "video",
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
