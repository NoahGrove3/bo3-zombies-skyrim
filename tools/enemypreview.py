"""(Noah, 2026-11-26: "an image preview of each enemy from their models and a short description of what each enemy does") each of
the night's enemies drawn from its own models, read from Noah's game: its NPC record followed through its templates to its race,
skin and outfit (the device-side resolver, build/enemies/raw/res.json), each piece's mesh posed by its bones (tools/modelconv/
nifworld) with its diffuse texture, rendered in a headless three.js and saved as a small picture for the workshop doc.

Reads build/enemies/raw (nifs) and build/enemies/tex (diffuse PNGs), never committed; writes build/preview/enemies/<id>.jpg.

    python3 tools/enemypreview.py [ids...]
"""
import json, pathlib, subprocess, sys, base64, io
import numpy as np
ROOT = pathlib.Path(__file__).resolve().parent.parent
E = ROOT / 'build' / 'enemies'
OUT = ROOT / 'build' / 'preview' / 'enemies'; OUT.mkdir(parents=True, exist_ok=True)
Z = ROOT / 'build' / 'zombies'
TB = {'tb_mangled': 'zombie01.nif', 'tb_foul': 'zombie02.nif', 'tb_putrid': 'zombie03putrid.nif', 'tb_malignant': 'zombie02dread.nif',
      'tb_relentless': 'zombie03.nif', 'tb_dread_putrid': 'zombie01putrid.nif'}


def read(nif):
    out = subprocess.run(['/home/claude/tools/nifworld', str(nif)], capture_output=True, text=True).stdout
    shapes, cur = [], None
    for l in out.splitlines():
        if l.startswith('shape '):
            p = l[6:].split('\t'); cur = {'name': p[0], 'tex': p[1] if len(p) > 1 else '', 'kind': p[2] if len(p) > 2 else 'lit', 'alpha': p[3] == '1' if len(p) > 3 else False, 'v': [], 't': []}
            shapes.append(cur)
        elif l.startswith('v ') and cur is not None: cur['v'].append([float(x) for x in l[2:].split()])
        elif l.startswith('t ') and cur is not None: cur['t'].append([int(x) for x in l[2:].split()[:3]])
    return [s for s in shapes if s['kind'] == 'lit' and s['v'] and s['t']]


def tex_file(t):
    k = t.lower().replace('/', '\\')
    if not k.startswith('textures\\'): k = 'textures\\' + k
    p = E / 'tex' / (k[:-4].replace('\\', '__') + '.png')
    if p.exists(): return p
    nm = k.split('\\')[-1][:-4]
    for d in (Z / 'tex',):
        for e in ('.png', '.jpg'):
            if (d / (nm + e)).exists(): return d / (nm + e)
    return None


def scene(eid, res):
    shapes = []
    if eid in TB: nifs = [Z / 'nif' / TB[eid]]
    else:
        r = res.get(eid) or {}
        nifs = [E / 'raw' / ('meshes\\' + p).lower().replace('\\', '__') for p in r.get('parts', [])]
        if r.get('face'): nifs.append(E / 'raw' / r['face'].lower().replace('\\', '__'))
    for n in nifs:
        if not n.exists(): continue
        for s in read(n):
            if any(w in s['name'].lower() for w in ('fx', 'glow', 'mist')) and not s['tex']: continue
            tf = tex_file(s['tex']) if s['tex'] else None
            V = np.array(s['v'], np.float32)
            shapes.append({'p': V[:, :3].round(2).flatten().tolist(), 'uv': V[:, 3:5].round(4).flatten().tolist(), 'i': np.array(s['t']).flatten().tolist(),
                           'tex': ('data:image/png;base64,' + base64.b64encode(tf.read_bytes()).decode()) if tf else '', 'alpha': s['alpha']})
    return shapes


