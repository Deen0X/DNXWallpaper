#!/usr/bin/env python3
"""WallpaperWaifu (v2, servidor). Scraping y token de descarga directa."""

from __future__ import annotations

import os
import re
import time
import urllib.parse
from pathlib import Path

import dnxlib as d

PLUGIN_ID = "wallpaperwaifu"
SITE = "https://wallpaperwaifu.com"
DOWNLOAD = "https://go.wallpaperwaifu.com/download.php?video="
LIST_TTL = 10 * 60
DETAIL_TTL = 60 * 60
ASSET_TTL = 7 * 24 * 3600
ENRICH_LIMIT = 12

CATEGORIES = [
    ("anime", "Anime"),
    ("fantasy", "Fantasía"),
    ("games", "Juegos"),
    ("vehicles", "Vehículos"),
    ("landscape", "Paisajes"),
    ("animals", "Animales"),
    ("abstract", "Abstracto"),
    ("sci-fi", "Ciencia ficción"),
    ("movies", "Películas"),
    ("lifestyle", "Estilo de vida"),
    ("pixel-art", "Pixel Art"),
    ("others", "Otros"),
]
CATEGORY_IDS = {item[0] for item in CATEGORIES}
ID_PARTS_RE = re.compile(r"^ww-(?P<category>[a-z0-9-]+)@(?P<slug>[a-z0-9-]+)$")
SLUG_RE = re.compile(r"^[a-z0-9-]+$")
ARTICLE_RE = re.compile(
    r'(?s)<article\s+data-id="(?P<post>[^"]+)"\s+class="(?P<classes>[^"]*)"[^>]*>(?P<body>.*?)</article>'
)
URL_RE = re.compile(r'href="https://wallpaperwaifu\.com/(?P<category>[a-z0-9-]+)/(?P<slug>[^"/]+)/"')
CAPTION_RE = re.compile(r'wall-caption">\s*(?P<title>[^<]+)\s*</div>')
ANCHOR_TITLE_RE = re.compile(r'title="(?P<title>[^"]+)"')
THUMB_RE = re.compile(r'(?:data-src|src)="(?P<thumb>https://wallpaperwaifu\.com/wp-content/uploads/[^"]+)"')
RES_CLASS_RE = re.compile(r"resolutions-(?P<res>[0-9]+x[0-9]+)")
TAG_CLASS_RE = re.compile(r"tag-(?P<tag>[a-z0-9-]+)")
PAGE_RE = re.compile(r"/page/(?P<page>[0-9]+)/")
H1_RE = re.compile(r"(?s)<h1[^>]*>(?P<title>.*?)</h1>")
OG_RE = re.compile(r'property="og:image"[^>]*content="(?P<url>[^"]+)"')
PUBLISHED_RE = re.compile(r'property="article:published_time"[^>]*content="(?P<date>[^"]+)"')
PREVIEW_RE = re.compile(r'(?P<url>/wp-content/uploads/preview/[^"\'\s]+\.(?:webm|mp4))', re.I)
TOKEN_RE = re.compile(r'(?s)id="wf-download"(?P<attrs>[^>]*)')
DATA_URL_RE = re.compile(r'data-url="(?P<token>[^"]+)"')
SIZE_RE = re.compile(r"(?s)File Size:.{0,400}?(?P<size>[0-9.,]+)\s*MB", re.I)
RES_TEXT_RE = re.compile(r"(?s)original resolution is.{0,400}?(?P<res>[0-9]{3,5}x[0-9]{3,5})", re.I)
TAG_LINK_RE = re.compile(r'href="https://wallpaperwaifu\.com/tag/(?P<tag>[^"/]+)/"')

PLUGIN_DIR = Path(__file__).resolve().parent


def _http():
    return d.HttpClient(PLUGIN_ID, d.VERSION, SITE, browser_ua=True,
                        accept="text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8")


def _cache(args):
    return d.DiskCache(PLUGIN_DIR, args.cache_dir or None)


def _parse_id(item_id):
    match = ID_PARTS_RE.match(item_id or "")
    if not match:
        raise d.PluginError("BAD_ARGS", "id invalido: %s" % item_id)
    return match.group("category"), match.group("slug")


