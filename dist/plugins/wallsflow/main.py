#!/usr/bin/env python3
"""Wallsflow (v2, servidor). Scraping de listados DLE y JSON-LD del detalle."""

from __future__ import annotations

import re
import time
import urllib.parse
from pathlib import Path

import dnxlib as d

PLUGIN_ID = "wallsflow"
SITE = "https://wallsflow.com"
LIST_TTL = 10 * 60
DETAIL_TTL = 60 * 60
ASSET_TTL = 7 * 24 * 3600
PAGE_SIZE = 12
ENRICH_LIMIT = 12

CATEGORIES = [
    ("anime", "Anime"),
    ("games", "Juegos"),
    ("cars", "Coches"),
    ("nature", "Naturaleza"),
    ("space", "Espacio"),
    ("movies", "Películas"),
    ("animals", "Animales"),
    ("minimalist", "Minimalista"),
    ("winter", "Invierno"),
    ("pixel-art", "Pixel Art"),
]
CATEGORY_IDS = {item[0] for item in CATEGORIES}
ID_PARTS_RE = re.compile(r"^wf-(?P<category>[a-z0-9-]+)@(?P<file>[0-9]+-[a-z0-9-]+)$")
SLUG_RE = re.compile(r"^[a-z0-9-]+$")
ARTICLE_RE = re.compile(
    r'(?s)<article[^>]*data-video-src="(?P<media>[^"]+)"[^>]*>(?P<body>.*?)</article>'
)
URL_RE = re.compile(
    r'href="https://wallsflow\.com/live-wallpapers/(?P<category>[a-z0-9-]+)/'
    r'(?P<file>[0-9]+-[a-z0-9-]+)\.html"'
)
TITLE_ATTR_RE = re.compile(r'title="(?P<title>[^"]+)"')
ARIA_RE = re.compile(r'aria-label="(?P<title>[^"]+)"')
IMG_RE = re.compile(r'<img[^>]+src="(?P<src>https://cloud\.wallsflow\.com/[^"]+)"', re.I)
IMG_ALT_RE = re.compile(r'<img[^>]+alt="(?P<alt>[^"]+)"', re.I)
PAGE_RE = re.compile(r"/live-wallpapers/(?:[a-z0-9-]+/)?page/(?P<page>[0-9]+)/")
H1_RE = re.compile(r"(?s)<h1[^>]*>(?P<title>.*?)</h1>")
OG_RE = re.compile(r'property="og:image"[^>]*content="(?P<url>[^"]+)"')
TAG_LINK_RE = re.compile(r'href="https://wallsflow\.com/tags/(?P<tag>[^"/]+)/"')
RESOLUTION_RE = re.compile(r"(?P<w>[0-9]{3,5})\s*(?:×|x|&#215;)\s*(?P<h>[0-9]{3,5})", re.I)

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
    return match.group("category"), match.group("file")


def _parse_cards(html):
    cards = []
    seen = set()
    for article in ARTICLE_RE.finditer(html):
        body = article.group("body")
        url_match = URL_RE.search(body)
        if not url_match:
            continue
        category = url_match.group("category")
        file_part = url_match.group("file")
        item_id = "%s/%s" % (category, file_part)
        if item_id in seen:
            continue
        title_match = TITLE_ATTR_RE.search(body)
        if not title_match:
            title_match = ARIA_RE.search(body)
        title = d.clean_text(title_match.group("title") if title_match else None)
        if not title:
            alt = IMG_ALT_RE.search(body)
            title = d.clean_text(alt.group("alt") if alt else None)
        if not title:
            title = file_part
        thumb_match = IMG_RE.search(body)
        card = {
            "id": item_id,
            "category": category,
            "file": file_part,
            "title": title,
            "page": "https://wallsflow.com/live-wallpapers/%s/%s.html" % (category, file_part),
            "media": d.clean_text(article.group("media")),
            "thumb": d.clean_text(thumb_match.group("src")) if thumb_match else None,
            "tags": [],
            "published": None,
        }
        seen.add(item_id)
        cards.append(card)
    return cards


