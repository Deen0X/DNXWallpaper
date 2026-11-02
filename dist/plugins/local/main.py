#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Plugin local de DNXWallpaper v3 (contrato v2, sólo biblioteca estándar).

Recorre las carpetas de config.json y sirve los ficheros EN SU SITIO mediante
URLs http://wpx.internal/__file/<ruta codificada>; no copia nada.

Comandos: health, list, detail, config
"""
import argparse
import hashlib
import json
import os
import sys
import urllib.parse

PLUGIN_DIR = os.path.dirname(os.path.abspath(__file__))
VIDEO_EXT = {'.mp4', '.webm', '.mkv', '.mov', '.avi', '.m4v'}
IMAGE_EXT = {'.jpg', '.jpeg', '.png', '.webp', '.bmp', '.gif'}


def out(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=False))
    sys.stdout.flush()


def fail(code, message, **extra):
    payload = {'ok': False, 'code': code, 'message': message}
    payload.update(extra)
    out(payload)
    raise SystemExit(0)


def config_path(args):
    if getattr(args, 'config', ''):
        return args.config
    return os.path.join(PLUGIN_DIR, 'config.json')


def load_folders(args):
    try:
        with open(config_path(args), 'r', encoding='utf-8') as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return []
    folders = data.get('folders') or []
    return [f for f in folders if isinstance(f, str) and os.path.isdir(f)]


def item_id(path):
    digest = hashlib.sha1(path.lower().encode('utf-8')).hexdigest()[:16]
    return 'loc-' + digest


def file_url(path):
    return 'http://wpx.internal/__file/' + urllib.parse.quote(
        path.replace('\\', '/'), safe='')


def build_index(folders):
    items = []
    for root in folders:
        for dirpath, _dirs, files in os.walk(root):
            for name in files:
                ext = os.path.splitext(name)[1].lower()
                if ext in VIDEO_EXT:
                    media_type = 'video'
                elif ext in IMAGE_EXT:
                    media_type = 'image'
                else:
                    continue
                path = os.path.join(dirpath, name)
                try:
                    size = os.path.getsize(path)
                except OSError:
                    continue
                items.append({
                    'id': item_id(path),
                    'title': os.path.splitext(name)[0],
                    'mediaType': media_type,
                    'media': {'type': 'video' if media_type == 'video' else 'image',
                              'url': file_url(path)},
                    'externalId': path,
                    'animated': media_type == 'video',
                    'sizeBytes': size,
                    'downloadable': True,
                    'thumbs': {},
                })
    items.sort(key=lambda it: it['title'].lower())
    return items


def cmd_health(args):
    folders = load_folders(args)
    out({'ok': True, 'configured': bool(folders), 'folders': len(folders)})


def cmd_list(args):
    folders = load_folders(args)
    if not folders:
        fail('CONFIG_REQUIRED', 'no hay carpetas configuradas', field='folders')
    items = build_index(folders)
    query = (getattr(args, 'query', '') or '').strip().lower()
    if query:
        items = [it for it in items if query in it['title'].lower()]
    limit = max(1, min(int(getattr(args, 'limit', 100) or 100), 500))
    page = max(1, int(getattr(args, 'page', 1) or 1))
    start = (page - 1) * limit
    chunk = items[start:start + limit]
    out({'ok': True, 'total': len(items), 'hasMore': start + limit < len(items),
         'limit': limit, 'items': chunk})


def cmd_detail(args):
    folders = load_folders(args)
    want = getattr(args, 'id', '')
    for it in build_index(folders):
        if it['id'] == want:
            out({'ok': True, 'item': it})
            return
    fail('NOT_FOUND', 'no existe el item')


def main():
    # La app lanza el plugin sin consola UTF-8: forzar la codificación evita
    # que un nombre de fichero con caracteres raros rompa la salida JSON.
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass
    parser = argparse.ArgumentParser()
    parser.add_argument('command')
    parser.add_argument('--id', default='')
    parser.add_argument('--out', default='')
    parser.add_argument('--query', default='')
    parser.add_argument('--category', default='')
    parser.add_argument('--tags', default='')
    parser.add_argument('--limit', type=int, default=100)
    parser.add_argument('--page', type=int, default=1)
    parser.add_argument('--sort', default='')
    parser.add_argument('--config', default='')
    args = parser.parse_args()

    if args.command == 'health':
        cmd_health(args)
    elif args.command == 'list':
        cmd_list(args)
    elif args.command == 'detail':
        cmd_detail(args)
    elif args.command == 'thumb':
        fail('NO_THUMB', 'el anfitrion genera la miniatura del fichero')
    elif args.command == 'fetch':
        fail('NOT_SUPPORTED', 'los ficheros se sirven en su sitio')
    else:
        fail('BAD_ARGS', 'comando no soportado: ' + args.command)


if __name__ == '__main__':
    main()
