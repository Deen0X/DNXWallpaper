#!/usr/bin/env python3
"""MotionBGs (v2, servidor). Scraping de listados y detalle JSON-LD."""

from __future__ import annotations

import re
import time
import urllib.parse
from pathlib import Path

import dnxlib as d

PLUGIN_ID = "motionbgs"
SITE = "https://motionbgs.com"
LIST_TTL = 10 * 60
DETAIL_TTL = 60 * 60
ASSET_TTL = 7 * 24 * 3600

CATEGORIES = [
    ("anime", "Anime", "/tag:anime"),
    ("games", "Juegos", "/tag:games"),
    ("cars", "Coches", "/tag:car"),
    ("nature", "Naturaleza", "/tag:nature"),
    ("space", "Espacio", "/tag:space"),
    ("fantasy", "Fantasía", "/tag:fantasy"),
    ("animal", "Animales", "/tag:animal"),
    ("abstract", "Abstracto", "/tag:abstract"),
    ("4k", "4K", "/4k"),
]
CATEGORY_BASE = {item[0]: item[2] for item in CATEGORIES}
TAG_TO_CATEGORY = {
    "anime": "anime",
    "games": "games",
    "car": "cars",
    "nature": "nature",
    "space": "space",
    "fantasy": "fantasy",
    "animal": "animal",
    "abstract": "abstract",
}
SLUG_RE = re.compile(r"^mb-[a-z0-9][a-z0-9-]*$")
SLUG_PART_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
CARD_RE = re.compile(
    r'(?s)<a title="(?P<title>[^"]*)"\s+href=/(?P<slug>[^ >]+)>.*?'
    r'<span class=ttl>(?P<ttl>[^<]*)</span>\s*<span class=frm>\s*(?P<label>[^<]*)</span>'
)
THUMB_RE = re.compile(r'/i/c/(?:364x205|546x308)/media/(?P<id>\d+)/(?P<file>[^"\s>]+)')
VARIANT_RE = re.compile(
    r"<span class=font-bold>(?P<label>[^<]+)</span>\s*Wallpaper\s*\((?P<mb>[\d.]+)Mb\)</div><div class=text-xs>(?P<w>\d+)x(?P<h>\d+)",
    re.I,
)
TAG_LINK_RE = re.compile(
    r'href=/tag:(?P<slug>[^/>]+)/>\s*<picture>.*?<span>(?P<name>[^<]+)</span>',
    re.S | re.I,
)
BREADCRUMB_TAG_RE = re.compile(r"/tag:(?P<slug>[^/]+)/?")
ISO_DURATION_RE = re.compile(r"^PT(?:(?P<h>\d+)H)?(?:(?P<m>\d+)M)?(?:(?P<s>[\d.]+)S)?$", re.I)
CANONICAL_RE = re.compile(r'<link[^>]+href=([^ >]+)[^>]*rel=["\']?canonical', re.I)
ID_RE = re.compile(r"^mb-")

PLUGIN_DIR = Path(__file__).resolve().parent


def _http():
    return d.HttpClient(PLUGIN_ID, d.VERSION, SITE, browser_ua=True,
                        accept="text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8")


def _cache(args):
    return d.DiskCache(PLUGIN_DIR, args.cache_dir or None)


def _external_id(item_id):
    if not SLUG_RE.match(item_id or ""):
        raise d.PluginError("BAD_ARGS", "id invalido: %s" % item_id)
    return item_id[3:]


def _resolution(label):
    value = (label or "").strip().upper()
    mapping = {
        "8K": (7680, 4320),
        "4K": (3840, 2160),
        "2K": (2560, 1440),
        "HD": (1920, 1080),
        "1080P": (1920, 1080),
    }
    return mapping.get(value, (None, None))


def _variant_key(label):
    value = (label or "").strip().upper()
    return {"8K": "8k", "4K": "4k", "2K": "2k", "HD": "hd", "1080P": "hd"}.get(value)