def _preview_from_thumb(thumb):
    if not thumb:
        return None
    match = re.search(
        r"/wp-content/uploads/(?P<year>[0-9]{4})/[^/]+/(?P<base>.+?)-thumb(?:-[0-9]+x[0-9]+)?\.[a-z]+$",
        thumb, re.I,
    )
    if not match:
        return None
    return "%s/wp-content/uploads/preview/%s/%s-preview.webm" % (
        SITE, match.group("year"), match.group("base")
    )


def _parse_cards(html):
    cards = []
    seen = set()
    for article in ARTICLE_RE.finditer(html):
        body = article.group("body")
        classes = article.group("classes")
        url_match = URL_RE.search(body)
        if not url_match:
            continue
        thumb = THUMB_RE.search(body)
        if not thumb:
            continue
        caption = CAPTION_RE.search(body)
        title = d.clean_text(caption.group("title") if caption else None)
        if not title:
            anchor = ANCHOR_TITLE_RE.search(body)
            title = d.clean_text(anchor.group("title") if anchor else None)
        if not title:
            continue
        category = url_match.group("category")
        slug = url_match.group("slug")
        item_id = "%s/%s" % (category, slug)
        if item_id in seen:
            continue
        seen.add(item_id)
        width = height = None
        res_match = RES_CLASS_RE.search(classes)
        if res_match:
            dims = re.match(r"([0-9]+)x([0-9]+)", res_match.group("res"))
            if dims:
                width, height = int(dims.group(1)), int(dims.group(2))
        tags = []
        for tag in TAG_CLASS_RE.finditer(classes):
            value = tag.group("tag")
            if value not in tags:
                tags.append(value)
        thumb_url = d.clean_text(thumb.group("thumb"))
        cards.append({
            "id": item_id,
            "category": category,
            "slug": slug,
            "title": title,
            "page": "https://wallpaperwaifu.com/%s/" % item_id,
            "thumb": thumb_url,
            "preview": _preview_from_thumb(thumb_url),
            "width": width,
            "height": height,
            "tags": tags,
        })
    return cards


def _listing(args):
    query = (args.query or "").strip()
    category = (args.category or "").strip().lower()
    tags = [tag.strip().lower() for tag in (args.tags or "").split(",") if tag.strip()]
    selectors = int(bool(query)) + int(bool(category)) + int(bool(tags))
    if selectors > 1:
        raise d.PluginError("BAD_ARGS", "WallpaperWaifu admite un solo selector: --query, --category o --tags")
    if query:
        return {"kind": "search", "key": "search:" + query, "category": None, "tag": None, "query": query}
    if category:
        if category not in CATEGORY_IDS:
            raise d.PluginError("NOT_FOUND", "categoria desconocida: %s" % category)
        return {"kind": "category", "key": "cat:" + category, "category": category, "tag": None, "query": None}
    if tags:
        if len(tags) > 1:
            raise d.PluginError("BAD_ARGS", "WallpaperWaifu admite un solo tag")
        tag = tags[0]
        if not SLUG_RE.match(tag):
            raise d.PluginError("BAD_ARGS", "tag invalido: %s" % tag)
        return {"kind": "tag", "key": "tag:" + tag, "category": None, "tag": tag, "query": None}
    return {"kind": "home", "key": "home", "category": None, "tag": None, "query": None}


def _page_url(listing, page):
    if listing["kind"] == "category":
        base = "%s/category/%s/" % (SITE, listing["category"])
        return base if page <= 1 else base + "page/%d/" % page
    if listing["kind"] == "tag":
        base = "%s/tag/%s/" % (SITE, listing["tag"])
        return base if page <= 1 else base + "page/%d/" % page
    if listing["kind"] == "search":
        return ("%s/?s=%s" % (SITE, urllib.parse.quote(listing["query"]))) if page <= 1 else (
            "%s/page/%d/?s=%s" % (SITE, page, urllib.parse.quote(listing["query"])))
    return SITE + "/" if page <= 1 else "%s/page/%d/" % (SITE, page)