HTML = """<!doctype html><meta charset=utf-8><body style="margin:0;background:#0d1016"><script src="three.min.js"></script><script>
const R=new THREE.WebGLRenderer({antialias:true,preserveDrawingBuffer:true}); R.setSize(640,640); R.setPixelRatio(1); R.outputEncoding=THREE.sRGBEncoding; document.body.appendChild(R.domElement);
window.draw=async function(shapes){
 const sc=new THREE.Scene(); sc.background=new THREE.Color(0x0d1016);
 sc.add(new THREE.HemisphereLight(0xcfd8ff,0x241c14,0.85)); const k=new THREE.DirectionalLight(0xfff0dc,1.15); k.position.set(0.6,1.4,0.9); sc.add(k);
 const r=new THREE.DirectionalLight(0x8fb0ff,0.55); r.position.set(-1,-0.8,0.6); sc.add(r);
 const g=new THREE.Group(); const loads=[];
 for(const s of shapes){ const geo=new THREE.BufferGeometry(); geo.setAttribute('position',new THREE.Float32BufferAttribute(s.p,3)); geo.setAttribute('uv',new THREE.Float32BufferAttribute(s.uv,2)); geo.setIndex(s.i); geo.computeVertexNormals();
  const m=new THREE.MeshStandardMaterial({color:0x9a9590,roughness:0.8,metalness:0.05,side:THREE.DoubleSide,alphaTest:s.alpha?0.4:0});
  if(s.tex) loads.push(new Promise(res=>new THREE.TextureLoader().load(s.tex,t=>{t.flipY=false;t.encoding=THREE.sRGBEncoding;m.map=t;m.color.set(0xffffff);m.transparent=false;m.needsUpdate=true;res();},undefined,()=>res())));
  g.add(new THREE.Mesh(geo,m)); }
 await Promise.all(loads); g.rotation.x=-Math.PI/2; sc.add(g);
 const b=new THREE.Box3().setFromObject(g), c=b.getCenter(new THREE.Vector3()), sz=b.getSize(new THREE.Vector3());
 const cam=new THREE.PerspectiveCamera(30,1,1,100000); const d=Math.max(sz.y,sz.x*0.9,sz.z*0.9)/ (2*Math.tan(15*Math.PI/180))*(sz.x>sz.y*2.5?0.8:1.05);
 cam.position.set(c.x+d*0.42,c.y+sz.y*0.06,c.z-d*0.91); cam.lookAt(c); R.render(sc,cam); return [sz.x,sz.y,sz.z];
};</script>"""


def main():
    res = json.load(open(E / 'raw' / 'res.json'))
    ids = sys.argv[1:] or list(json.load(open(ROOT / 'sheets' / 'enemies.json'))['rows'][i]['id'] for i in range(len(json.load(open(ROOT / 'sheets' / 'enemies.json'))['rows'])))
    site = E / 'site'; site.mkdir(exist_ok=True)
    (site / 'r.html').write_text(HTML); (site / 'three.min.js').write_bytes(pathlib.Path('/tmp/three_r128/build/three.min.js').read_bytes())
    from playwright.sync_api import sync_playwright
    with sync_playwright() as pw:
        b = pw.chromium.launch(args=['--use-gl=swiftshader', '--enable-webgl', '--ignore-gpu-blocklist'])
        pg = b.new_page(viewport={'width': 640, 'height': 640}); pg.goto((site / 'r.html').as_uri()); pg.wait_for_function('() => !!window.draw')
        for eid in ids:
            sh = scene(eid, res)
            if not sh: print(eid, 'no model'); continue
            try: sz = pg.evaluate('s => draw(s)', sh)
            except Exception as e: print(eid, 'failed', str(e)[:80]); continue
            pg.locator('canvas').screenshot(path=str(OUT / f'{eid}.png'))
            from PIL import Image
            Image.open(OUT / f'{eid}.png').convert('RGB').resize((320, 320), Image.LANCZOS).save(OUT / f'{eid}.jpg', quality=84); (OUT / f'{eid}.png').unlink()
            print(eid, len(sh), 'shapes', [round(x) for x in sz])
        b.close()


if __name__ == '__main__': main()