def _parse_cards(html):
    cards = []
    for match in CARD_RE.finditer(html):
        slug = match.group("slug").strip("/")
        if not SLUG_PART_RE.match(slug):
            continue
        thumb = THUMB_RE.search(match.group(0))
        if not thumb:
            continue
        label = match.group("label").strip()
        width, height = _resolution(label)
        cards.append({
            "slug": slug,
            "title": d.clean_text(match.group("ttl")) or slug,
            "label": label,
            "width": width,
            "height": height,
            "media_id": thumb.group("id"),
            "thumb": SITE + thumb.group(0),
        })
    return cards


def _listing(args):
    query = (args.query or "").strip()
    category = (args.category or "").strip().lower()
    tags = [tag.strip().lower() for tag in (args.tags or "").split(",") if tag.strip()]

    selectors = int(bool(query)) + int(bool(category)) + int(bool(tags))
    if selectors > 1:
        raise d.PluginError("BAD_ARGS", "MotionBGs admite un solo selector: --query, --category o --tags")
    if category:
        if category not in CATEGORY_BASE:
            raise d.PluginError("NOT_FOUND", "categoria desconocida: %s" % category)
        return {"kind": "category", "key": "cat:" + category, "base": CATEGORY_BASE[category],
                "category": category, "tag": None, "query": None}
    if tags:
        if len(tags) > 1:
            raise d.PluginError("BAD_ARGS", "MotionBGs admite un solo tag")
        tag = tags[0]
        if not SLUG_PART_RE.match(tag):
            raise d.PluginError("BAD_ARGS", "tag invalido: %s" % tag)
        return {"kind": "tag", "key": "tag:" + tag, "base": "/tag:" + tag,
                "category": None, "tag": tag, "query": None}
    if query:
        return {"kind": "search", "key": "search:" + query, "base": "/search?q=" + urllib.parse.quote(query),
                "category": None, "tag": None, "query": query}
    return {"kind": "home", "key": "home", "base": "", "category": None, "tag": None, "query": None}


def _page_url(listing, page):
    if listing["kind"] == "search":
        if page <= 1:
            return SITE + listing["base"]
        return SITE + listing["base"] + "&page=%d" % page
    base = listing["base"]
    if page <= 1:
        return SITE + (base + "/" if base else "/")
    return SITE + base + "/%d/" % page


def _get_page(http, cache, listing, page):
    url = _page_url(listing, page)
    key = "list-%s-%d.html" % (listing["key"], page)
    return cache.get_text(key, LIST_TTL, lambda: http.get_text(url))


def _canonical_page(html):
    match = CANONICAL_RE.search(html)
    if not match:
        return None
    page = re.search(r"/([0-9]+)/?$", match.group(1))
    if page:
        return int(page.group(1))
    return None


def _total(http, cache, listing):
    try:
        last_html = _get_page(http, cache, listing, 999999)
    except d.PluginError:
        return None
    last_page = _canonical_page(last_html)
    if not last_page or last_page < 1:
        return None
    last_cards = _parse_cards(last_html)
    if last_page == 1:
        return len(last_cards)
    second_cards = _parse_cards(_get_page(http, cache, listing, 2))
    if not second_cards:
        return len(last_cards)
    return (last_page - 1) * len(second_cards) + len(last_cards)


def _fetch_cards(http, cache, listing, offset, limit):
    result = []
    first = _parse_cards(_get_page(http, cache, listing, 1))
    if not first:
        return result
    if offset < len(first):
        page = 1
        index = offset
        cards = first
    else:
        second = _parse_cards(_get_page(http, cache, listing, 2))
        if not second:
            return result
        relative = offset - len(first)
        page = 2 + relative // len(second)
        index = relative % len(second)
        cards = second if page == 2 else None

    while len(result) < limit and page <= 5000:
        if cards is None:
            cards = _parse_cards(_get_page(http, cache, listing, page))
        if not cards:
            break
        for card in cards[index:]:
            result.append(card)
            if len(result) >= limit:
                break
        page += 1
        index = 0
        cards = None
    return result


