#!/usr/bin/env python3
"""LivelyWallpaper.app (v2, servidor). Scraping del sitemap de vídeo."""

from __future__ import annotations

import html as _htmlmod
import re
import time
import urllib.parse
import xml.etree.ElementTree as ET
from pathlib import Path

import dnxlib as d

PLUGIN_ID = "lively"
SITE = "https://livelywallpaper.app"
SITEMAP = SITE + "/video-sitemap.xml"
LIST_TTL = 10 * 60
DETAIL_TTL = 60 * 60
ASSET_TTL = 7 * 24 * 3600
PAGE_SIZE = 36

CATEGORIES = [
    ("anime", "Anime"),
    ("games", "Juegos"),
    ("cars", "Coches"),
    ("nature", "Naturaleza"),
    ("space", "Espacio"),
    ("art-fantasy", "Arte y Fantasía"),
    ("4k", "4K"),
]
CATEGORY_IDS = {item[0] for item in CATEGORIES}
ID_RE = re.compile(r"^lv-[a-z0-9][a-z0-9-]*$")
SLUG_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
CARD_RE = re.compile(r'(?s)<article class="wallpaper-card"(?P<attrs>[^>]*)>(?P<body>.*?)</article>')

SITEMAP_NS = "http://www.sitemaps.org/schemas/sitemap/0.9"
VIDEO_NS = "http://www.google.com/schemas/sitemap-video/1.1"

PLUGIN_DIR = Path(__file__).resolve().parent


def _http():
    return d.HttpClient(PLUGIN_ID, d.VERSION, SITE, browser_ua=True,
                        accept="text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8")


def _cache(args):
    return d.DiskCache(PLUGIN_DIR, args.cache_dir or None)


def _external_id(item_id):
    if not ID_RE.match(item_id or ""):
        raise d.PluginError("BAD_ARGS", "id invalido: %s" % item_id)
    return item_id[3:]


def _text(node, path):
    found = node.find(path, {"s": SITEMAP_NS, "v": VIDEO_NS})
    return found.text if found is not None and found.text else None


def _parse_sitemap(xml):
    try:
        root = ET.fromstring(xml)
    except ET.ParseError as exc:
        raise d.PluginError("PARSE", "sitemap invalido: %s" % exc)
    entries = []
    seen = set()
    for url in root.findall("s:url", {"s": SITEMAP_NS}):
        loc = _text(url, "s:loc")
        video = url.find("v:video", {"v": VIDEO_NS})
        if not loc or video is None:
            continue
        slug = urllib.parse.urlparse(loc).path.rstrip("/").rsplit("/", 1)[-1]
        if not slug or slug in seen:
            continue
        seen.add(slug)
        content = _text(video, "v:content_loc")
        if not content:
            continue
        tags = []
        for tag in video.findall("v:tag", {"v": VIDEO_NS}):
            value = (tag.text or "").strip().lower()
            if value and value not in tags:
                tags.append(value)
        duration = None
        try:
            duration = int(float(_text(video, "v:duration") or 0))
        except ValueError:
            pass
        entries.append({
            "slug": slug,
            "title": d.clean_text(_text(video, "v:title") or slug),
            "description": d.clean_text(_text(video, "v:description")),
            "published": d.normalize_date(_text(video, "v:publication_date")),
            "duration": duration,
            "thumb": _text(video, "v:thumbnail_loc"),
            "media": content,
            "preview": content.replace("/download.mp4", "/preview.mp4"),
            "page": loc,
            "tags": tags,
            "categories": [],
            "width": None,
            "height": None,
            "size": None,
        })
    return entries


def _index(http, cache):
    xml = cache.get_text("video-sitemap.xml", LIST_TTL, lambda: http.get_text(SITEMAP)).encode("utf-8")
    entries = _parse_sitemap(xml)
    if not entries:
        raise d.PluginError("PARSE", "el sitemap no contiene videos")
    entries.sort(key=lambda item: item.get("published") or "", reverse=True)
    return entries


def _parse_size(value):
    if not value:
        return None
    match = re.search(r"([0-9]+(?:[.,][0-9]+)?)\s*(B|KB|MB|GB)", str(value), re.I)
    if not match:
        return None
    number = float(match.group(1).replace(",", "."))
    unit = match.group(2).upper()
    multiplier = {"B": 1, "KB": 1024, "MB": 1024 ** 2, "GB": 1024 ** 3}[unit]
    return int(number * multiplier)


def _resolution(label):
    value = (label or "").strip().upper()
    mapping = {
        "8K": (7680, 4320),
        "4K": (3840, 2160),
        "2K": (2560, 1440),
        "1080P": (1920, 1080),
        "HD": (1920, 1080),
    }
    if value in mapping:
        return mapping[value]
    match = re.search(r"([0-9]{3,5})\s*[×xX]\s*([0-9]{3,5})", label or "")
    if match:
        return int(match.group(1)), int(match.group(2))
    return None, None