def _get_page(http, cache, listing, page):
    url = _page_url(listing, page)
    key = "list-%s-%d.html" % (listing["key"], page)
    return cache.get_text(key, LIST_TTL, lambda: http.get_text(url))


def _max_page(html):
    pages = [1]
    for match in PAGE_RE.finditer(html):
        try:
            pages.append(int(match.group("page")))
        except ValueError:
            pass
    return max(pages)


def _total(http, cache, listing):
    html = _get_page(http, cache, listing, 1)
    first = _parse_cards(html)
    if not first:
        return 0
    last_page = _max_page(html)
    if last_page <= 1:
        return len(first)
    last = _parse_cards(_get_page(http, cache, listing, last_page))
    return (last_page - 1) * len(first) + len(last)


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
    while len(result) < limit and page <= 2000:
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


def _card_item(card):
    preview = card.get("preview")
    categories = [card["category"]]
    if card.get("width") and card["width"] >= 3840 and "4k" not in categories:
        categories.append("4k")
    return d.make_item(
        "ww-%s@%s" % (card["category"], card["slug"]),
        card["title"],
        "video",
        preview or card["page"],
        external_id="%s/%s" % (card["category"], card["slug"]),
        animated=True,
        width=card.get("width"),
        height=card.get("height"),
        source_url=card["page"],
        license="personal-use",
        attribution="WallpaperWaifu.com",
        tags=card.get("tags"),
        categories=categories,
        static_thumb=card.get("thumb"),
        animated_thumb=preview,
        static_cropped=d.aspect_mismatch(card.get("width"), card.get("height"), 16, 9),
        content_type="video/webm" if preview else "text/html",
    )


def _detail(http, cache, category, slug):
    url = "%s/%s/%s/" % (SITE, category, slug)
    key = "detail-%s-%s.html" % (category, slug)
    html = cache.get_text(key, DETAIL_TTL, lambda: http.get_text(url)).replace("\\/", "/")
    h1 = H1_RE.search(html)
    title = d.clean_text(re.sub(r"<[^>]+>", "", h1.group("title"))) if h1 else slug
    title = title or slug
    published_match = PUBLISHED_RE.search(html)
    published = d.normalize_date(published_match.group("date")) if published_match else None
    og = OG_RE.search(html)
    thumbnail = d.clean_text(og.group("url")) if og else None
    preview_match = PREVIEW_RE.search(html)
    preview = None
    if preview_match:
        preview = preview_match.group("url")
        if preview.startswith("/"):
            preview = SITE + preview
    token_match = TOKEN_RE.search(html)
    data_match = DATA_URL_RE.search(token_match.group("attrs")) if token_match else None
    token = d.clean_text(data_match.group("token")) if data_match else None
    media = DOWNLOAD + token if token else None
    size_match = SIZE_RE.search(html)
    size = int(float(size_match.group("size").replace(",", ".")) * 1024 * 1024) if size_match else None
    res_match = RES_TEXT_RE.search(html)
    width = height = None
    if res_match:
        dims = re.match(r"([0-9]+)x([0-9]+)", res_match.group("res"))
        if dims:
            width, height = int(dims.group(1)), int(dims.group(2))
    tags = []
    for tag in TAG_LINK_RE.finditer(html):
        value = tag.group("tag").lower()
        if value not in tags:
            tags.append(value)
    categories = [category]
    if width and width >= 3840 and "4k" not in categories:
        categories.append("4k")
    if not media and not preview:
        raise d.PluginError("NOT_FOUND", "no hay media para '%s/%s'" % (category, slug))
    return d.make_item(
        "ww-%s@%s" % (category, slug),
        title,
        "video",
        media or preview,
        external_id="%s/%s" % (category, slug),
        animated=True,
        width=width,
        height=height,
        size_bytes=size,
        published_at=published,
        source_url=url,
        license="personal-use",
        attribution="WallpaperWaifu.com",
        tags=tags,
        categories=categories,
        static_thumb=thumbnail,
        animated_thumb=preview,
        static_cropped=d.aspect_mismatch(width, height, 16, 9),
        content_type="video/mp4" if media else "video/webm",
    )


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
        d.log("aviso: WallpaperWaifu no soporta filtros de fecha; se ignoran")
    if args.min_rating:
        d.log("aviso: la fuente no publica rating; se ignora --min-rating")

    listing = _listing(args)
    http = _http()
    cache = _cache(args)
    total = _total(http, cache, listing)
    cards = _fetch_cards(http, cache, listing, offset, limit)

    if limit <= ENRICH_LIMIT:
        details = d.parallel_map(
            lambda card: _detail(http, cache, card["category"], card["slug"]),
            cards, workers=4,
        )
        items = [item for item in details if item]
    else:
        items = [_card_item(card) for card in cards]

    return d.list_response(
        items, total, offset, limit, requested,
        next_offset=offset + len(items) if offset + len(items) < total else None,
        sorted_flag=(sort == "newest"),
    )