def _listing(args):
    query = (args.query or "").strip()
    category = (args.category or "").strip().lower()
    tags = [tag.strip().lower() for tag in (args.tags or "").split(",") if tag.strip()]
    selectors = int(bool(query)) + int(bool(category)) + int(bool(tags))
    if selectors > 1:
        raise d.PluginError("BAD_ARGS", "Wallsflow admite un solo selector: --query, --category o --tags")
    if category:
        if category not in CATEGORY_IDS:
            raise d.PluginError("NOT_FOUND", "categoria desconocida: %s" % category)
        return {"kind": "category", "key": "cat:" + category, "category": category, "query": None, "tag": None}
    if query:
        return {"kind": "search", "key": "search:" + query, "category": None, "query": query, "tag": None}
    if tags:
        if len(tags) > 1:
            raise d.PluginError("BAD_ARGS", "Wallsflow admite un solo tag")
        return {"kind": "tag", "key": "tag:" + tags[0], "category": None, "query": None, "tag": tags[0]}
    return {"kind": "home", "key": "home", "category": None, "query": None, "tag": None}


def _page_url(listing, page):
    if listing["kind"] == "search":
        base = "%s/index.php?do=search&subaction=search&story=%s" % (
            SITE, urllib.parse.quote(listing["query"]))
        return base if page <= 1 else base + "&page=%d" % page
    if listing["kind"] == "category":
        base = "%s/live-wallpapers/%s/" % (SITE, listing["category"])
    elif listing["kind"] == "tag":
        base = "%s/tags/%s/" % (SITE, urllib.parse.quote(listing["tag"]))
    else:
        base = "%s/live-wallpapers/" % SITE
    return base if page <= 1 else base + "page/%d/" % page


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
    if listing["kind"] == "search":
        return len(_parse_cards(_get_page(http, cache, listing, 1)))
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
    if listing["kind"] == "search":
        cards = _parse_cards(_get_page(http, cache, listing, 1))
        return cards[offset:offset + limit]
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
    return d.make_item(
        "wf-%s@%s" % (card["category"], card["file"]),
        card["title"],
        "video",
        card["media"],
        external_id="%s/%s" % (card["category"], card["file"]),
        animated=True,
        width=1280,
        height=720,
        source_url=card["page"],
        license="personal-use",
        attribution="Wallsflow.com",
        categories=[card["category"]],
        static_thumb=card.get("thumb"),
        animated_thumb=card["media"],
        static_cropped=False,
        content_type="video/mp4",
    )


