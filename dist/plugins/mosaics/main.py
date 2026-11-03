import argparse
import json
import os
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))  # ...\dist
DIR = os.path.join(ROOT, 'res', 'mosaics')

VIDEO_EXT = ('.mp4', '.webm', '.mkv', '.mov')
IMAGE_EXT = ('.jpg', '.jpeg', '.png', '.webp', '.gif', '.avif')


def file_url(path):
    return 'http://wpx.internal/__file/' + urllib.parse.quote(
        path.replace('\\', '/'), safe='')


def meta_for(stem):
    p = os.path.join(DIR, stem + '.json')
    try:
        with open(p, 'r', encoding='utf-8') as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return {}


def build():
    items = []
    if not os.path.isdir(DIR):
        return items
    for name in sorted(os.listdir(DIR)):
        ext = os.path.splitext(name)[1].lower()
        if ext not in VIDEO_EXT and ext not in IMAGE_EXT:
            continue
        path = os.path.join(DIR, name)
        if not os.path.isfile(path):
            continue
        stem = os.path.splitext(name)[0]
        meta = meta_for(stem)
        media_type = 'video' if ext in VIDEO_EXT else 'image'
        try:
            size = os.path.getsize(path)
        except OSError:
            continue
        items.append({
            'id': stem,
            'title': meta.get('title') or ('Mosaico ' + stem),
            'mediaType': media_type,
            'media': {'type': media_type, 'url': file_url(path)},
            'externalId': path,
            'animated': media_type == 'video',
            'width': int(meta.get('width') or 0),
            'height': int(meta.get('height') or 0),
            'sizeBytes': size,
            'downloadable': True,
            'tags': 'mosaico',
            'thumbs': {},
            'sourceUrl': '',
        })
    return items


def cmd_list(_args):
    items = build()
    return {'ok': True, 'total': len(items), 'hasMore': False, 'items': items}


def cmd_detail(args):
    for it in build():
        if it['id'] == args.id:
            return {'ok': True, 'item': it}
    return {'ok': False, 'code': 'NOT_FOUND', 'message': 'no existe'}


def cmd_health(_args):
    return {'ok': True, 'status': 'ok', 'version': '1.0.0'}


def main():
    p = argparse.ArgumentParser()
    p.add_argument('command')
    p.add_argument('--id', default='')
    p.add_argument('--out', default='')
    p.add_argument('--config', default='')
    p.add_argument('--cache-dir', default='')
    a, _unknown = p.parse_known_args()
    if a.command == 'list':
        out = cmd_list(a)
    elif a.command == 'detail':
        out = cmd_detail(a)
    elif a.command == 'health':
        out = cmd_health(a)
    else:
        out = {'ok': False, 'code': 'BAD_ARGS', 'message': 'comando desconocido'}
    print(json.dumps(out, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
