import sys, json, pathlib, io
from PIL import Image, ImageFilter, ImageChops
from playwright.sync_api import sync_playwright
HERE = pathlib.Path(__file__).resolve().parent
BUILD = HERE.parent.parent / 'build' / 'preview'
OUT = BUILD / 'out'; OUT.mkdir(parents=True, exist_ok=True)
keys = sys.argv[1:]
N, FPS = int(__import__('os').environ.get('NF', 24)), 12
shared = (BUILD / 'data' / 'shared.json').read_text()
with sync_playwright() as pw:
    b = pw.chromium.launch(args=['--use-gl=swiftshader', '--enable-webgl', '--ignore-gpu-blocklist'])
    pg = b.new_page(viewport={'width': 880, 'height': 440})
    pg.route('https://cdnjs.cloudflare.com/**', lambda r: r.fulfill(path=str(pathlib.Path(__import__('os').environ.get('THREE_JS', '/tmp/three_r128/build/three.min.js'))), content_type='application/javascript'))
    errs = []; pg.on('console', lambda m: m.type == 'error' and errs.append(m.text)); pg.on('pageerror', lambda e: errs.append(str(e)))
    pg.goto('file://' + str(HERE / 'render.html'))
    pg.wait_for_function('() => window.setup')
    pg.evaluate('s => { window.__shared = JSON.parse(s); }', shared)
    for k in keys:
        d = (BUILD / 'data' / f'{k}.json').read_text()
        pg.evaluate('d => window.setup(JSON.parse(d), window.__shared)', d)
        frames = []
        for i in range(N):
            pg.evaluate(f'window.frame({i / FPS + 0.7}, false)')
            full = Image.open(io.BytesIO(pg.locator('#c').screenshot())).convert('RGB')
            pg.evaluate(f'window.frame({i / FPS + 0.7}, true)')
            fxl = Image.open(io.BytesIO(pg.locator('#c').screenshot())).convert('RGB')
            glow = fxl.filter(ImageFilter.GaussianBlur(9)).point(lambda v: min(255, int(v * 1.6)))
            glow2 = fxl.filter(ImageFilter.GaussianBlur(3)).point(lambda v: min(255, int(v * 0.6)))
            im = ImageChops.add(ImageChops.add(full, glow), glow2)
            frames.append(im.resize((520, 260), Image.LANCZOS))
        frames[0].save(OUT / f'{k}.png')
        frames[0].save(OUT / f'{k}.webp', save_all=True, append_images=frames[1:], duration=int(1000 / FPS), loop=0, quality=72, method=4)
        print(k, (OUT / f'{k}.webp').stat().st_size // 1024, 'KB', errs[-3:] if errs else '')
    b.close()