def cmd_detail(args):
    category, slug = _parse_id(args.id)
    return {"ok": True, "item": _detail(_http(), _cache(args), category, slug)}


def cmd_thumb(args):
    if args.kind not in ("static", "animated"):
        raise d.PluginError("BAD_ARGS", "--kind debe ser static|animated")
    category, slug = _parse_id(args.id)
    http = _http()
    cache = _cache(args)
    item = _detail(http, cache, category, slug)
    animated = args.animated or args.kind == "animated"
    if animated:
        preview = (item.get("thumbs") or {}).get("animatedUrl")
        if preview:
            result = d.save_animated_thumb(
                http, preview, args.out, size=args.size,
                cache=cache, cache_key="anim-%s-%s.webm" % (category, slug), ttl=ASSET_TTL,
            )
            if result:
                return d.thumbnail_response(animated_result=result)
    # El preview WebM conserva la proporcion original; og:image es 16:9.
    preview = (item.get("thumbs") or {}).get("animatedUrl")
    if preview:
        result = d.save_frame_url(
            http, preview, args.out, size=args.size or 640,
            cache=cache, cache_key="frame-%s-%s.webm" % (category, slug), ttl=ASSET_TTL,
        )
        if result:
            return d.thumbnail_response(result)
    thumbnail = (item.get("thumbs") or {}).get("staticUrl")
    if not thumbnail:
        raise d.PluginError("NO_THUMB", "WallpaperWaifu no ofrece miniatura")
    result = d.save_thumbnail_url(
        http, thumbnail, args.out, size=args.size,
        cache=cache, cache_key="thumb-%s-%s.jpg" % (category, slug), ttl=ASSET_TTL,
    )
    return d.thumbnail_response(result)


def cmd_fetch(args):
    if args.kind not in ("media", "preview"):
        raise d.PluginError("BAD_ARGS", "--kind debe ser media|preview")
    category, slug = _parse_id(args.id)
    http = _http()
    item = _detail(http, _cache(args), category, slug)
    preview = (item.get("thumbs") or {}).get("animatedUrl")
    url = preview if args.kind == "preview" else (item.get("media") or {}).get("url")
    if not url:
        raise d.PluginError("NOT_FOUND", "no hay %s para '%s'" % (args.kind, args.id))
    referer = item.get("sourceUrl")
    try:
        result = http.download(url, args.out, max_bytes=args.max_bytes, timeout=300, referer=referer)
        if (result.get("contentType") or "").startswith("text/"):
            raise d.PluginError("NETWORK", "el proveedor devolvio HTML en vez de video")
    except d.PluginError as exc:
        if args.kind == "media" and preview and exc.code == "NETWORK":
            d.log("aviso: fallo el MP4 (%s); se usa el preview WebM" % exc.message)
            result = http.download(preview, args.out, max_bytes=args.max_bytes, timeout=300, referer=referer)
        else:
            try:
                if os.path.exists(args.out):
                    os.remove(args.out)
            except OSError:
                pass
            raise
    if result.get("contentType") in (None, "", "application/octet-stream"):
        result["contentType"] = "video/mp4" if args.kind == "media" else "video/webm"
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