def _parse_card(match):
    attributes = match.group("attrs")
    body = match.group("body")
    slug_match = re.search(r'class="wallpaper-card-media"\s+href="[^"]*/([a-z0-9-]+)/"', body, re.I)
    if not slug_match:
        return None
    slug = slug_match.group(1)
    title = d.clean_text(d.attr(attributes, "data-title"))
    description = d.clean_text(d.attr(attributes, "data-description"))
    category_text = d.attr(attributes, "data-category") or ""
    categories = [item.lower() for item in category_text.split() if item]
    width, height = _resolution(d.attr(attributes, "data-resolution"))
    size = _parse_size(d.attr(attributes, "data-size"))
    preview = d.attr(attributes, "data-preview-video")
    image_match = re.search(r"<img[^>]+src=\"([^\"]+)\"", body, re.I)
    thumb = _html_unescape(image_match.group(1)) if image_match else None
    media = preview.replace("/preview.mp4", "/download.mp4") if preview else None
    return {
        "slug": slug,
        "title": title or slug,
        "description": description,
        "page": "%s/live-wallpapers/%s/" % (SITE, slug),
        "categories": categories,
        "width": width,
        "height": height,
        "size": size,
        "thumb": thumb,
        "preview": preview,
        "media": media,
        "tags": [],
        "published": None,
        "duration": None,
    }


def _html_unescape(value):
    return _htmlmod.unescape(value)


def _category_page_url(category, page):
    if page <= 1:
        return "%s/live-wallpapers/%s/" % (SITE, category)
    return "%s/live-wallpapers/%s/%d/" % (SITE, category, page)


def _category_cards(http, cache, category):
    if category not in CATEGORY_IDS:
        raise d.PluginError("NOT_FOUND", "categoria desconocida: %s" % category)

    def get_page(page):
        url = _category_page_url(category, page)
        key = "cat-%s-%d.html" % (category, page)
        return cache.get_text(key, LIST_TTL, lambda: http.get_text(url))

    first = get_page(1)
    cards = [card for card in (_parse_card(match) for match in CARD_RE.finditer(first)) if card]
    if not cards:
        return cards, 0

    pages = [1]
    for link in re.finditer(r'href="[^"]*/live-wallpapers/%s/([0-9]+)/"' % re.escape(category), first):
        page = int(link.group(1))
        if page not in pages:
            pages.append(page)
    last_page = max(pages)
    if last_page > 1:
        first_count = len(cards)
        total = None
        for page in range(2, last_page + 1):
            page_cards = [card for card in (_parse_card(match) for match in CARD_RE.finditer(get_page(page))) if card]
            cards.extend(page_cards)
            if page == last_page:
                total = (last_page - 1) * first_count + len(page_cards)
    else:
        total = len(cards)

    # Completa los datos que solo estan en el sitemap.
    index = {entry["slug"]: entry for entry in _index(http, cache)}
    for card in cards:
        source = index.get(card["slug"])
        if source:
            card["description"] = card["description"] or source.get("description")
            card["published"] = source.get("published")
            card["duration"] = source.get("duration")
            card["tags"] = source.get("tags") or []
            card["media"] = card["media"] or source.get("media")
            card["preview"] = card["preview"] or source.get("preview")
            card["thumb"] = card["thumb"] or source.get("thumb")
    return cards, total


def _to_item(entry):
    slug = entry["slug"]
    width, height = entry.get("width"), entry.get("height")
    if not width and entry.get("resolution"):
        width, height = _resolution(entry["resolution"])
    media = entry.get("media")
    preview = entry.get("preview")
    if not media:
        raise d.PluginError("PARSE", "item sin media: %s" % slug)
    categories = [item for item in (entry.get("categories") or []) if item]
    if width and width >= 3840 and "4k" not in categories:
        categories.append("4k")
    return d.make_item(
        "lv-" + slug,
        entry.get("title") or slug,
        "video",
        media,
        external_id=slug,
        animated=True,
        width=width,
        height=height,
        size_bytes=entry.get("size"),
        duration_sec=entry.get("duration"),
        published_at=entry.get("published"),
        source_url=entry.get("page") or "%s/live-wallpapers/%s/" % (SITE, slug),
        license="personal-use",
        attribution="LivelyWallpaper.app (fan-made, no oficial)",
        tags=entry.get("tags"),
        categories=categories,
        static_thumb=entry.get("thumb"),
        animated_thumb=preview,
        static_cropped=False,
        description=entry.get("description"),
        content_type="video/mp4",
    )


