"""Genera iconos editables (PNG 256 + ICO) para cada ejecutable.

Uso:
  python tools/make-icons.py          # crea los PNG si no existen y los ICO
  (edita assets/icons/*.png y vuelve a ejecutar para regenerar los .ico)

Los ICO incrustan el PNG tal cual (soportado desde Vista), por lo que se
puede editar el PNG con cualquier editor y regenerar el ICO al instante.
"""
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICONS = os.path.join(ROOT, 'assets', 'icons')
os.makedirs(ICONS, exist_ok=True)

SIZE = 256


def write_png(path, pixels):
    raw = bytearray()
    for y in range(SIZE):
        raw.append(0)
        raw += pixels[y]

    def chunk(typ, data):
        c = struct.pack('>I', len(data)) + typ + data
        return c + struct.pack('>I', zlib.crc32(typ + data) & 0xFFFFFFFF)

    png = (b'\x89PNG\r\n\x1a\n' +
           chunk(b'IHDR', struct.pack('>IIBBBBB', SIZE, SIZE, 8, 2, 0, 0, 0)) +
           chunk(b'IDAT', zlib.compress(bytes(raw), 9)) +
           chunk(b'IEND', b''))
    with open(path, 'wb') as f:
        f.write(png)


def gradient(c1, c2, angle=1.0):
    rows = []
    for y in range(SIZE):
        t = (y / (SIZE - 1)) ** angle
        row = bytearray()
        for x in range(SIZE):
            # leve variación diagonal
            tt = min(1.0, max(0.0, t * 0.85 + (x / (SIZE - 1)) * 0.15))
            row += bytes([int(c1[0] + (c2[0] - c1[0]) * tt),
                          int(c1[1] + (c2[1] - c1[1]) * tt),
                          int(c1[2] + (c2[2] - c1[2]) * tt)])
        rows.append(row)
    return rows


def rounded(rows, radius=40, inset=8):
    for y in range(SIZE):
        for x in range(SIZE):
            # esquinas redondeadas -> color de fondo transparente simulado
            cx = min(x, SIZE - 1 - x)
            cy = min(y, SIZE - 1 - y)
            if cx < radius and cy < radius:
                dx = radius - cx
                dy = radius - cy
                if dx * dx + dy * dy > radius * radius:
                    i = x * 3
                    rows[y][i:i + 3] = b'\x14\x14\x18'
    return rows


def rect(rows, x0, y0, x1, y1, color):
    for y in range(max(0, y0), min(SIZE, y1)):
        for x in range(max(0, x0), min(SIZE, x1)):
            i = x * 3
            rows[y][i:i + 3] = bytes(color)


def triangle_play(rows, cx, cy, half):
    for dy in range(-half, half + 1):
        w = int((dy + half) * 0.9)
        y = cy + dy
        if 0 <= y < SIZE:
            rect(rows, cx - w // 2, y, cx + w // 2, y + 1, (255, 255, 255))


def draw_launcher():
    rows = rounded(gradient((22, 22, 26), (60, 60, 70)))
    # cohete simplificado: triángulo naranja + base
    for i in range(120):
        rect(rows, 128 - i // 3, 190 - i, 128 + i // 3, 190 - i + 1, (232, 90, 16))
    rect(rows, 100, 190, 156, 200, (232, 90, 16))
    rect(rows, 112, 200, 144, 206, (255, 170, 90))
    return rows


def draw_motor():
    rows = rounded(gradient((10, 30, 60), (30, 110, 190)))
    triangle_play(rows, 118, 128, 70)
    return rows


def draw_ui():
    rows = rounded(gradient((40, 10, 60), (120, 30, 140)))
    rect(rows, 60, 70, 196, 186, (250, 250, 255))
    rect(rows, 60, 70, 196, 96, (232, 90, 16))
    for y in (110, 138, 166):
        rect(rows, 74, y, 182, y + 10, (170, 170, 190))
    return rows


def ico_from_png(png_path, ico_path):
    with open(png_path, 'rb') as f:
        png = f.read()
    header = struct.pack('<HHH', 0, 1, 1)
    entry = struct.pack('<BBBBHHII', 0, 0, 0, 0, 1, 32, len(png), 6 + 16)
    with open(ico_path, 'wb') as f:
        f.write(header + entry + png)


APPS = {
    'launcher': draw_launcher,
    'motor': draw_motor,
    'ui': draw_ui,
}

for name, fn in APPS.items():
    png = os.path.join(ICONS, name + '.png')
    ico = os.path.join(ICONS, name + '.ico')
    if not os.path.exists(png):
        write_png(png, fn())
        print('creado', png)
    ico_from_png(png, ico)
    print('generado', ico)
