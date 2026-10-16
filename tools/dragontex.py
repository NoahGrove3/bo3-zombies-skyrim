"""The glider dragon's bond looks (src/Movement.cpp: the Echo Altar's dragon bonds), from Skyrim's own snow dragon skin.
    python tools/dragontex.py <dragon_snow.dds> <out Textures dir>
Writes Textures/CurseOfAlduin/Dragon/: bond2_d.dds (frost: its dark markings turned glacier blue), bond3_d.dds (Alduin's
thrall: black scales, ember-red markings) and glow maps bond1_g / bond2_g / bond3_g (the eyes; from bond II the markings
too, faintly) - the nif's emissive colour lights them (tools/modelconv/dragonvariant.cpp). All mipmapped DXT."""
import io, struct, sys, pathlib
import numpy as np
from PIL import Image

EYE = (1758 / 2048, 1137 / 4096)   # the eye in the skin's UVs (both eyes share it)
EYE_R = 21 / 2048


def dds(img, path, fmt):
    """a full mip chain, each level compressed by Pillow's encoder, under one header"""
    levels, w, h = [], img.width, img.height
    cur = img
    while True:
        b = io.BytesIO()
        cur.save(b, 'DDS', pixel_format=fmt)
        levels.append(b.getvalue()[128:])
        if w == 1 and h == 1 or len(levels) >= 12:
            break
        w, h = max(1, w // 2), max(1, h // 2)
        cur = img.resize((w, h), Image.LANCZOS) if min(w, h) >= 4 else img.resize((max(4, w), max(4, h)), Image.LANCZOS)
        if min(w, h) < 4:
            break
    head = bytearray(open_header(img, fmt))
    struct.pack_into('<I', head, 8, struct.unpack_from('<I', head, 8)[0] | 0x20000)  # DDSD_MIPMAPCOUNT
    struct.pack_into('<I', head, 28, len(levels))
    struct.pack_into('<I', head, 108, 0x1000 | 0x8 | 0x400000)                    # texture | complex | mipmap
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(head) + b''.join(levels))
    print(path, img.size, fmt, len(levels), 'mips')


def open_header(img, fmt):
    b = io.BytesIO()
    img.save(b, 'DDS', pixel_format=fmt)
    return b.getvalue()[:128]


def main():
    src, out = sys.argv[1], pathlib.Path(sys.argv[2]) / 'CurseOfAlduin' / 'Dragon'
    im = Image.open(src).convert('RGBA').resize((1024, 2048), Image.LANCZOS)
    a = np.asarray(im).astype(np.float32) / 255
    rgb, alpha = a[..., :3], a[..., 3:]
    L = rgb @ np.array([0.299, 0.587, 0.114], np.float32)
    # the markings: the skin's dark, blue-black pattern (darker than the scales round them, and bluer)
    from PIL import ImageFilter
    Li = Image.fromarray((L * 255).astype(np.uint8), 'L')
    local = np.asarray(Li.filter(ImageFilter.GaussianBlur(10))).astype(np.float32) / 255
    blue = np.clip((rgb[..., 2] - rgb[..., 0]) * 8 + 0.2, 0, 1)
    valid = (L > 0.03) * (local > 0.12)
    hp = np.clip((local - L - 0.02) * 10, 0, 1) * np.clip((0.45 - L) * 5, 0, 1)
    dark = np.clip((0.34 - L) * 5, 0, 1) * blue
    mark = np.clip(np.maximum(hp * 1.6, dark * 0.9), 0, 1) * valid
    h, w = L.shape
    yy, xx = np.mgrid[0:h, 0:w]
    eye = np.clip(1.3 - np.hypot(xx / w - EYE[0], (yy / h - EYE[1]) * h / w) / EYE_R, 0, 1)

    # bond II: glacier - markings a bright cold blue, the scales a touch paler
    t2 = rgb * 0.45 + L[..., None] * np.array([0.80, 0.93, 1.10], np.float32) * 0.6 + 0.02
    ice = np.array([0.42, 0.72, 1.0], np.float32)
    t2 = t2 * (1 - mark[..., None]) + (ice * (0.35 + 0.65 * L[..., None] * 2.2)) * mark[..., None]
    # bond III: Alduin's thrall - black scales, the markings ember red
    t3 = (L[..., None] ** 1.3) * np.array([0.34, 0.30, 0.31], np.float32)
    ember = np.array([0.62, 0.14, 0.06], np.float32)
    t3 = t3 * (1 - mark[..., None]) + ember * (0.45 + 0.55 * L[..., None] * 2) * mark[..., None]
    for name, t in (('bond2_d', t2), ('bond3_d', t3)):
        t = np.clip(t, 0, 1)
        t = t * (1 - eye[..., None] * 0.6) + eye[..., None] * 0.6 * rgb  # (the eye keeps its own look under the glow)
        img = Image.fromarray((np.concatenate([t, alpha], -1) * 255 + 0.5).astype(np.uint8), 'RGBA')
        dds(img, out / f'{name}.dds', 'DXT5')
    # glow maps (half size): the eyes; from bond II the markings faintly
    for name, k in (('bond1_g', 0.0), ('bond2_g', 0.10), ('bond3_g', 0.12)):
        g = np.clip(np.maximum(eye, mark * k), 0, 1)
        img = Image.fromarray((g * 255 + 0.5).astype(np.uint8), 'L').convert('RGB').resize((512, 1024), Image.LANCZOS)
        dds(img, out / f'{name}.dds', 'DXT1')
    # previews
    for name, t in (('bond2', t2), ('bond3', t3)):
        Image.fromarray((np.clip(t, 0, 1) * 255).astype(np.uint8)).resize((512, 1024)).save(pathlib.Path(sys.argv[2]) / f'preview_{name}.png')


main()
