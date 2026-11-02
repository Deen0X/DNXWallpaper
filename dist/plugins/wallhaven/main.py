#!/usr/bin/env python3
"""Wallhaven (v2, servidor). API oficial, solo SFW, sin API key."""

from __future__ import annotations

import re
import time
import urllib.parse
from pathlib import Path

import dnxlib as d

PLUGIN_ID = "wallhaven"
SITE = "https://wallhaven.cc"
API = "https://wallhaven.cc/api/v1"
PAGE_SIZE = 24
LIST_TTL = 10 * 60
DETAIL_TTL = 60 * 60
ASSET_TTL = 7 * 24 * 3600

CATEGORIES = [
    ("general", "General", "100"),
    ("anime", "Anime", "010"),
    ("people", "Personas", "001"),
]
CATEGORY_BITS = {item[0]: item[2] for item in CATEGORIES}
ID_RE = re.compile(r"^wh-[a-z0-9]{4,16}$")
ID_PART_RE = re.compile(r"^[a-z0-9]{4,16}$")
TAG_RE = re.compile(r"^[a-z0-9][a-z0-9._-]*$")

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


def _search_page(http, cache, query, page):
    url = "%s/search?%s&page=%d" % (API, query, page)
    key = _cache_key("search", url) + ".json"
    return cache.get_json(
        key, LIST_TTL,
        lambda: http.get_json(url),
    )


def _detail(http, cache, external_id):
    url = "%s/w/%s" % (API, external_id)
    key = _cache_key("detail", external_id) + ".json"
    data = cache.get_json(key, DETAIL_TTL, lambda: http.get_json(url))
    if not isinstance(data, dict) or not isinstance(data.get("data"), dict):
        raise d.PluginError("NOT_FOUND", "no existe el id '%s'" % external_id)
    return data["data"]


def _build_query(args, sort):
    parts = []
    text = (args.query or "").strip()
    tags = [tag.strip().lower() for tag in (args.tags or "").split(",") if tag.strip()]
    for tag in tags:
        if not TAG_RE.match(tag):
            raise d.PluginError("BAD_ARGS", "tag invalido: %s" % tag)
    q_parts = []
    if text:
        q_parts.append(text)
    q_parts.extend("+" + tag for tag in tags)

    category = (args.category or "").strip().lower()
    bits = "111"
    if category:
        if category not in CATEGORY_BITS:
            raise d.PluginError("NOT_FOUND", "categoria desconocida: %s" % category)
        bits = CATEGORY_BITS[category]

    if sort == "newest":
        sorting, order, top_range = "date_added", "desc", None
    elif sort == "popular":
        sorting, order, top_range = "favorites", "desc", None
    else:
        sorting, order, top_range = "relevance", "desc", None

    parts.append("purity=100")
    parts.append("categories=" + bits)
    if q_parts:
        parts.append("q=" + urllib.parse.quote(" ".join(q_parts), safe=""))
    parts.append("sorting=" + sorting)
    parts.append("order=" + order)
    if top_range:
        parts.append("topRange=" + top_range)
    return "&".join(parts), bool(q_parts)


def _to_item(data):
    external = str(data.get("id") or "")
    if not external:
        raise d.PluginError("PARSE", "wallhaven devolvio un item sin id")
    width = int(data.get("dimension_x") or 0)
    height = int(data.get("dimension_y") or 0)
    size = data.get("file_size")
    tags = []
    for tag in data.get("tags") or []:
        if isinstance(tag, dict) and tag.get("name"):
            tags.append(tag["name"])
    categories = []
    if data.get("category"):
        categories.append(data["category"])
    if width >= 3840 and "4k" not in categories:
        categories.append("4k")
    uploader = data.get("uploader") if isinstance(data.get("uploader"), dict) else {}
    author = None
    if uploader.get("username"):
        author = {
            "name": uploader["username"],
            "url": "%s/user/%s" % (SITE, uploader["username"]),
        }
    thumbs = data.get("thumbs") if isinstance(data.get("thumbs"), dict) else {}
    # "original" conserva la proporcion real; "large" viene recortado a 16:9.
    static_thumb = thumbs.get("original") or thumbs.get("large") or data.get("path")
    return d.make_item(
        "wh-" + external,
        data.get("id") or external,
        "image",
        data.get("path"),
        external_id=external,
        animated=False,
        width=width,
        height=height,
        size_bytes=size,
        published_at=data.get("created_at"),
        source_url="%s/w/%s" % (SITE, external),
        license="personal-use",
        attribution="Wallhaven.cc",
        tags=tags,
        categories=categories,
        static_thumb=static_thumb,
        static_cropped=not bool(thumbs.get("original")),
        content_type=data.get("file_type") or "image/jpeg",
        popularity=data.get("favorites"),
        author=author,
    )


