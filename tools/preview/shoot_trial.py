import sys, io, os, pathlib
from PIL import Image, ImageFilter, ImageChops
from playwright.sync_api import sync_playwright
HERE = pathlib.Path(__file__).resolve().parent; BUILD = HERE.parent.parent / 'build' / 'preview'; OUT = BUILD / 'out'; OUT.mkdir(parents=True, exist_ok=True)
mode = sys.argv[1]   # wide | close
T0, T1, FPS = (0.0, 8.0, 10) if mode == 'wide' else (1.05, 3.0, 15)
if os.environ.get('AT'): T0 = float(os.environ['AT'])
if os.environ.get('ONE'): T1 = T0 + float(os.environ['ONE'])
size = (720, 405) if mode == 'wide' else (640, 360)
with sync_playwright() as pw:
    b = pw.chromium.launch(args=['--use-gl=swiftshader', '--enable-webgl', '--ignore-gpu-blocklist'])
    pg = b.new_page(viewport={'width': 1280, 'height': 720})
    pg.route('https://cdnjs.cloudflare.com/**', lambda r: r.fulfill(path=str(pathlib.Path(__import__('os').environ.get('THREE_JS', '/tmp/three_r128/build/three.min.js'))), content_type='application/javascript'))
    errs = []; pg.on('pageerror', lambda e: errs.append(str(e))); pg.on('console', lambda m: m.type == 'error' and errs.append(m.text))
    pg.goto('file://' + str(HERE / 'trial.html')); pg.wait_for_function('() => window.setup')
    pg.evaluate('d => window.setup(JSON.parse(d))', (BUILD / 'data' / 'trial.json').read_text())
    if mode == 'close': pg.evaluate('window.CLOSE = true')
    frames = []; t = T0
    while t < T1 - 1e-6:
        pg.evaluate(f'window.frame({t}, false)'); full = Image.open(io.BytesIO(pg.locator('#c').screenshot())).convert('RGB')
        pg.evaluate(f'window.frame({t}, true)'); fxl = Image.open(io.BytesIO(pg.locator('#c').screenshot())).convert('RGB')
        glow = fxl.filter(ImageFilter.GaussianBlur(14)).point(lambda v: min(255, int(v * 1.5)))
        glow2 = fxl.filter(ImageFilter.GaussianBlur(4)).point(lambda v: min(255, int(v * 0.5)))
        frames.append(ImageChops.add(ImageChops.add(full, glow), glow2).resize(size, Image.LANCZOS)); t += 1 / FPS
    frames[len(frames) // 2].save(OUT / f'trial_{mode}.png')
    frames[0].save(OUT / f'trial_{mode}.webp', save_all=True, append_images=frames[1:], duration=int(1000 / FPS), loop=0, quality=70, method=4)
    print(mode, len(frames), (OUT / f'trial_{mode}.webp').stat().st_size // 1024, 'KB', errs[:3])
    b.close()
