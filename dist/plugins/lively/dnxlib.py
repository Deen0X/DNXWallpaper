#!/usr/bin/env python3
"""Runtime comun para los plugins DNXWallpaper v2 (servidor).

Solo stdlib.  La salida de `main.py` debe ser exactamente una linea JSON en
stdout; los logs van a stderr.  Este modulo no imprime nada por si mismo.
"""

from __future__ import annotations

import argparse
import base64
import binascii
import concurrent.futures
import datetime as _dt
import gzip
import hashlib
import html as _html
import json
import os
import re
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import traceback
import urllib.error
import urllib.parse
import urllib.request
import zlib
from pathlib import Path

VERSION = "2.0.0"
MAX_LIMIT = 200

# En Windows la consola puede ser cp1252; el contrato exige UTF-8 en stdout.
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
BROWSER_UA = (
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/131.0.0.0 Safari/537.36"
)
EXIT_CODES = {
    "BAD_ARGS": 2,
    "NOT_FOUND": 4,
    "NO_THUMB": 4,
    "CONFIG_REQUIRED": 5,
    "TOO_LARGE": 6,
    "RATE_LIMITED": 7,
    "NETWORK": 8,
    "PARSE": 9,
    "INTERNAL": 1,
}


class PluginError(Exception):
    def __init__(self, code, message, **extra):
        super().__init__(message)
        self.code = code
        self.message = message
        self.extra = extra
        self.exit_code = EXIT_CODES.get(code, 1)


def log(*args):
    print(*args, file=sys.stderr, flush=True)