def _detail(http, cache, category, file_part):
    url = "%s/live-wallpapers/%s/%s.html" % (SITE, category, file_part)
    key = "detail-%s-%s.html" % (category, file_part)
    html = cache.get_text(key, DETAIL_TTL, lambda: http.get_text(url, referer=SITE + "/"))
    article = d.find_json_ld(html, "Article") or {}
    video = article.get("video") if isinstance(article.get("video"), dict) else {}
    title = d.clean_text(article.get("name") or article.get("headline"))
    if not title:
        h1 = H1_RE.search(html)
        title = d.clean_text(re.sub(r"<[^>]+>", "", h1.group("title"))) if h1 else None
    title = title or file_part
    published = d.normalize_date(article.get("datePublished"))
    description = d.clean_text(article.get("description"))
    media = video.get("contentUrl")
    if not media:
        media = article.get("contentUrl")
    thumb = None
    image = article.get("image")
    if isinstance(image, list) and image:
        thumb = image[0]
    elif isinstance(image, str):
        thumb = image
    if not thumb:
        og = OG_RE.search(html)
        thumb = d.clean_text(og.group("url")) if og else None
    tags = []
    for tag in TAG_LINK_RE.finditer(html):
        value = urllib.parse.unquote(tag.group("tag")).lower()
        if value not in tags:
            tags.append(value)
    categories = [category]
    # La resolucion del detalle es la del original; el MP4 publico es 720p.
    # Solo se usa para marcar 4K, nunca como width/height del media entregado.
    resolution_match = RESOLUTION_RE.search(html)
    if resolution_match and int(resolution_match.group("w")) >= 3840 and "4k" not in categories:
        categories.append("4k")
    if not media:
        raise d.PluginError("NOT_FOUND", "no hay media para '%s/%s'" % (category, file_part))
    author = article.get("author") if isinstance(article.get("author"), dict) else {}
    return d.make_item(
        "wf-%s@%s" % (category, file_part),
        title,
        "video",
        media,
        external_id="%s/%s" % (category, file_part),
        animated=True,
        width=1280,
        height=720,
        published_at=published,
        source_url=url,
        license="personal-use",
        attribution="Wallsflow.com",
        tags=tags,
        categories=categories,
        static_thumb=thumb,
        animated_thumb=media,
        static_cropped=False,
        description=description,
        content_type="video/mp4",
        author={"name": author.get("name")} if author.get("name") else None,
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
        d.log("aviso: Wallsflow no expone fecha en los listados; se ignoran --since/--until")
    if args.min_rating:
        d.log("aviso: la fuente no publica rating; se ignora --min-rating")

    listing = _listing(args)
    http = _http()
    cache = _cache(args)
    total = _total(http, cache, listing)
    cards = _fetch_cards(http, cache, listing, offset, limit)

    if listing["kind"] != "search" and limit <= ENRICH_LIMIT:
        details = d.parallel_map(
            lambda card: _detail(http, cache, card["category"], card["file"]),
            cards, workers=4,
        )
        items = [item for item in details if item]
    else:
        items = [_card_item(card) for card in cards]

    next_offset = None
    if listing["kind"] != "search":
        if offset + len(items) < total:
            next_offset = offset + len(items)
    return d.list_response(
        items, total, offset, limit, requested,
        next_offset=next_offset,
        sorted_flag=(sort == "newest" and listing["kind"] != "search"),
        has_more=(False if listing["kind"] == "search" else None),
    )


def cmd_detail(args):
    category, file_part = _parse_id(args.id)
    return {"ok": True, "item": _detail(_http(), _cache(args), category, file_part)}


def cmd_thumb(args):
    if args.kind not in ("static", "animated"):
        raise d.PluginError("BAD_ARGS", "--kind debe ser static|animated")
    category, file_part = _parse_id(args.id)
    http = _http()
    cache = _cache(args)
    item = _detail(http, cache, category, file_part)
    animated = args.animated or args.kind == "animated"
    media = (item.get("media") or {}).get("url")
    if animated and media:
        result = d.save_animated_thumb(
            http, media, args.out, size=args.size,
            cache=cache, cache_key="anim-%s-%s.mp4" % (category, file_part), ttl=ASSET_TTL,
        )
        if result:
            return d.thumbnail_response(animated_result=result)
    # El MP4 publico es 1280x720; el fotograma completo evita posters
    # recortados si el original era otra proporcion.
    frame = (item.get("media") or {}).get("url")
    if frame:
        result = d.save_frame_url(
            http, frame, args.out, size=args.size or 640,
            cache=cache, cache_key="frame-%s-%s.mp4" % (category, file_part), ttl=ASSET_TTL,
        )
        if result:
            return d.thumbnail_response(result)
    thumbnail = (item.get("thumbs") or {}).get("staticUrl")
    if not thumbnail:
        raise d.PluginError("NO_THUMB", "Wallsflow no ofrece miniatura")
    result = d.save_thumbnail_url(
        http, thumbnail, args.out, size=args.size,
        cache=cache, cache_key="thumb-%s-%s.webp" % (category, file_part), ttl=ASSET_TTL,
    )
    return d.thumbnail_response(result)


def cmd_fetch(args):
    if args.kind not in ("media", "preview"):
        raise d.PluginError("BAD_ARGS", "--kind debe ser media|preview")
    category, file_part = _parse_id(args.id)
    http = _http()
    item = _detail(http, _cache(args), category, file_part)
    url = (item.get("media") or {}).get("url")
    if not url:
        raise d.PluginError("NOT_FOUND", "no hay media para '%s'" % args.id)
    result = http.download(
        url, args.out, max_bytes=args.max_bytes, timeout=300,
        referer=item.get("sourceUrl"),
    )
    if (result.get("contentType") or "").startswith("text/"):
        try:
            Path(args.out).unlink(missing_ok=True)
        except OSError:
            pass
        raise d.PluginError("NETWORK", "el proveedor devolvio HTML en vez de video")
    if result.get("contentType") in (None, "", "application/octet-stream"):
        result["contentType"] = "video/mp4"
    return d.fetch_response(result, probe=True)


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

