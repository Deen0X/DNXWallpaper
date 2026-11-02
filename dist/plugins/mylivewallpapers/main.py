#!/usr/bin/env python3
"""MyLiveWallpapers (v2, servidor). Sucuri + descarga móvil directa."""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import time
import urllib.parse
import zipfile
from pathlib import Path

import dnxlib as d

PLUGIN_ID = "mylivewallpapers"
SITE = "https://mylivewallpapers.com"
LIST_TTL = 10 * 60
DETAIL_TTL = 60 * 60
ASSET_TTL = 7 * 24 * 3600
ENRICH_LIMIT = 12

CATEGORIES = [
    ("anime", "Anime", "/anime/"),
    ("fantasy", "Fantasía", "/fantasy/"),
    ("games", "Juegos", "/games/"),
    ("cars", "Coches", "/cars/"),
    ("nature", "Naturaleza", "/nature/"),
    ("animals", "Animales", "/animals/"),
    ("lifestyle", "Estilo de vida", "/lifestyle/"),
    ("sci-fi", "Ciencia ficción", "/sci-fi/"),
    ("movies", "Películas", "/movies/"),
    ("scary", "Terror", "/scary/"),
    ("comics", "Cómics", "/comics/"),
    ("abstract", "Abstracto", "/abstract/"),
    ("halloween", "Halloween", "/halloween/"),
    ("cute", "Cute", "/cute/"),
    ("4k-3840x2160", "4K · 3840x2160", "/category/4k-3840x2160/"),
    ("qhd-2560x1440", "QHD · 2560x1440", "/category/qhd-2560x1440/"),
    ("full-hd-1920x1080", "Full HD · 1920x1080", "/category/full-hd-1920x1080/"),
    ("hd-1280x720", "HD · 1280x720", "/category/hd-1280x720/"),
    ("1366x768", "1366x768", "/category/1366x768/"),
    ("ultrawide-standard", "Ultrawide estándar", "/category/ultrawide-standard/"),
    ("ultrawide-3440x1440", "Ultrawide · 3440x1440", "/category/ultrawide-3440x1440/"),
    ("mobile-resolution", "Móvil", "/category/mobile-resolution/"),
    ("other-resolution", "Otras resoluciones", "/category/other-resolution/"),
]
CATEGORY_BASE = {item[0]: item[2] for item in CATEGORIES}
ALIASES = {
    "4k": "4k-3840x2160", "2160p": "4k-3840x2160", "uhd": "4k-3840x2160",
    "3840x2160": "4k-3840x2160",
    "1440p": "qhd-2560x1440", "2k": "qhd-2560x1440", "qhd": "qhd-2560x1440",
    "2560x1440": "qhd-2560x1440",
    "1080p": "full-hd-1920x1080", "fullhd": "full-hd-1920x1080",
    "full-hd": "full-hd-1920x1080", "fhd": "full-hd-1920x1080",
    "1920x1080": "full-hd-1920x1080",
    "720p": "hd-1280x720", "1280x720": "hd-1280x720",
    "1366x768": "1366x768",
    "ultrawide": "ultrawide-standard", "21:9": "ultrawide-standard",
    "3440x1440": "ultrawide-3440x1440",
    "mobile": "mobile-resolution", "vertical": "mobile-resolution",
    "portrait": "mobile-resolution", "other": "other-resolution",
}
ID_PARTS_RE = re.compile(r"^ml-(?P<category>[a-z0-9-]+)@(?P<slug>[a-z0-9-]+)$")
SLUG_RE = re.compile(r"^[a-z0-9-]+$")
CARD_RE = re.compile(
    r'(?s)<a\s+href="https://mylivewallpapers\.com/(?P<category>[a-z0-9-]+)/(?P<slug>[^"/]+/?)"'
    r'(?P<attrs>[^>]*)>(?P<body>.*?)</a>'
)
CLASS_RE = re.compile(r'class="(?P<classes>[^"]*)"')
THUMB_RE = re.compile(r"background-image:\s*url\(\s*(?P<thumb>[^)\s]+)")
TITLE_RE = re.compile(r'(?s)<h2[^>]*class="archive-post-title"[^>]*>(?P<title>[^<]+)</h2>')
DATE_RE = re.compile(r'(?s)<p[^>]*class="archive-post-date"[^>]*>(?P<date>[^<]+)</p>')
TAG_CLASS_RE = re.compile(r"tag-(?P<tag>[a-z0-9-]+)")
DIM_RE = re.compile(r"(?P<w>[0-9]{3,5})\s*x\s*(?P<h>[0-9]{3,5})")
PAGE_RE = re.compile(r"/page/(?P<page>[0-9]+)/")
H1_RE = re.compile(r"(?s)<h1[^>]*>(?P<title>.*?)</h1>")
POST_DATE_RE = re.compile(r'(?s)<p[^>]*class="post-date"[^>]*>(?P<date>[^<]+)</p>')
OG_RE = re.compile(r'property="og:image"[^>]*content="(?P<url>[^"]+)"')
PREVIEW_RE = re.compile(r'https://mylivewallpapers\.com/wp-content/uploads/[^"\'\s]+\.mp4', re.I)
DOWNLOAD_RE = re.compile(r'data-downloadurl="(?P<url>[^"]+)"')
RES_TEXT_RE = re.compile(r"Resolution:.{0,120}?(?P<w>[0-9]{3,5})\s*[xX]\s*(?P<h>[0-9]{3,5})", re.I | re.S)
TAG_LINK_RE = re.compile(r'href="https://mylivewallpapers\.com/tag/(?P<tag>[^"/]+)/"')

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
    path = urllib.parse.urlparse(thumb).path
    filename = path.rsplit("/", 1)[-1]
    if not filename.lower().startswith("thumb-"):
        return None
    preview = "PREVIEW-" + filename[6:]
    preview = re.sub(r"\.[A-Za-z0-9]+$", ".mp4", preview)
    directory = path.rsplit("/", 1)[0]
    return "https://mylivewallpapers.com" + directory + "/" + preview