def emit(obj):
    json.dump(obj, sys.stdout, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
    sys.stdout.write("\n")
    sys.stdout.flush()


def run(handlers):
    """Ejecuta el comando y garantiza una unica linea JSON en stdout."""
    try:
        parser = build_parser()
        args = parser.parse_args()
        handler = handlers.get(args.cmd)
        if handler is None:
            raise PluginError("BAD_ARGS", "comando desconocido: %s" % args.cmd)
        result = handler(args)
        if result is None:
            result = {"ok": True}
        emit(result)
        return 0
    except PluginError as exc:
        payload = {"ok": False, "code": exc.code, "message": exc.message}
        payload.update(exc.extra)
        emit(payload)
        return exc.exit_code
    except KeyboardInterrupt:
        emit({"ok": False, "code": "INTERNAL", "message": "interrumpido"})
        return 1
    except SystemExit as exc:
        # argparse solo deberia salir en casos que ya hemos convertido.
        code = int(exc.code or 0)
        if code == 0:
            return 0
        emit({"ok": False, "code": "BAD_ARGS", "message": "argumentos invalidos"})
        return 2
    except Exception as exc:  # noqa: BLE001 - el contrato exige INTERNAL
        log(traceback.format_exc())
        emit({"ok": False, "code": "INTERNAL", "message": str(exc) or exc.__class__.__name__})
        return 1


class JsonArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        raise PluginError("BAD_ARGS", message)

    def exit(self, status=0, message=None):
        if message:
            log(message.strip())
        if status == 0:
            raise SystemExit(0)
        raise PluginError("BAD_ARGS", message.strip() if message else "argumentos invalidos")


def _common_parent():
    parent = argparse.ArgumentParser(add_help=False)
    parent.add_argument("--cache-dir", default="")
    parent.add_argument("--config", default="")
    return parent


def build_parser():
    parent = _common_parent()
    parser = JsonArgumentParser(prog="main.py", add_help=False)
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("health", parents=[parent], add_help=False)
    sub.add_parser("categories", parents=[parent], add_help=False)

    p = sub.add_parser("list", parents=[parent], add_help=False)
    p.add_argument("--page", type=int, default=1)
    p.add_argument("--limit", type=int, default=100)
    p.add_argument("--sort", default="newest")
    p.add_argument("--query", default="")
    p.add_argument("--category", default="")
    p.add_argument("--cursor", default="")
    p.add_argument("--tags", default="")
    p.add_argument("--animated", nargs="?", const=True, default=None)
    p.add_argument("--min-rating", type=int, default=0)
    p.add_argument("--since", default="")
    p.add_argument("--until", default="")

    p = sub.add_parser("detail", parents=[parent], add_help=False)
    p.add_argument("--id", required=True)

    p = sub.add_parser("thumb", parents=[parent], add_help=False)
    p.add_argument("--id", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--size", type=int, default=None)
    p.add_argument("--animated", action="store_true")
    p.add_argument("--kind", default="static")

    p = sub.add_parser("fetch", parents=[parent], add_help=False)
    p.add_argument("--id", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--max-bytes", type=int, default=None)
    p.add_argument("--kind", default="media")

    p = sub.add_parser("config", parents=[parent], add_help=False)
    p.add_argument("--set", action="append", default=[])
    return parser


# ---------------------------------------------------------------------------
# Utilidades
# ---------------------------------------------------------------------------


def parse_bool(value, default=None):
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    text = str(value).strip().lower()
    if text in ("1", "true", "yes", "y", "on", "si", "sí"):
        return True
    if text in ("0", "false", "no", "n", "off"):
        return False
    raise PluginError("BAD_ARGS", "valor booleano invalido: %s" % value)


def parse_int(value, default=None, minimum=None, maximum=None):
    if value is None or value == "":
        return default
    try:
        number = int(value)
    except (TypeError, ValueError):
        raise PluginError("BAD_ARGS", "numero invalido: %s" % value)
    if minimum is not None and number < minimum:
        raise PluginError("BAD_ARGS", "valor fuera de rango: %s" % value)
    if maximum is not None and number > maximum:
        raise PluginError("BAD_ARGS", "valor fuera de rango: %s" % value)
    return number


def utc_now():
    return _dt.datetime.now(_dt.timezone.utc)


def normalize_date(value):
    """Devuelve ISO-8601 UTC con Z o None."""
    if value is None:
        return None
    if isinstance(value, _dt.datetime):
        date = value
    elif isinstance(value, _dt.date):
        date = _dt.datetime(value.year, value.month, value.day, tzinfo=_dt.timezone.utc)
    else:
        text = str(value).strip()
        if not text:
            return None
        if re.fullmatch(r"\d{4}-\d{2}-\d{2}", text):
            text += "T00:00:00+00:00"
        try:
            date = _dt.datetime.fromisoformat(text.replace("Z", "+00:00"))
        except ValueError:
            return None
    if date.tzinfo is None:
        date = date.replace(tzinfo=_dt.timezone.utc)
    return date.astimezone(_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_date_filter(value):
    if not value:
        return None
    normalized = normalize_date(value)
    if not normalized:
        raise PluginError("BAD_ARGS", "fecha invalida; use YYYY-MM-DD")
    return normalized


def clean_text(value, limit=None):
    if value is None:
        return None
    text = _html.unescape(str(value))
    text = re.sub(r"<[^>]+>", " ", text)
    text = re.sub(r"\s+", " ", text).strip()
    if not text:
        return None
    if limit and len(text) > limit:
        text = text[: limit - 1].rstrip() + "…"
    return text


def strip_tags(value):
    return clean_text(value)


def attr(tag_html, name):
    match = re.search(r'\b%s\s*=\s*["\']([^"\']*)["\']' % re.escape(name), tag_html, re.I | re.S)
    return _html.unescape(match.group(1)) if match else None


def meta_content(markup, prop):
    patterns = [
        r'<meta[^>]+(?:property|name)\s*=\s*["\']%s["\'][^>]*content\s*=\s*["\']([^"\']*)["\']' % re.escape(prop),
        r'<meta[^>]+content\s*=\s*["\']([^"\']*)["\'][^>]*(?:property|name)\s*=\s*["\']%s["\']' % re.escape(prop),
    ]
    for pattern in patterns:
        match = re.search(pattern, markup, re.I | re.S)
        if match:
            return _html.unescape(match.group(1))
    return None


def json_ld_nodes(markup):
    for match in re.finditer(
        r'<script[^>]*type\s*=\s*["\']?application/ld\+json["\']?[^>]*>(.*?)</script>',
        markup,
        re.I | re.S,
    ):
        raw = match.group(1).strip()
        try:
            node = json.loads(raw)
        except Exception:
            continue
        stack = [node]
        while stack:
            current = stack.pop()
            if isinstance(current, dict):
                yield current
                graph = current.get("@graph")
                if isinstance(graph, list):
                    stack.extend(graph)
            elif isinstance(current, list):
                stack.extend(current)


def find_json_ld(markup, type_name):
    wanted = type_name.lower()
    for node in json_ld_nodes(markup):
        node_type = node.get("@type")
        if isinstance(node_type, str) and node_type.lower() == wanted:
            return node
        if isinstance(node_type, list) and any(str(item).lower() == wanted for item in node_type):
            return node
    return None


def encode_cursor(offset):
    raw = json.dumps({"o": int(offset)}, separators=(",", ":")).encode("utf-8")
    return base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=")


def decode_cursor(cursor):
    try:
        padding = "=" * (-len(cursor) % 4)
        raw = base64.urlsafe_b64decode(cursor + padding)
        data = json.loads(raw.decode("utf-8"))
        offset = int(data["o"])
        if offset < 0:
            raise ValueError
        return offset
    except Exception:
        raise PluginError("BAD_ARGS", "cursor invalido")


def page_offset(args, limit):
    if getattr(args, "cursor", ""):
        return decode_cursor(args.cursor)
    page = parse_int(getattr(args, "page", 1), 1, 1)
    return (page - 1) * limit


def request_limit(args):
    requested = parse_int(getattr(args, "limit", 100), 100, 1)
    return min(requested, MAX_LIMIT), requested


def sort_value(args):
    value = (getattr(args, "sort", "") or "newest").strip().lower()
    if value not in ("newest", "popular", "relevance"):
        raise PluginError("BAD_ARGS", "--sort debe ser newest|popular|relevance")
    return value


def paginate_offset(fetch_page, offset, limit, max_pages=100):
    """Recorre paginas de tamano variable y devuelve una rebanada por offset."""
    results = []
    page = 1
    skipped = 0
    while len(results) < limit and page <= max_pages:
        items = fetch_page(page)
        if not items:
            break
        if skipped + len(items) <= offset:
            skipped += len(items)
            page += 1
            continue
        start = max(0, offset - skipped)
        for item in items[start:]:
            results.append(item)
            if len(results) >= limit:
                break
        skipped += len(items)
        page += 1
    return results


def list_response(items, total, offset, limit, requested=None, next_offset=None,
                  sorted_flag=True, extra=None, has_more=None):
    if next_offset is None and has_more is None:
        if total is not None and offset + len(items) < total and items:
            next_offset = offset + len(items)
        else:
            next_offset = None
    if has_more is None:
        has_more = next_offset is not None and (total is None or next_offset < total)
    payload = {
        "ok": True,
        "total": total,
        "hasMore": bool(has_more),
        "nextCursor": encode_cursor(next_offset) if has_more else None,
        "limit": limit,
        "items": items,
    }
    if requested is not None and requested != limit:
        payload["requestedLimit"] = requested
    if sorted_flag is False:
        payload["sorted"] = False
    if extra:
        payload.update(extra)
    return payload


def make_item(item_id, title, media_type, media_url, *, external_id=None,
              animated=None, width=None, height=None, size_bytes=None,
              duration_sec=None, fps=None, codec=None, hdr=None,
              published_at=None, source_url=None, license=None, attribution=None,
              tags=None, categories=None, static_thumb=None, animated_thumb=None,
              static_cropped=None, animated_cropped=None,
              description=None, content_type=None, popularity=None, author=None,
              downloadable=True):
    if not media_url:
        raise PluginError("INTERNAL", "item sin media.url: %s" % item_id)
    item = {
        "id": str(item_id),
        "title": clean_text(title) or str(external_id or item_id),
        "mediaType": media_type,
        "media": {"type": media_type, "url": media_url},
    }
    if external_id:
        item["externalId"] = str(external_id)
    if animated is not None:
        item["animated"] = bool(animated)
    if published_at:
        normalized = normalize_date(published_at)
        if normalized:
            item["publishedAt"] = normalized
    if width:
        item["width"] = int(width)
    if height:
        item["height"] = int(height)
    if size_bytes:
        item["sizeBytes"] = int(size_bytes)
    if duration_sec:
        item["durationSec"] = round(float(duration_sec), 3)
    if fps:
        item["fps"] = round(float(fps), 3)
    if codec:
        item["codec"] = codec
    if hdr is not None:
        item["hdr"] = bool(hdr)
    if license:
        item["license"] = license
    if attribution:
        item["attribution"] = attribution
    if source_url:
        item["sourceUrl"] = source_url
    if tags:
        unique = []
        for tag in tags:
            text = clean_text(tag, 80)
            if text and text not in unique:
                unique.append(text)
        if unique:
            item["tags"] = unique
    if categories:
        unique = []
        for category in categories:
            text = clean_text(category, 80)
            if text and text not in unique:
                unique.append(text)
        if unique:
            item["categories"] = unique
    if description:
        item["description"] = clean_text(description, 1200)
    if content_type:
        item["media"]["contentType"] = content_type
    if size_bytes:
        item["media"]["sizeBytes"] = int(size_bytes)
    if static_thumb or animated_thumb:
        thumbs = {
            "staticUrl": static_thumb or None,
            "animatedUrl": animated_thumb or None,
        }
        if static_cropped:
            thumbs["staticCropped"] = True
        if animated_cropped:
            thumbs["animatedCropped"] = True
        item["thumbs"] = thumbs
    if popularity is not None:
        item["popularity"] = popularity
    if author:
        item["author"] = author
    item["downloadable"] = bool(downloadable)
    return item


def aspect_mismatch(width, height, reference_width=16, reference_height=9,
                    tolerance=0.06):
    """True si la proporcion del medio difiere de la referencia."""
    try:
        width = float(width)
        height = float(height)
        reference_width = float(reference_width)
        reference_height = float(reference_height)
    except (TypeError, ValueError):
        return False
    if width <= 0 or height <= 0 or reference_width <= 0 or reference_height <= 0:
        return False
    ratio = width / height
    reference = reference_width / reference_height
    return abs(ratio - reference) / reference > tolerance


# ---------------------------------------------------------------------------
# HTTP
# ---------------------------------------------------------------------------


def _decompress(body, headers):
    encoding = (headers.get("Content-Encoding") or "").lower()
    if not body or not encoding:
        return body
    if "gzip" in encoding:
        try:
            return gzip.decompress(body)
        except Exception:
            return body
    if "deflate" in encoding:
        try:
            return zlib.decompress(body)
        except Exception:
            try:
                return zlib.decompress(body, -zlib.MAX_WBITS)
            except Exception:
                return body
    return body


def _retry_after(headers):
    value = headers.get("Retry-After") if headers else None
    if value:
        try:
            return max(1, int(float(value)))
        except ValueError:
            pass
    reset = headers.get("X-RateLimit-Reset") if headers else None
    if reset:
        try:
            return max(1, int(float(reset) - time.time()))
        except ValueError:
            pass
    return 60


class HttpClient:
    def __init__(self, plugin_id, version=VERSION, site="https://example.invalid",
                 browser_ua=False, accept=None, extra_headers=None):
        self.plugin_id = plugin_id
        self.version = version
        self.site = site
        self.cookies = {}
        self._cookie_lock = threading.Lock()
        self.user_agent = BROWSER_UA if browser_ua else (
            "DNXWallpaperEngine-Plugin-%s/%s (+%s)" % (plugin_id, version, site)
        )
        self.accept = accept or "*/*"
        self.extra_headers = dict(extra_headers or {})
        self.opener = urllib.request.build_opener()

    def _headers(self, headers=None, referer=None):
        merged = {
            "User-Agent": self.user_agent,
            "Accept": self.accept,
            "Accept-Language": "en-US,en;q=0.9",
            "Accept-Encoding": "gzip, deflate",
        }
        with self._cookie_lock:
            cookie_header = "; ".join("%s=%s" % (k, v) for k, v in self.cookies.items())
        if cookie_header:
            merged["Cookie"] = cookie_header
        merged.update(self.extra_headers)
        if referer:
            merged["Referer"] = referer
        if headers:
            merged.update(headers)
        return merged

    def _store_cookies(self, headers):
        if headers is None:
            return
        try:
            values = headers.get_all("Set-Cookie") or []
        except AttributeError:
            value = headers.get("Set-Cookie")
            values = [value] if value else []
        for value in values:
            pair = value.split(";", 1)[0].strip()
            if "=" in pair:
                name, cookie_value = pair.split("=", 1)
                with self._cookie_lock:
                    self.cookies[name.strip()] = cookie_value.strip()

    def _open_raw(self, url, method="GET", headers=None, timeout=30, referer=None, data=None):
        request = urllib.request.Request(
            url, data=data, method=method, headers=self._headers(headers, referer)
        )
        try:
            response = self.opener.open(request, timeout=timeout)
            status = getattr(response, "status", 200)
            response_headers = response.headers
            body = b"" if method == "HEAD" else response.read()
            final_url = response.geturl()
            response.close()
            self._store_cookies(response_headers)
            return status, final_url, response_headers, body
        except urllib.error.HTTPError as exc:
            body = b""
            try:
                body = exc.read()
            except Exception:
                pass
            self._store_cookies(exc.headers)
            return exc.code, url, exc.headers, body
        except (urllib.error.URLError, socket.timeout, TimeoutError, ssl.SSLError,
                ConnectionError, OSError) as exc:
            reason = getattr(exc, "reason", exc)
            raise PluginError("NETWORK", "fallo de red: %s (%s)" % (url, reason))

    def _raise_status(self, status, url, headers=None, body=b""):
        text = ""
        try:
            text = body.decode("utf-8", "replace")[:1000]
        except Exception:
            pass
        if status in (401, 403) and "invalid api key" in text.lower():
            raise PluginError("CONFIG_REQUIRED", "API key invalida")
        if status == 404 or status == 410:
            raise PluginError("NOT_FOUND", "404: %s" % url)
        if status == 429:
            raise PluginError(
                "RATE_LIMITED",
                "limite del proveedor: %s" % url,
                retryAfterSec=_retry_after(headers),
            )
        if status == 401:
            raise PluginError("CONFIG_REQUIRED", "autenticacion requerida: %s" % url)
        if status == 403:
            raise PluginError("NETWORK", "acceso denegado (403): %s" % url)
        if status == 400:
            if "invalid api key" in text.lower():
                raise PluginError("CONFIG_REQUIRED", "API key invalida")
            raise PluginError("BAD_ARGS", "peticion rechazada (400): %s" % url)
        if 500 <= status <= 599:
            raise PluginError("NETWORK", "error del servidor (%s): %s" % (status, url))
        raise PluginError("NETWORK", "HTTP %s: %s" % (status, url))

    def get_bytes(self, url, headers=None, timeout=30, referer=None, sucuri=False):
        for attempt in range(3):
            status, final_url, response_headers, body = self._open_raw(
                url, "GET", headers, timeout, referer
            )
            if status == 200:
                return _decompress(body, response_headers)
            if sucuri and _is_sucuri(body):
                cookie = solve_sucuri(body.decode("utf-8", "replace"))
                if cookie:
                    self._store_cookie_pair(cookie)
                    continue
            if status in (301, 302, 303, 307, 308) and not body:
                raise PluginError("NETWORK", "redireccion sin destino: %s" % url)
            self._raise_status(status, url, response_headers, body)
        raise PluginError("NETWORK", "no se pudo superar el challenge: %s" % url)

    def get_text(self, url, headers=None, timeout=30, referer=None, sucuri=False):
        data = self.get_bytes(url, headers, timeout, referer, sucuri)
        return data.decode("utf-8", "replace")

    def get_json(self, url, headers=None, timeout=30, referer=None):
        data = self.get_bytes(url, headers, timeout, referer)
        try:
            return json.loads(data.decode("utf-8", "replace"))
        except Exception as exc:
            raise PluginError("PARSE", "respuesta JSON invalida: %s" % exc)

    def head(self, url, headers=None, timeout=15, referer=None):
        try:
            status, final_url, response_headers, _ = self._open_raw(
                url, "HEAD", headers, timeout, referer
            )
        except PluginError:
            return None
        if status == 200:
            return response_headers
        return None

    def download(self, url, out, max_bytes=None, timeout=300, headers=None,
                 referer=None, sucuri=False):
        out_path = Path(out)
        if out_path.parent and str(out_path.parent) not in ("", "."):
            out_path.parent.mkdir(parents=True, exist_ok=True)

        if max_bytes is not None:
            head_headers = self.head(url, headers, referer=referer)
            if head_headers is not None:
                length = head_headers.get("Content-Length")
                if length:
                    try:
                        if int(length) > max_bytes:
                            raise PluginError(
                                "TOO_LARGE",
                                "el medio supera --max-bytes",
                                bytes=int(length),
                            )
                    except ValueError:
                        pass

        temp_path = str(out_path) + ".part"
        last_error = None
        for attempt in range(3):
            request = urllib.request.Request(
                url, method="GET", headers=self._headers(headers, referer)
            )
            response = None
            try:
                response = self.opener.open(request, timeout=timeout)
                status = getattr(response, "status", 200)
                response_headers = response.headers
                content_type = response_headers.get("Content-Type") or ""
                if status == 200:
                    length = response_headers.get("Content-Length")
                    if max_bytes is not None and length:
                        try:
                            if int(length) > max_bytes:
                                raise PluginError(
                                    "TOO_LARGE",
                                    "el medio supera --max-bytes",
                                    bytes=int(length),
                                )
                        except ValueError:
                            pass
                    digest = hashlib.sha256()
                    total = 0
                    try:
                        with open(temp_path, "wb") as output:
                            while True:
                                chunk = response.read(65536)
                                if not chunk:
                                    break
                                total += len(chunk)
                                if max_bytes is not None and total > max_bytes:
                                    raise PluginError(
                                        "TOO_LARGE",
                                        "el medio supera --max-bytes",
                                        bytes=total,
                                    )
                                digest.update(chunk)
                                output.write(chunk)
                    finally:
                        response.close()
                        response = None
                    if max_bytes is not None and total > max_bytes:
                        raise PluginError("TOO_LARGE", "el medio supera --max-bytes", bytes=total)
                    os.replace(temp_path, str(out_path))
                    self._store_cookies(response_headers)
                    return {
                        "path": str(out),
                        "bytes": total,
                        "sha256": digest.hexdigest(),
                        "contentType": content_type.split(";")[0].strip() or "application/octet-stream",
                    }

                body = response.read(65536)
                response.close()
                response = None
                self._store_cookies(response_headers)
                if sucuri and _is_sucuri(body):
                    cookie = solve_sucuri(body.decode("utf-8", "replace"))
                    if cookie:
                        self._store_cookie_pair(cookie)
                        continue
                self._raise_status(status, url, response_headers, body)
            except PluginError:
                _try_delete(temp_path)
                raise
            except urllib.error.HTTPError as exc:
                error_body = b""
                try:
                    error_body = exc.read()
                except Exception:
                    pass
                self._store_cookies(exc.headers)
                if sucuri and _is_sucuri(error_body):
                    cookie = solve_sucuri(error_body.decode("utf-8", "replace"))
                    if cookie:
                        self._store_cookie_pair(cookie)
                        continue
                _try_delete(temp_path)
                self._raise_status(exc.code, url, exc.headers, error_body)
            except (urllib.error.URLError, socket.timeout,
                    TimeoutError, ssl.SSLError, ConnectionError, OSError) as exc:
                last_error = exc
                _try_delete(temp_path)
                if attempt < 2:
                    time.sleep(0.4 * (attempt + 1))
                    continue
                raise PluginError("NETWORK", "fallo de descarga: %s (%s)" % (url, exc))
            finally:
                if response is not None:
                    try:
                        response.close()
                    except Exception:
                        pass
        _try_delete(temp_path)
        raise PluginError("NETWORK", "no se pudo descargar %s (%s)" % (url, last_error))

    def _store_cookie_pair(self, pair):
        if "=" in pair:
            name, value = pair.split("=", 1)
            with self._cookie_lock:
                self.cookies[name.strip()] = value.strip()


def _try_delete(path):
    try:
        if os.path.exists(path):
            os.remove(path)
    except Exception:
        pass


def _is_sucuri(body):
    if not body:
        return False
    text = body.decode("utf-8", "replace")
    return "sucuri_cloudproxy_js" in text or "sucuri_cloudproxy_uuid" in text


# ---------------------------------------------------------------------------
# Sucuri (mylivewallpapers)
# ---------------------------------------------------------------------------


def _split_top(text, separator):
    parts = []
    buffer = []
    quote = None
    depth = 0
    index = 0
    while index < len(text):
        char = text[index]
        if quote:
            buffer.append(char)
            if char == "\\" and index + 1 < len(text):
                buffer.append(text[index + 1])
                index += 2
                continue
            if char == quote:
                quote = None
            index += 1
            continue
        if char in ("'", '"'):
            quote = char
            buffer.append(char)
        elif char == "(":
            depth += 1
            buffer.append(char)
        elif char == ")":
            depth = max(0, depth - 1)
            buffer.append(char)
        elif char == separator and depth == 0:
            parts.append("".join(buffer))
            buffer = []
        else:
            buffer.append(char)
        index += 1
    parts.append("".join(buffer))
    return parts


def _js_unescape(text):
    out = []
    index = 0
    while index < len(text):
        char = text[index]
        if char != "\\" or index + 1 >= len(text):
            out.append(char)
            index += 1
            continue
        nxt = text[index + 1]
        index += 2
        if nxt == "n":
            out.append("\n")
        elif nxt == "r":
            out.append("\r")
        elif nxt == "t":
            out.append("\t")
        elif nxt == "0":
            out.append("\0")
        elif nxt == "\\":
            out.append("\\")
        elif nxt == "'":
            out.append("'")
        elif nxt == '"':
            out.append('"')
        elif nxt == "x" and index + 2 <= len(text):
            try:
                out.append(chr(int(text[index:index + 2], 16)))
                index += 2
            except ValueError:
                out.append("x")
        elif nxt == "u" and index + 4 <= len(text):
            try:
                out.append(chr(int(text[index:index + 4], 16)))
                index += 4
            except ValueError:
                out.append("u")
        else:
            out.append(nxt)
    return "".join(out)


def _js_eval(expression, variables):
    result = []
    for raw in _split_top(expression, "+"):
        token = raw.strip()
        if not token or token in ("''", '""'):
            continue
        if len(token) >= 2 and token[0] == token[-1] and token[0] in ("'", '"'):
            result.append(_js_unescape(token[1:-1]))
            continue
        match = re.match(r"(?is)^String\s*\.\s*fromCharCode\s*\((.*)\)$", token)
        if match:
            for argument in match.group(1).split(","):
                try:
                    result.append(chr(int(argument.strip())))
                except ValueError:
                    pass
            continue
        if token in variables:
            result.append(variables[token])
            continue
        result.append(token)
    return "".join(result)


def solve_sucuri(markup):
    match = re.search(r"S\s*=\s*'(?P<b64>[^']+)'", markup, re.S)
    if not match:
        return None
    try:
        code = base64.b64decode(match.group("b64")).decode("utf-8", "replace")
    except (binascii.Error, ValueError):
        return None

    variables = {}
    for raw in _split_top(code, ";"):
        statement = raw.strip()
        if not statement:
            continue
        cookie = re.match(r"(?s)^document\s*\.\s*cookie\s*=\s*(?P<expr>.*)$", statement)
        if cookie:
            value = _js_eval(cookie.group("expr"), variables)
            pair = value.split(";", 1)[0].strip()
            return pair if "=" in pair else None
        assignment = re.match(
            r"(?s)^(?:var\s+)?(?P<name>[A-Za-z_$][A-Za-z0-9_$]*)\s*=\s*(?P<expr>.*)$",
            statement,
        )
        if assignment:
            variables[assignment.group("name")] = _js_eval(assignment.group("expr"), variables)
    return None


# ---------------------------------------------------------------------------
# Cache en disco
# ---------------------------------------------------------------------------


class DiskCache:
    def __init__(self, base_dir, cache_dir=None):
        root = Path(cache_dir) if cache_dir else Path(base_dir) / "cache"
        try:
            root.mkdir(parents=True, exist_ok=True)
            probe = root / ".write-test"
            probe.write_text("ok", encoding="utf-8")
            probe.unlink(missing_ok=True)
        except OSError:
            root = Path(tempfile.gettempdir()) / "dnx-plugin-cache" / Path(base_dir).name
            root.mkdir(parents=True, exist_ok=True)
        self.root = root

    def _path(self, key):
        safe = re.sub(r"[^A-Za-z0-9._-]+", "_", key)
        safe = safe[:80].strip("._") or "cache"
        digest = hashlib.sha1(key.encode("utf-8")).hexdigest()[:12]
        suffix = Path(key).suffix
        if suffix and len(suffix) <= 6 and re.fullmatch(r"\.[A-Za-z0-9]+", suffix):
            name = "%s-%s%s" % (safe, digest, suffix)
        else:
            name = "%s-%s" % (safe, digest)
        return self.root / name

    def get_bytes(self, key, ttl, loader):
        path = self._path(key)
        try:
            if path.exists() and ttl and time.time() - path.stat().st_mtime < ttl:
                return path.read_bytes()
        except OSError:
            pass
        data = loader()
        try:
            temp = str(path) + ".part"
            with open(temp, "wb") as handle:
                handle.write(data)
            os.replace(temp, str(path))
        except OSError:
            pass
        return data

    def get_text(self, key, ttl, loader):
        return self.get_bytes(key, ttl, lambda: loader().encode("utf-8")).decode("utf-8", "replace")

    def get_json(self, key, ttl, loader):
        raw = self.get_bytes(key, ttl, lambda: json.dumps(loader()).encode("utf-8"))
        try:
            return json.loads(raw.decode("utf-8", "replace"))
        except Exception:
            raise PluginError("PARSE", "cache JSON invalida")


# ---------------------------------------------------------------------------
# ffmpeg / ffprobe
# ---------------------------------------------------------------------------

_CREATE_NO_WINDOW = 0x08000000 if os.name == "nt" else 0


def _find_exe(name, env_name=None):
    env_value = os.environ.get(env_name) if env_name else None
    if env_value:
        candidate = Path(env_value)
        if candidate.is_file():
            return str(candidate)
        if candidate.is_dir():
            for filename in (name + ".exe", name):
                path = candidate / filename
                if path.is_file():
                    return str(path)
    found = shutil.which(name)
    if found:
        return found
    here = Path(__file__).resolve().parent
    for directory in (here, here.parent, here.parent / "tools", here.parent.parent / "tools"):
        for filename in (name + ".exe", name):
            path = directory / filename
            if path.is_file():
                return str(path)
    return None


def ffmpeg_exe():
    return _find_exe("ffmpeg", "DNX_FFMPEG")


def ffprobe_exe():
    return _find_exe("ffprobe", "DNX_FFPROBE")


def _run_process(args, timeout=60):
    try:
        completed = subprocess.run(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            creationflags=_CREATE_NO_WINDOW,
            check=False,
        )
        return completed.returncode, completed.stdout.decode("utf-8", "replace"), completed.stderr.decode("utf-8", "replace")
    except (OSError, subprocess.SubprocessError) as exc:
        return 1, "", str(exc)


def ffmpeg_available():
    return ffmpeg_exe() is not None


def ffprobe_available():
    return ffprobe_exe() is not None


def probe_video(path, timeout=30):
    exe = ffprobe_exe()
    if not exe:
        return None
    code, stdout, stderr = _run_process(
        [
            exe, "-v", "error", "-select_streams", "v:0",
            "-show_entries", "stream=width,height,codec_name,r_frame_rate",
            "-show_entries", "format=duration", "-of", "json", str(path),
        ],
        timeout=timeout,
    )
    if code != 0:
        if stderr:
            log("ffprobe: " + stderr.strip())
        return None
    try:
        data = json.loads(stdout)
    except Exception:
        return None
    streams = data.get("streams") or []
    if not streams:
        return None
    stream = streams[0]
    result = {}
    try:
        result["width"] = int(stream.get("width") or 0)
        result["height"] = int(stream.get("height") or 0)
    except (TypeError, ValueError):
        pass
    duration = (data.get("format") or {}).get("duration")
    if duration:
        try:
            result["durationSec"] = float(duration)
        except ValueError:
            pass
    codec = stream.get("codec_name")
    if codec:
        result["codec"] = codec
    rate = stream.get("r_frame_rate")
    if rate and "/" in rate:
        try:
            numerator, denominator = rate.split("/", 1)
            denominator = float(denominator)
            if denominator:
                result["fps"] = float(numerator) / denominator
        except (ValueError, ZeroDivisionError):
            pass
    return result or None


def image_size(path):
    info = probe_video(path, timeout=15)
    if info and info.get("width") and info.get("height"):
        return int(info["width"]), int(info["height"])
    try:
        from PIL import Image  # type: ignore

        with Image.open(path) as image:
            return int(image.width), int(image.height)
    except Exception:
        return 0, 0


def _image_suffix(path):
    suffix = Path(path).suffix.lower()
    return suffix if suffix in (".jpg", ".jpeg", ".png", ".webp") else ""


def ffmpeg_image(source, output, size=None):
    exe = ffmpeg_exe()
    if not exe:
        return False
    suffix = _image_suffix(output)
    target = output
    if not suffix:
        target = str(Path(output).with_suffix(".jpg"))
    args = [exe, "-y", "-i", str(source)]
    filters = []
    if size and int(size) > 0:
        limit = int(size)
        # Cap the longest side without upscaling and sin recortar.
        filters.append(
            "scale=w='min(%d,iw)':h='min(%d,ih)':force_original_aspect_ratio=decrease"
            % (limit, limit)
        )
    if target.lower().endswith((".jpg", ".jpeg")):
        filters.append("scale=trunc(iw/2)*2:trunc(ih/2)*2")
    if filters:
        args += ["-vf", ",".join(filters)]
    args += ["-frames:v", "1"]
    if target.lower().endswith((".jpg", ".jpeg")):
        args += ["-c:v", "mjpeg", "-q:v", "3", "-pix_fmt", "yuvj420p"]
    elif target.lower().endswith(".png"):
        args += ["-c:v", "png"]
    else:
        args += ["-c:v", "libwebp", "-q:v", "80"]
    args.append(target)
    code, _, stderr = _run_process(args, timeout=60)
    if code != 0:
        if stderr:
            log("ffmpeg: " + stderr.strip()[-800:])
        return False
    if target != output:
        try:
            os.replace(target, output)
        except OSError:
            return False
    return True


def ffmpeg_animated_webp(source, output, size=None, duration=6):
    exe = ffmpeg_exe()
    if not exe:
        return False
    limit = int(size) if size and int(size) > 0 else 320
    # Sin crop: el WebP conserva la proporcion real del video (vertical,
    # panoramico, 16:9...). Solo se limita el lado mayor.
    filter_value = (
        "fps=10,scale=w='min(%d,iw)':h='min(%d,ih)':force_original_aspect_ratio=decrease"
        % (limit, limit)
    )
    code, _, stderr = _run_process(
        [
            exe, "-y", "-i", str(source), "-t", str(duration),
            "-vf", filter_value, "-an", "-loop", "0",
            "-c:v", "libwebp", "-lossless", "0", "-q:v", "75", str(output),
        ],
        timeout=120,
    )
    if code != 0:
        if stderr:
            log("ffmpeg: " + stderr.strip()[-800:])
        return False
    return True


def mime_for_path(path):
    suffix = Path(path).suffix.lower()
    return {
        ".jpg": "image/jpeg",
        ".jpeg": "image/jpeg",
        ".png": "image/png",
        ".webp": "image/webp",
        ".mp4": "video/mp4",
        ".webm": "video/webm",
        ".mov": "video/quicktime",
        ".gif": "image/gif",
    }.get(suffix, "application/octet-stream")


def guess_suffix(url, content_type=None):
    path = urllib.parse.urlparse(url).path
    suffix = Path(path).suffix.lower()
    if suffix in (".jpg", ".jpeg", ".png", ".webp", ".gif", ".mp4", ".webm", ".mov"):
        return suffix
    if content_type:
        content_type = content_type.split(";")[0].strip().lower()
        mapping = {
            "image/jpeg": ".jpg", "image/png": ".png", "image/webp": ".webp",
            "image/gif": ".gif", "video/mp4": ".mp4", "video/webm": ".webm",
            "video/quicktime": ".mov",
        }
        if content_type in mapping:
            return mapping[content_type]
    return ".bin"


def _write_temp(data, suffix):
    handle = tempfile.NamedTemporaryFile(delete=False, suffix=suffix)
    try:
        handle.write(data)
        return handle.name
    finally:
        handle.close()


def save_thumbnail_bytes(data, out, size=None, suffix_hint=".jpg"):
    target = Path(out)
    if target.parent and str(target.parent) not in ("", "."):
        target.parent.mkdir(parents=True, exist_ok=True)
    if not size or int(size) <= 0:
        size = 640
    suffix = guess_suffix(str(target), None) or suffix_hint
    suffix = suffix_hint if suffix == ".bin" else suffix
    source = _write_temp(data, suffix)
    try:
        if suffix in (".jpg", ".jpeg") and not size and data[:3] == b"\xff\xd8\xff":
            shutil.copyfile(source, str(target))
            ok = True
        else:
            ok = ffmpeg_image(source, str(target), size)
            if not ok and suffix in (".jpg", ".jpeg") and _image_suffix(str(target)) in (".jpg", ".jpeg"):
                shutil.copyfile(source, str(target))
                ok = True
    finally:
        _try_delete(source)
    if not ok:
        raise PluginError("NO_THUMB", "no se pudo generar la miniatura")
    mime = mime_for_path(str(target))
    width, height = image_size(str(target))
    return {"path": str(out), "mime": mime, "width": width, "height": height, "animated": False}


def save_thumbnail_url(http, url, out, size=None, cache=None, cache_key=None,
                       ttl=7 * 24 * 3600, referer=None, sucuri=False):
    if cache is not None:
        key = (cache_key or ("thumb-" + hashlib.sha1(url.encode("utf-8")).hexdigest())) + "-nocrop-v2"
        data = cache.get_bytes(key, ttl, lambda: http.get_bytes(url, referer=referer, sucuri=sucuri, timeout=60))
    else:
        data = http.get_bytes(url, referer=referer, sucuri=sucuri, timeout=60)
    suffix = guess_suffix(url)
    return save_thumbnail_bytes(data, out, size, suffix)


def save_frame_url(http, video_url, out, size=None, cache=None, cache_key=None,
                   ttl=7 * 24 * 3600, referer=None, sucuri=False):
    """Extrae un fotograma completo del video, sin recortar."""
    if cache is not None:
        key = (cache_key or ("frame-" + hashlib.sha1(video_url.encode("utf-8")).hexdigest() + ".mp4")) + "-nocrop-v2"
        data = cache.get_bytes(key, ttl, lambda: http.get_bytes(video_url, referer=referer, sucuri=sucuri, timeout=60))
    else:
        data = http.get_bytes(video_url, referer=referer, sucuri=sucuri, timeout=60)
    source = _write_temp(data, guess_suffix(video_url))
    try:
        ok = ffmpeg_image(source, out, size or 640)
    finally:
        _try_delete(source)
    if not ok:
        return None
    width, height = image_size(str(out))
    return {
        "path": str(out),
        "mime": mime_for_path(str(out)),
        "width": width,
        "height": height,
        "animated": False,
    }


def save_animated_thumb(http, video_url, out, size=None, cache=None, cache_key=None,
                        ttl=7 * 24 * 3600, referer=None, sucuri=False):
    if cache is not None:
        key = (cache_key or ("anim-" + hashlib.sha1(video_url.encode("utf-8")).hexdigest() + ".mp4")) + "-nocrop-v2"
        data = cache.get_bytes(key, ttl, lambda: http.get_bytes(video_url, referer=referer, sucuri=sucuri, timeout=60))
    else:
        data = http.get_bytes(video_url, referer=referer, sucuri=sucuri, timeout=60)
    source = _write_temp(data, guess_suffix(video_url))
    try:
        ok = ffmpeg_animated_webp(source, out, size)
    finally:
        _try_delete(source)
    if not ok:
        return None
    width, height = image_size(str(out))
    return {"path": str(out), "mime": "image/webp", "width": width, "height": height, "animated": True}


def thumbnail_response(static_result=None, animated_result=None):
    if animated_result:
        return {
            "ok": True,
            "path": animated_result["path"],
            "mime": animated_result["mime"],
            "width": animated_result["width"],
            "height": animated_result["height"],
            "animated": True,
        }
    if static_result:
        return {
            "ok": True,
            "path": static_result["path"],
            "mime": static_result["mime"],
            "width": static_result["width"],
            "height": static_result["height"],
            "animated": False,
        }
    raise PluginError("NO_THUMB", "sin miniatura disponible")


def fetch_response(result, *, width=None, height=None, duration=None, fps=None,
                   codec=None, probe=False):
    if probe:
        info = probe_video(result["path"])
        if info:
            width = info.get("width") or width
            height = info.get("height") or height
            duration = info.get("durationSec") or duration
            fps = info.get("fps") or fps
            codec = info.get("codec") or codec
    payload = {
        "ok": True,
        "path": result["path"],
        "bytes": result["bytes"],
        "sha256": result["sha256"],
        "contentType": result["contentType"],
    }
    if width:
        payload["width"] = int(width)
    if height:
        payload["height"] = int(height)
    if duration:
        payload["durationSec"] = round(float(duration), 3)
    if fps:
        payload["fps"] = round(float(fps), 3)
    if codec:
        payload["codec"] = codec
    return payload


def parallel_map(function, items, workers=4):
    if not items:
        return []
    results = [None] * len(items)
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(function, item): index for index, item in enumerate(items)}
        for future in concurrent.futures.as_completed(futures):
            index = futures[future]
            try:
                results[index] = future.result()
            except Exception as exc:  # noqa: BLE001
                log("aviso: elemento %s descartado: %s" % (index, exc))
    return results


# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------


def config_path(args, plugin_dir):
    if getattr(args, "config", ""):
        return Path(args.config)
    env = os.environ.get("DNX_PLUGIN_CONFIG")
    if env:
        return Path(env)
    return Path(plugin_dir) / "config.json"


def load_config_values(args, plugin_dir):
    path = config_path(args, plugin_dir)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def save_config_values(args, plugin_dir, values):
    path = config_path(args, plugin_dir)
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        temp = str(path) + ".part"
        Path(temp).write_text(json.dumps(values, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        os.replace(temp, str(path))
    except OSError as exc:
        raise PluginError("INTERNAL", "no se pudo guardar la configuracion: %s" % exc)


def config_values_with_defaults(values, fields):
    result = dict(values)
    for field in fields:
        key = field.get("key")
        if key and key not in result and "default" in field:
            result[key] = field["default"]
    return result


def _coerce_config_value(field, raw):
    field_type = field.get("type", "text")
    if field_type == "number":
        try:
            number = float(raw)
            return int(number) if number.is_integer() else number
        except ValueError:
            raise PluginError("BAD_ARGS", "%s debe ser un numero" % field.get("key"))
    if field_type == "bool":
        return parse_bool(raw)
    if field_type == "select":
        options = field.get("options") or []
        if raw not in options:
            raise PluginError("BAD_ARGS", "%s debe ser uno de %s" % (field.get("key"), options))
        return raw
    return str(raw)


def config_command(args, fields, plugin_dir):
    values = config_values_with_defaults(load_config_values(args, plugin_dir), fields)
    changed = False
    for assignment in getattr(args, "set", []) or []:
        if "=" not in assignment:
            raise PluginError("BAD_ARGS", "--set debe ser clave=valor")
        key, raw = assignment.split("=", 1)
        key = key.strip()
        field = next((item for item in fields if item.get("key") == key), None)
        if field is None:
            raise PluginError("BAD_ARGS", "clave de configuracion desconocida: %s" % key)
        values[key] = _coerce_config_value(field, raw.strip())
        changed = True
    if changed:
        save_config_values(args, plugin_dir, values)
        return {"ok": True, "values": values}
    return {"ok": True, "values": values, "fields": fields}


def config_required(field_key, message):
    raise PluginError("CONFIG_REQUIRED", message, field=field_key)


# ---------------------------------------------------------------------------
# Salud
# ---------------------------------------------------------------------------


def health_payload(started=None, **extra):
    elapsed = 0
    if started is not None:
        elapsed = int((time.perf_counter() - started) * 1000)
    payload = {"ok": True, "version": VERSION, "latencyMs": max(0, elapsed)}
    payload.update(extra)
    return payload
