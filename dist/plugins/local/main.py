#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Plugin local de DNXWallpaper v3 (contrato v2, solo biblioteca estandar).

Recorre las carpetas de config.json y sirve los ficheros EN SU SITIO mediante
URLs http://wpx.internal/__file/<ruta codificada>; no copia nada.

Comandos: health, list, detail, config y serve (protocolo 3).
"""
import argparse
import hashlib
import json
import os
import sys
import traceback
import urllib.parse

PLUGIN_DIR = os.path.dirname(os.path.abspath(__file__))
VIDEO_EXT = {'.mp4', '.webm', '.mkv', '.mov', '.avi', '.m4v'}
IMAGE_EXT = {'.jpg', '.jpeg', '.png', '.webp', '.bmp', '.gif'}


class LocalError(Exception):
    def __init__(self, code, message, **extra):
        super().__init__(message)
        self.code = code
        self.message = message
        self.extra = extra


def out(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=False, separators=(',', ':')))
    sys.stdout.write('\n')
    sys.stdout.flush()


def out_with_id(request_id, payload):
    body = dict(payload) if isinstance(payload, dict) else {'ok': True}
    body.pop('id', None)
    response = {'id': request_id}
    response.update(body)
    out(response)


def error_payload(exc):
    payload = {'ok': False, 'code': exc.code, 'message': exc.message}
    payload.update(exc.extra)
    return payload


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


def payload_health(args):
    folders = load_folders(args)
    return {'ok': True, 'configured': bool(folders), 'folders': len(folders)}


def payload_list(args):
    folders = load_folders(args)
    if not folders:
        raise LocalError('CONFIG_REQUIRED', 'no hay carpetas configuradas',
                         field='folders')
    items = build_index(folders)
    query = (getattr(args, 'query', '') or '').strip().lower()
    if query:
        items = [it for it in items if query in it['title'].lower()]
    limit = max(1, min(int(getattr(args, 'limit', 100) or 100), 500))
    page = max(1, int(getattr(args, 'page', 1) or 1))
    start = (page - 1) * limit
    chunk = items[start:start + limit]
    return {'ok': True, 'total': len(items),
            'hasMore': start + limit < len(items),
            'limit': limit, 'items': chunk}


def payload_detail(args):
    folders = load_folders(args)
    want = getattr(args, 'id', '')
    for it in build_index(folders):
        if it['id'] == want:
            return {'ok': True, 'item': it}
    raise LocalError('NOT_FOUND', 'no existe el item')


def dispatch(args):
    command = args.command
    if command == 'health':
        return payload_health(args)
    if command == 'list':
        return payload_list(args)
    if command == 'detail':
        return payload_detail(args)
    if command == 'thumb':
        raise LocalError('NO_THUMB',
                         'el anfitrion genera la miniatura del fichero')
    if command == 'fetch':
        raise LocalError('NOT_SUPPORTED',
                         'los ficheros se sirven en su sitio')
    raise LocalError('BAD_ARGS', 'comando no soportado: ' + command)


class LocalParser(argparse.ArgumentParser):
    def error(self, message):
        raise LocalError('BAD_ARGS', message)

    def exit(self, status=0, message=None):
        if status == 0:
            raise SystemExit(0)
        raise LocalError('BAD_ARGS', message or 'argumentos invalidos')


def build_parser():
    parser = LocalParser(add_help=False)
    parser.add_argument('command')
    parser.add_argument('--id', default='')
    parser.add_argument('--out', default='')
    parser.add_argument('--query', default='')
    parser.add_argument('--category', default='')
    parser.add_argument('--tags', default='')
    parser.add_argument('--limit', type=int, default=100)
    parser.add_argument('--page', type=int, default=1)
    parser.add_argument('--sort', default='')
    parser.add_argument('--cursor', default='')
    parser.add_argument('--kind', default='')
    parser.add_argument('--size', type=int, default=None)
    parser.add_argument('--max-bytes', type=int, default=None)
    parser.add_argument('--min-rating', type=int, default=0)
    parser.add_argument('--since', default='')
    parser.add_argument('--until', default='')
    parser.add_argument('--animated', action='store_true')
    parser.add_argument('--config', default='')
    parser.add_argument('--cache-dir', default='')
    return parser


def request_argv(cmd, args_obj, serve_args):
    argv = [cmd]
    provided = set()
    for raw_key, value in (args_obj or {}).items():
        key = str(raw_key).replace('_', '-')
        provided.add(key.lower())
        if value is None:
            continue
        flag = '--' + key
        if key == 'animated':
            if isinstance(value, str):
                low = value.strip().lower()
                value = low in ('1', 'true', 'yes', 'y', 'on', 'si')
            if isinstance(value, bool):
                if value:
                    argv.append(flag)
                continue
        if isinstance(value, bool):
            if value:
                argv.append(flag)
            continue
        if isinstance(value, (list, tuple)):
            for item in value:
                if item is not None:
                    argv.extend([flag, str(item)])
            continue
        argv.extend([flag, str(value)])

    if getattr(serve_args, 'config', '') and 'config' not in provided:
        argv.extend(['--config', serve_args.config])
    if getattr(serve_args, 'cache_dir', '') and 'cache-dir' not in provided:
        argv.extend(['--cache-dir', serve_args.cache_dir])
    return argv


def serve(parser, serve_args):
    try:
        sys.stdin.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

    out({'id': 0, 'ok': True, 'event': 'ready', 'protocol': 3})

    for raw in sys.stdin:
        line = raw.strip()
        try:
            request = json.loads(line)
        except Exception:
            out_with_id(None, {'ok': False, 'code': 'BAD_ARGS',
                               'message': 'linea invalida'})
            continue
        if not isinstance(request, dict):
            out_with_id(None, {'ok': False, 'code': 'BAD_ARGS',
                               'message': 'peticion invalida'})
            continue

        request_id = request.get('id')
        cmd = request.get('cmd')
        if cmd == 'shutdown':
            out_with_id(request_id, {'ok': True})
            return 0
        if not isinstance(cmd, str) or not cmd:
            out_with_id(request_id, {'ok': False, 'code': 'BAD_ARGS',
                                     'message': 'falta cmd'})
            continue
        args_obj = request.get('args') or {}
        if not isinstance(args_obj, dict):
            out_with_id(request_id, {'ok': False, 'code': 'BAD_ARGS',
                                     'message': 'args debe ser un objeto'})
            continue

        try:
            argv = request_argv(cmd, args_obj, serve_args)
            cmd_args = parser.parse_args(argv)
            payload = dispatch(cmd_args)
        except LocalError as exc:
            payload = error_payload(exc)
        except SystemExit:
            payload = {'ok': False, 'code': 'BAD_ARGS',
                       'message': 'argumentos invalidos'}
        except Exception as exc:
            print(traceback.format_exc(), file=sys.stderr, flush=True)
            payload = {'ok': False, 'code': 'INTERNAL',
                       'message': str(exc) or exc.__class__.__name__}
        out_with_id(request_id, payload)
    return 0


def main():
    # La app lanza el plugin sin consola UTF-8: forzar la codificacion evita
    # que un nombre de fichero con caracteres raros rompa la salida JSON.
    try:
        sys.stdout.reconfigure(encoding='utf-8')
        sys.stderr.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

    parser = build_parser()
    args = parser.parse_args()
    if args.command == 'serve':
        return serve(parser, args)

    try:
        payload = dispatch(args)
    except LocalError as exc:
        payload = error_payload(exc)
    except Exception as exc:
        print(traceback.format_exc(), file=sys.stderr, flush=True)
        payload = {'ok': False, 'code': 'INTERNAL',
                   'message': str(exc) or exc.__class__.__name__}
        out(payload)
        return 1
    out(payload)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