def _card_to_item(card, listing):
    key = _variant_key(card["label"]) or "hd"
    media = "%s/dl/%s/%s/" % (SITE, key, card["media_id"])
    categories = [listing["category"]] if listing.get("category") else []
    if card.get("width") and card["width"] >= 3840 and "4k" not in categories:
        categories.append("4k")
    tags = [listing["tag"]] if listing.get("tag") else []
    return d.make_item(
        "mb-" + card["slug"],
        card["title"],
        "video",
        media,
        external_id=card["slug"],
        animated=True,
        width=card.get("width"),
        height=card.get("height"),
        source_url="%s/%s" % (SITE, card["slug"]),
        license="personal-use",
        attribution="MotionBGs.com",
        tags=tags,
        categories=categories,
        static_thumb=card["thumb"],
        static_cropped=d.aspect_mismatch(card.get("width"), card.get("height"), 16, 9),
        content_type="video/mp4",
    )


def _detail(http, cache, slug):
    key = "detail-%s.html" % slug
    html = cache.get_text(key, DETAIL_TTL, lambda: http.get_text("%s/%s" % (SITE, slug)))
    video = d.find_json_ld(html, "VideoObject")
    if not video:
        raise d.PluginError("PARSE", "el detalle no contiene VideoObject")
    content = video.get("contentUrl")
    media_match = re.search(r"/media/(\d+)/", content or "")
    if not media_match:
        raise d.PluginError("PARSE", "no se pudo determinar el id de media")
    media_id = media_match.group(1)
    variants = []
    for match in VARIANT_RE.finditer(html):
        variant_key = _variant_key(match.group("label"))
        if not variant_key:
            continue
        try:
            width = int(match.group("w"))
            height = int(match.group("h"))
            size = int(float(match.group("mb").replace(",", ".")) * 1024 * 1024)
        except ValueError:
            continue
        variants.append({"key": variant_key, "width": width, "height": height, "size": size})
    best = max(variants, key=lambda item: item["width"] * item["height"], default=None)
    width = best["width"] if best else int(video.get("width") or 0)
    height = best["height"] if best else int(video.get("height") or 0)
    size = best["size"] if best else None
    duration = None
    match = ISO_DURATION_RE.match(str(video.get("duration") or "").strip())
    if match:
        seconds = 0
        if match.group("h"):
            seconds += int(match.group("h")) * 3600
        if match.group("m"):
            seconds += int(match.group("m")) * 60
        if match.group("s"):
            seconds += float(match.group("s"))
        duration = seconds or None
    tags = []
    cut = html.lower().find("<section class=dl")
    tag_html = html[:cut] if cut > 0 else html
    for tag in TAG_LINK_RE.finditer(tag_html):
        value = tag.group("slug").lower()
        if value not in tags:
            tags.append(value)
    if not tags:
        for tag in BREADCRUMB_TAG_RE.finditer(tag_html):
            value = tag.group("slug").lower()
            if value not in tags:
                tags.append(value)
    categories = []
    for tag in tags:
        category = TAG_TO_CATEGORY.get(tag)
        if category and category not in categories:
            categories.append(category)
    if width >= 3840 and "4k" not in categories:
        categories.append("4k")
    media = "%s/dl/%s/%s/" % (SITE, best["key"], media_id) if best else content
    thumbnail = video.get("thumbnailUrl")
    if isinstance(thumbnail, list):
        thumbnail = thumbnail[0] if thumbnail else None
    title = d.clean_text(video.get("name")) or slug
    title = re.sub(r"\s+live\s+wallpaper$", "", title, flags=re.I).strip() or slug
    item = d.make_item(
        "mb-" + slug,
        title,
        "video",
        media,
        external_id=slug,
        animated=True,
        width=width,
        height=height,
        size_bytes=size,
        duration_sec=duration,
        published_at=video.get("uploadDate"),
        source_url="%s/%s" % (SITE, slug),
        license="personal-use",
        attribution="MotionBGs.com",
        tags=tags,
        categories=categories,
        static_thumb=thumbnail,
        animated_thumb=content,
        static_cropped=d.aspect_mismatch(width, height, 16, 9),
        animated_cropped=d.aspect_mismatch(width, height, 16, 9),
        description=video.get("description"),
        content_type="video/mp4",
    )
    return item


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
    if args.since or args.until:
        d.log("aviso: MotionBGs no expone fecha en los listados; se ignoran --since/--until")
    if args.min_rating:
        d.log("aviso: la fuente no publica rating; se ignora --min-rating")

    listing = _listing(args)
    http = _http()
    cache = _cache(args)
    total = _total(http, cache, listing)
    cards = _fetch_cards(http, cache, listing, offset, limit)
    items = [_card_to_item(card, listing) for card in cards]
    next_offset = None
    if len(items) >= limit and (total is None or offset + len(items) < total):
        next_offset = offset + len(items)
    return d.list_response(
        items, total, offset, limit, requested,
        next_offset=next_offset,
        sorted_flag=(sort == "newest"),
    )