def _matches(entry, query, tags, since, until):
    if since and (entry.get("published") or "") < since:
        return False
    if until and (entry.get("published") or "") > until:
        return False
    entry_tags = set(entry.get("tags") or [])
    for tag in tags:
        if tag not in entry_tags:
            return False
    if query:
        haystack = " ".join([
            entry.get("title") or "",
            entry.get("description") or "",
            " ".join(entry.get("tags") or []),
            " ".join(entry.get("categories") or []),
        ]).lower()
        if query.lower() not in haystack:
            return False
    return True


def cmd_health(_args):
    started = time.perf_counter()
    return d.health_payload(started)


def cmd_categories(_args):
    return {"ok": True, "categories": [{"id": item[0], "name": item[1]} for item in CATEGORIES]}


def cmd_config(args):
    return d.config_command(args, [], PLUGIN_DIR)


def cmd_list(args):
    limit, requested = d.request_limit(args)
    offset = d.page_offset(args, limit)
    animated = d.parse_bool(args.animated, True)
    sort = d.sort_value(args)
    if animated is False:
        return d.list_response([], 0, offset, limit, requested, sorted_flag=True)

    if args.min_rating:
        d.log("aviso: la fuente no publica rating; se ignora --min-rating")
    since = d.parse_date_filter(args.since) if args.since else None
    until = d.parse_date_filter(args.until) if args.until else None
    tags = [tag.strip().lower() for tag in (args.tags or "").split(",") if tag.strip()]
    category = (args.category or "").strip().lower()
    query = (args.query or "").strip()
    sorted_flag = sort == "newest"

    http = _http()
    cache = _cache(args)
    if category:
        cards, _ = _category_cards(http, cache, category)
        entries = [card for card in cards if _matches(card, query, tags, since, until)]
        if sort == "newest":
            entries.sort(key=lambda item: item.get("published") or "", reverse=True)
        total = len(entries)
    else:
        entries = [entry for entry in _index(http, cache) if _matches(entry, query, tags, since, until)]
        total = len(entries)

    window = entries[offset:offset + limit]
    items = [_to_item(entry) for entry in window]
    return d.list_response(items, total, offset, limit, requested, sorted_flag=sorted_flag)


def cmd_detail(args):
    slug = _external_id(args.id)
    http = _http()
    cache = _cache(args)
    for entry in _index(http, cache):
        if entry["slug"] == slug:
            return {"ok": True, "item": _to_item(entry)}
    raise d.PluginError("NOT_FOUND", "no existe el id '%s'" % args.id)


def cmd_thumb(args):
    if args.kind not in ("static", "animated"):
        raise d.PluginError("BAD_ARGS", "--kind debe ser static|animated")
    slug = _external_id(args.id)
    entry = None
    http = _http()
    cache = _cache(args)
    for item in _index(http, cache):
        if item["slug"] == slug:
            entry = item
            break
    if entry is None:
        raise d.PluginError("NOT_FOUND", "no existe el id '%s'" % args.id)

    animated = args.animated or args.kind == "animated"
    if animated and entry.get("preview"):
        result = d.save_animated_thumb(
            http, entry["preview"], args.out, size=args.size,
            cache=cache, cache_key="anim-%s.mp4" % slug, ttl=ASSET_TTL,
        )
        if result:
            return d.thumbnail_response(animated_result=result)

    # El preview conserva la proporcion del video; el thumb del sitemap puede
    # venir recortado. Se prefiere el fotograma completo.
    preview = entry.get("preview")
    if preview:
        result = d.save_frame_url(
            http, preview, args.out, size=args.size or 640,
            cache=cache, cache_key="frame-%s.mp4" % slug, ttl=ASSET_TTL,
        )
        if result:
            return d.thumbnail_response(result)
    thumbnail = entry.get("thumb")
    if not thumbnail:
        raise d.PluginError("NO_THUMB", "la fuente no ofrece miniatura")
    result = d.save_thumbnail_url(
        http, thumbnail, args.out, size=args.size,
        cache=cache, cache_key="thumb-%s.webp" % slug, ttl=ASSET_TTL,
    )
    return d.thumbnail_response(result)


def cmd_fetch(args):
    if args.kind not in ("media", "preview"):
        raise d.PluginError("BAD_ARGS", "--kind debe ser media|preview")
    slug = _external_id(args.id)
    http = _http()
    cache = _cache(args)
    entry = next((item for item in _index(http, cache) if item["slug"] == slug), None)
    if entry is None:
        raise d.PluginError("NOT_FOUND", "no existe el id '%s'" % args.id)
    url = entry.get("preview") if args.kind == "preview" else entry.get("media")
    if not url:
        raise d.PluginError("NOT_FOUND", "no hay %s para '%s'" % (args.kind, args.id))
    result = http.download(url, args.out, max_bytes=args.max_bytes, timeout=300)
    return d.fetch_response(
        result, duration=entry.get("duration"), probe=True,
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