def cmd_health(_args):
    started = time.perf_counter()
    return d.health_payload(started)


def cmd_categories(_args):
    return {
        "ok": True,
        "categories": [{"id": item[0], "name": item[1]} for item in CATEGORIES],
    }


def cmd_list(args):
    limit, requested = d.request_limit(args)
    offset = d.page_offset(args, limit)
    sort = d.sort_value(args)
    animated = d.parse_bool(args.animated, None)
    if animated is True:
        return d.list_response([], 0, offset, limit, requested, sorted_flag=True)

    if args.since or args.until:
        d.log("aviso: wallhaven no soporta filtros de fecha; se ignoran")
    if args.min_rating:
        d.log("aviso: wallhaven no publica rating; se ignora --min-rating")

    query, has_query = _build_query(args, sort)
    sorted_flag = sort != "relevance" or has_query

    http = _http()
    cache = _cache(args)
    api_page = offset // PAGE_SIZE + 1
    start = offset % PAGE_SIZE
    items = []
    total = None
    last_page = api_page

    while len(items) < limit and api_page <= last_page:
        payload = _search_page(http, cache, query, api_page)
        meta = payload.get("meta") if isinstance(payload, dict) else {}
        data = payload.get("data") if isinstance(payload, dict) else []
        if not isinstance(data, list):
            raise d.PluginError("PARSE", "wallhaven devolvio data invalida")
        if total is None:
            total = int((meta or {}).get("total") or 0)
            last_page = int((meta or {}).get("last_page") or api_page)
            if offset >= total:
                break
        for node in data[start:]:
            items.append(_to_item(node))
            if len(items) >= limit:
                break
        api_page += 1
        start = 0

    total = 0 if total is None else total
    return d.list_response(items, total, offset, limit, requested, sorted_flag=sorted_flag)


def cmd_detail(args):
    external = _external_id(args.id)
    data = _detail(_http(), _cache(args), external)
    return {"ok": True, "item": _to_item(data)}


def cmd_thumb(args):
    if args.kind not in ("static", "animated"):
        raise d.PluginError("BAD_ARGS", "--kind debe ser static|animated")
    external = _external_id(args.id)
    data = _detail(_http(), _cache(args), external)
    thumbs = data.get("thumbs") if isinstance(data.get("thumbs"), dict) else {}
    # El original conserva la proporcion; "large" esta recortado a 16:9.
    url = data.get("path") or thumbs.get("original") or thumbs.get("large")
    if not url:
        raise d.PluginError("NO_THUMB", "wallhaven no ofrece miniatura")
    try:
        result = d.save_thumbnail_url(
            _http(), url, args.out, size=args.size, cache=_cache(args),
            cache_key="thumb-%s-full" % external, ttl=ASSET_TTL,
        )
    except d.PluginError:
        fallback = thumbs.get("original")
        if not fallback or fallback == url:
            raise
        result = d.save_thumbnail_url(
            _http(), fallback, args.out, size=args.size, cache=_cache(args),
            cache_key="thumb-%s-original" % external, ttl=ASSET_TTL,
        )
    # wallhaven es estatico: --animated devuelve el estatico, no falla.
    return d.thumbnail_response(result)


def cmd_fetch(args):
    if args.kind not in ("media", "preview"):
        raise d.PluginError("BAD_ARGS", "--kind debe ser media|preview")
    if args.kind == "preview":
        raise d.PluginError("BAD_ARGS", "wallhaven es estatico; no hay preview en video")
    external = _external_id(args.id)
    data = _detail(_http(), _cache(args), external)
    url = data.get("path")
    if not url:
        raise d.PluginError("NOT_FOUND", "no hay media para '%s'" % external)
    result = _http().download(url, args.out, max_bytes=args.max_bytes, timeout=300)
    return d.fetch_response(
        result,
        width=data.get("dimension_x"),
        height=data.get("dimension_y"),
    )


def cmd_config(args):
    return d.config_command(args, [], PLUGIN_DIR)


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
