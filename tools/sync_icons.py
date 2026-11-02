# -*- coding: utf-8 -*-
"""Genera todos los iconos del producto a partir de resources/*.png.

Fuentes (se pueden reemplazar sin tocar código):
  resources/Catalog.png              -> catálogo / ventana principal / launcher
  resources/Service.png              -> servicio (EXE del motor + bandeja)
  resources/Mobile.png               -> "Adaptar a dispositivo" (device-editor.exe)
  resources/DNXWallpaperv3__android.png -> icono de la app Android
  resources/DNXWallpaperv3_b.png     -> avatar "Acerca de"

Salidas:
  assets/icons/*.ico|png             (iconos embebidos en los EXE al compilar)
  dist/res/app.ico                   (icono de la bandeja en tiempo de ejecución)
  dist/res/icons/about.png           (avatar)
  dist/res/editor/about.png          (avatar del editor web)
  appcompanion/.../res/mipmap-*/...  (iconos Android)
  appcompanion/.../res/drawable-nodpi/about.png

Uso: python tools/sync_icons.py
"""
import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, 'resources')

ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)

# densidad -> (legacy px, foreground px)
ANDROID = {
    'mdpi': (48, 108),
    'hdpi': (72, 162),
    'xhdpi': (96, 216),
    'xxhdpi': (144, 324),
    'xxxhdpi': (192, 432),
}


def load(name):
    p = os.path.join(RES, name)
    if not os.path.exists(p):
        raise SystemExit('falta ' + p)
    return Image.open(p).convert('RGBA')


def square(img, size, pad=0.0):
    """Recorta al centro (cuadrado) y escala a `size`; pad = margen 0..0.5."""
    w, h = img.size
    side = min(w, h)
    img = img.crop(((w - side) // 2, (h - side) // 2,
                    (w + side) // 2, (h + side) // 2))
    inner = max(1, int(round(size * (1.0 - 2 * pad))))
    img = img.resize((inner, inner), Image.LANCZOS)
    if inner == size:
        return img
    canvas = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    off = (size - inner) // 2
    canvas.paste(img, (off, off), img)
    return canvas


def ensure_dir(path):
    os.makedirs(path, exist_ok=True)


def save_png(img, path, size, pad=0.0):
    ensure_dir(os.path.dirname(path))
    square(img, size, pad).save(path, 'PNG')


def save_ico(img, path):
    ensure_dir(os.path.dirname(path))
    img.save(path, format='ICO', sizes=[(s, s) for s in ICO_SIZES])


def main():
    catalog = load('Catalog.png')
    service = load('Service.png')
    mobile = load('Mobile.png')
    # Icono de la app Android: el del dispositivo (Mobile.png).
    android = load('Mobile.png')
    about = load('DNXWallpaperv3_b.png')

    out = os.path.join(ROOT, 'assets', 'icons')
    # --- EXE embebidos (rc) ---
    save_ico(catalog, os.path.join(out, 'launcher.ico'))
    save_png(catalog, os.path.join(out, 'launcher.png'), 256)
    save_ico(catalog, os.path.join(out, 'ui.ico'))
    save_png(catalog, os.path.join(out, 'ui.png'), 256)
    save_ico(service, os.path.join(out, 'motor.ico'))
    save_png(service, os.path.join(out, 'motor.png'), 256)
    save_ico(mobile, os.path.join(out, 'device-editor.ico'))

    de_res = os.path.join(ROOT, 'device-editor-re', 'res')
    ensure_dir(de_res)
    save_ico(mobile, os.path.join(de_res, 'device-editor.ico'))

    # --- bandeja + avatar ---
    dist_res = os.path.join(ROOT, 'dist', 'res')
    save_ico(service, os.path.join(dist_res, 'app.ico'))
    icons = os.path.join(dist_res, 'icons')
    save_png(about, os.path.join(icons, 'about.png'), 512)
    save_png(catalog, os.path.join(icons, 'catalog.png'), 256)
    save_png(service, os.path.join(icons, 'service.png'), 256)
    save_png(mobile, os.path.join(icons, 'mobile.png'), 256)
    save_png(about, os.path.join(dist_res, 'editor', 'about.png'), 256)
    # Icono de la herramienta "Adaptar a dispositivo" (logo + favicon).
    save_png(mobile, os.path.join(dist_res, 'editor', 'mobile.png'), 256)

    # --- Android ---
    ares = os.path.join(ROOT, 'appcompanion', 'DnxCompanion', 'app', 'src',
                        'main', 'res')
    for dens, (legacy, fg) in ANDROID.items():
        d = os.path.join(ares, 'mipmap-' + dens)
        ensure_dir(d)
        save_png(android, os.path.join(d, 'ic_launcher.png'), legacy)
        save_png(android, os.path.join(d, 'ic_launcher_round.png'), legacy)
        # Foreground adaptativo: el icono ocupa el 66% central (72/108 dp).
        save_png(android, os.path.join(d, 'ic_launcher_foreground.png'), fg,
                 pad=0.17)
    save_png(about, os.path.join(ares, 'drawable-nodpi', 'about.png'), 512)

    # --- XML del icono adaptativo ---
    anydpi = os.path.join(ares, 'mipmap-anydpi-v26')
    ensure_dir(anydpi)
    for name in ('ic_launcher.xml', 'ic_launcher_round.xml'):
        with open(os.path.join(anydpi, name), 'w', encoding='utf-8',
                  newline='\n') as f:
            f.write('<?xml version="1.0" encoding="utf-8"?>\n'
                    '<adaptive-icon xmlns:android="http://schemas.android.com/'
                    'apk/res/android">\n'
                    '    <background android:drawable="@color/'
                    'ic_launcher_background" />\n'
                    '    <foreground android:drawable="@mipmap/'
                    'ic_launcher_foreground" />\n'
                    '</adaptive-icon>\n')

    colors = os.path.join(ares, 'values', 'colors.xml')
    ensure_dir(os.path.dirname(colors))
    with open(colors, 'w', encoding='utf-8', newline='\n') as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n'
                '<resources>\n'
                '    <color name="ic_launcher_background">#1E2128</color>\n'
                '</resources>\n')

    print('iconos sincronizados desde resources/')


if __name__ == '__main__':
    main()