def cmd_detail(args):
    slug = _external_id(args.id)
    return {"ok": True, "item": _detail(_http(), _cache(args), slug)}


def cmd_thumb(args):
    if args.kind not in ("static", "animated"):
        raise d.PluginError("BAD_ARGS", "--kind debe ser static|animated")
    slug = _external_id(args.id)
    http = _http()
    cache = _cache(args)
    item = _detail(http, cache, slug)
    animated = args.animated or args.kind == "animated"
    preview = (item.get("thumbs") or {}).get("animatedUrl")
    media = (item.get("media") or {}).get("url")
    source = preview
    if d.aspect_mismatch(item.get("width"), item.get("height"), 16, 9) and media:
        source = media
    if animated and source:
        result = d.save_animated_thumb(
            http, source, args.out, size=args.size,
            cache=cache, cache_key="anim-%s.mp4" % slug, ttl=ASSET_TTL,
        )
        if result:
            return d.thumbnail_response(animated_result=result)
    # El contentUrl del detalle conserva la proporcion del video; el poster
    # 960x540 es solo respaldo.
    if source:
        result = d.save_frame_url(
            http, source, args.out, size=args.size or 640,
            cache=cache, cache_key="frame-%s.mp4" % slug, ttl=ASSET_TTL,
        )
        if result:
            return d.thumbnail_response(result)
    thumbnail = (item.get("thumbs") or {}).get("staticUrl")
    if not thumbnail:
        raise d.PluginError("NO_THUMB", "MotionBGs no ofrece miniatura")
    result = d.save_thumbnail_url(
        http, thumbnail, args.out, size=args.size,
        cache=cache, cache_key="thumb-%s.jpg" % slug, ttl=ASSET_TTL,
    )
    return d.thumbnail_response(result)


def cmd_fetch(args):
    if args.kind not in ("media", "preview"):
        raise d.PluginError("BAD_ARGS", "--kind debe ser media|preview")
    slug = _external_id(args.id)
    http = _http()
    item = _detail(http, _cache(args), slug)
    url = ((item.get("thumbs") or {}).get("animatedUrl") if args.kind == "preview"
           else (item.get("media") or {}).get("url"))
    if not url:
        raise d.PluginError("NOT_FOUND", "no hay %s para '%s'" % (args.kind, args.id))
    result = http.download(url, args.out, max_bytes=args.max_bytes, timeout=300)
    return d.fetch_response(
        result, width=item.get("width"), height=item.get("height"),
        duration=item.get("durationSec"), probe=True,
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