def _parse_date(value):
    if not value:
        return None
    try:
        from datetime import datetime
        return datetime.strptime(value.strip(), "%B %d, %Y").strftime("%Y-%m-%dT00:00:00Z")
    except ValueError:
        return d.normalize_date(value)


def _parse_cards(html):
    cards = []
    seen = set()
    for match in CARD_RE.finditer(html):
        attrs = match.group("attrs")
        if not re.search(r'class="[^"]*\bpost\b', attrs, re.I):
            continue
        thumb = THUMB_RE.search(attrs)
        if not thumb:
            continue
        body = match.group("body")
        title = TITLE_RE.search(body)
        if not title:
            continue
        category = match.group("category")
        slug = match.group("slug").strip("/")
        item_id = "%s/%s" % (category, slug)
        if item_id in seen:
            continue
        seen.add(item_id)
        classes_match = CLASS_RE.search(attrs)
        classes = classes_match.group("classes") if classes_match else ""
        width = height = None
        dims = DIM_RE.search(classes)
        if dims:
            width, height = int(dims.group("w")), int(dims.group("h"))
        tags = []
        for tag in TAG_CLASS_RE.finditer(classes):
            value = tag.group("tag")
            if value not in tags:
                tags.append(value)
        date = DATE_RE.search(body)
        thumb_url = d.clean_text(thumb.group("thumb"))
        cards.append({
            "id": item_id,
            "category": category,
            "slug": slug,
            "title": d.clean_text(title.group("title")) or slug,
            "page": "https://mylivewallpapers.com/%s/" % item_id,
            "thumb": thumb_url,
            "preview": _preview_from_thumb(thumb_url),
            "published": _parse_date(date.group("date")) if date else None,
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
        raise d.PluginError("BAD_ARGS", "MyLiveWallpapers admite un solo selector: --query, --category o --tags")
    if query:
        alias = ALIASES.get(query.replace(" ", "").lower())
        if alias:
            return {"kind": "category", "key": "cat:" + alias, "base": CATEGORY_BASE[alias],
                    "category": alias, "tag": None, "query": None}
        return {"kind": "search", "key": "search:" + query, "base": None,
                "category": None, "tag": None, "query": query}
    if category:
        if category not in CATEGORY_BASE:
            raise d.PluginError("NOT_FOUND", "categoria desconocida: %s" % category)
        return {"kind": "category", "key": "cat:" + category, "base": CATEGORY_BASE[category],
                "category": category, "tag": None, "query": None}
    if tags:
        if len(tags) > 1:
            raise d.PluginError("BAD_ARGS", "MyLiveWallpapers admite un solo tag")
        tag = tags[0]
        if not SLUG_RE.match(tag):
            raise d.PluginError("BAD_ARGS", "tag invalido: %s" % tag)
        return {"kind": "tag", "key": "tag:" + tag, "base": None,
                "category": None, "tag": tag, "query": None}
    return {"kind": "home", "key": "home", "base": "/", "category": None, "tag": None, "query": None}


def _page_url(listing, page):
    if listing["kind"] == "tag":
        base = "%s/tag/%s/" % (SITE, listing["tag"])
        return base if page <= 1 else base + "page/%d/" % page
    if listing["kind"] == "search":
        return ("%s/?s=%s" % (SITE, urllib.parse.quote(listing["query"]))) if page <= 1 else (
            "%s/page/%d/?s=%s" % (SITE, page, urllib.parse.quote(listing["query"])))
    base = listing["base"] or "/"
    return SITE + base if page <= 1 else SITE + base + "page/%d/" % page


def _get_page(http, cache, listing, page):
    url = _page_url(listing, page)
    key = "list-%s-%d.html" % (listing["key"], page)
    return cache.get_text(
        key, LIST_TTL, lambda: http.get_text(url, referer=SITE + "/", sucuri=True)
    )


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
        "ml-%s@%s" % (card["category"], card["slug"]),
        card["title"],
        "video",
        preview or card["page"],
        external_id="%s/%s" % (card["category"], card["slug"]),
        animated=True,
        width=606,
        height=1080,
        published_at=card.get("published"),
        source_url=card["page"],
        license="personal-use",
        attribution="MyLiveWallpapers.com",
        tags=card.get("tags"),
        categories=categories,
        static_thumb=card.get("thumb"),
        animated_thumb=preview,
        static_cropped=True,
        animated_cropped=True,
        content_type="video/mp4" if preview else "text/html",
    )


def _detail(http, cache, category, slug):
    url = "%s/%s/%s/" % (SITE, category, slug)
    key = "detail-%s-%s.html" % (category, slug)
    html = cache.get_text(
        key, DETAIL_TTL, lambda: http.get_text(url, referer=SITE + "/", sucuri=True)
    )
    h1 = H1_RE.search(html)
    title = d.clean_text(re.sub(r"<[^>]+>", "", h1.group("title"))) if h1 else slug
    title = title or slug
    date_match = POST_DATE_RE.search(html)
    published = _parse_date(date_match.group("date")) if date_match else None
    og = OG_RE.search(html)
    thumbnail = d.clean_text(og.group("url")) if og else None
    preview_match = PREVIEW_RE.search(html)
    preview = preview_match.group(0) if preview_match else None
    downloads = [d.clean_text(m.group("url")) for m in DOWNLOAD_RE.finditer(html)]
    mobile = next((item for item in downloads if "/download/mobile-" in item), None)
    fourk = next((item for item in downloads if "/download/4k-" in item), None)
    media = mobile or fourk or (downloads[0] if downloads else None)
    width = height = None
    res_match = RES_TEXT_RE.search(html)
    if res_match:
        width, height = int(res_match.group("w")), int(res_match.group("h"))
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
    # El media entregado es el MP4 movil (606x1080). La resolucion del texto
    # es la del original 4K y no coincide con el archivo descargado.
    if media and "/download/mobile-" in media:
        width, height = 606, 1080
    return d.make_item(
        "ml-%s@%s" % (category, slug),
        title,
        "video",
        media or preview,
        external_id="%s/%s" % (category, slug),
        animated=True,
        width=width,
        height=height,
        published_at=published,
        source_url=url,
        license="personal-use",
        attribution="MyLiveWallpapers.com",
        tags=tags,
        categories=categories,
        static_thumb=thumbnail,
        animated_thumb=preview,
        static_cropped=d.aspect_mismatch(width, height, 16, 9),
        animated_cropped=d.aspect_mismatch(width, height, 16, 9),
        content_type="video/mp4",
    )


def _file_sha(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _download_media(http, item, out, max_bytes):
    source = (item.get("media") or {}).get("url")
    preview = (item.get("thumbs") or {}).get("animatedUrl")
    if not source:
        raise d.PluginError("NOT_FOUND", "no hay media")
    referer = item.get("sourceUrl")
    temp = out + ".download"
    try:
        result = http.download(source, temp, max_bytes=max_bytes, timeout=300,
                               referer=referer, sucuri=True)
        with open(temp, "rb") as handle:
            magic = handle.read(4)
        if magic == b"PK\x03\x04":
            with zipfile.ZipFile(temp) as archive:
                entries = [entry for entry in archive.infolist() if entry.filename.lower().endswith(".mp4")]
                if not entries:
                    raise d.PluginError("NOT_FOUND", "el paquete 4K no contiene MP4 (solo .mlw propietario)")
                entry = max(entries, key=lambda candidate: candidate.file_size)
                with archive.open(entry) as source_handle, open(out, "wb") as target:
                    shutil.copyfileobj(source_handle, target)
            os.remove(temp)
            return {
                "path": out,
                "bytes": os.path.getsize(out),
                "sha256": _file_sha(out),
                "contentType": "video/mp4",
            }
        os.replace(temp, out)
        if (result.get("contentType") or "").startswith("text/"):
            try:
                os.remove(out)
            except OSError:
                pass
            raise d.PluginError("NETWORK", "el proveedor devolvio HTML en vez de video")
        result["path"] = out
        result["contentType"] = "video/mp4"
        return result
    except d.PluginError as exc:
        try:
            if os.path.exists(temp):
                os.remove(temp)
        except OSError:
            pass
        if exc.code in ("NETWORK", "NOT_FOUND") and preview:
            d.log("aviso: fallo el MP4 (%s); se usa el preview" % exc.message)
            result = http.download(
                preview, out, max_bytes=max_bytes, timeout=300,
                referer=referer, sucuri=True,
            )
            result["contentType"] = "video/mp4"
            return result
        raise
    finally:
        try:
            if os.path.exists(temp):
                os.remove(temp)
        except OSError:
            pass


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
        d.log("aviso: MyLiveWallpapers no soporta filtros de fecha; se ignoran")
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
            cards, workers=2,
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
    media = (item.get("media") or {}).get("url")
    preview = (item.get("thumbs") or {}).get("animatedUrl")
    referer = item.get("sourceUrl")

    # El preview del sitio es 16:9; el MP4 movil es 606x1080. Para conservar
    # la proporcion real del media se usa el MP4 movil.
    if animated:
        for source, suffix in ((media, ".mp4"), (preview, ".mp4")):
            if not source:
                continue
            result = d.save_animated_thumb(
                http, source, args.out, size=args.size,
                cache=cache, cache_key="anim-%s-%s%s" % (category, slug, suffix),
                ttl=ASSET_TTL, referer=referer, sucuri=True,
            )
            if result:
                return d.thumbnail_response(animated_result=result)

    for source, suffix in ((media, ".mp4"), (preview, ".mp4")):
        if not source:
            continue
        result = d.save_frame_url(
            http, source, args.out, size=args.size or 640,
            cache=cache, cache_key="frame-%s-%s%s" % (category, slug, suffix),
            ttl=ASSET_TTL, referer=referer, sucuri=True,
        )
        if result:
            return d.thumbnail_response(result)

    thumbnail = (item.get("thumbs") or {}).get("staticUrl")
    if not thumbnail:
        raise d.PluginError("NO_THUMB", "MyLiveWallpapers no ofrece miniatura")
    result = d.save_thumbnail_url(
        http, thumbnail, args.out, size=args.size,
        cache=cache, cache_key="thumb-%s-%s.png" % (category, slug), ttl=ASSET_TTL,
        referer=referer, sucuri=True,
    )
    return d.thumbnail_response(result)


def cmd_fetch(args):
    if args.kind not in ("media", "preview"):
        raise d.PluginError("BAD_ARGS", "--kind debe ser media|preview")
    category, slug = _parse_id(args.id)
    http = _http()
    item = _detail(http, _cache(args), category, slug)
    if args.kind == "preview":
        preview = (item.get("thumbs") or {}).get("animatedUrl")
        if not preview:
            raise d.PluginError("NOT_FOUND", "no hay preview para '%s'" % args.id)
        result = http.download(
            preview, args.out, max_bytes=args.max_bytes, timeout=300,
            referer=item.get("sourceUrl"), sucuri=True,
        )
        result["contentType"] = "video/mp4"
    else:
        result = _download_media(http, item, args.out, args.max_bytes)
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

