// The workshop's 3D weapon viewer (taken from the old workshop's app.js: one WebGL context drawing every view in turn, each
// weapon's model and its effect art in any look and colour). Needs three.js r128 loaded before it.
const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
function hexrgb(h) { h = h.replace('#', ''); return [0, 2, 4].map(i => parseInt(h.substr(i, 2), 16) / 255); }
const COLS = {"white": "#eaf1ff", "blue": "#4f8bff", "iceblue": "#8fd8ff", "orange": "#ff7a1f", "red": "#e0262b", "green": "#48d058", "poison": "#9be03a", "drain": "#6e5bff", "purple": "#a54bff", "moon": "#c9d4e8", "fear": "#c0182a", "gold": "#ffc54a", "ss_none": "#ff4a32", "arcane": "#a8c4ff", "shockv": "#9f8cff", "shadow": "#9a3bff", "carian": "#4682ff"};
const SCYTHES = ['priest_vokun', 'tab_silverscythe', 'tab_bonereaper', 'tab_wyrmtooth', 'tab_bloodmoon', 'cls_forsworn_alch', 'cls_falmer_alch', 'cls_dwarven_alch', 'cls_dragonpriest_alch'];
// ================================================================ the renderer: one WebGL context drawing every live view in turn
const GL = (function () {
  const W = 720, H = 360;
  let R = null, ok = false;
  const glc = document.createElement('canvas');
  try {
    R = new THREE.WebGLRenderer({ canvas: glc, antialias: true, preserveDrawingBuffer: true });
    R.setPixelRatio(1); R.setSize(W, H, false); R.outputEncoding = THREE.sRGBEncoding;
    R.toneMapping = THREE.ACESFilmicToneMapping; R.toneMappingExposure = 1.05; R.autoClear = false;
    ok = true;
  } catch (e) { ok = false; }
  const views = new Set();
  // bloom: the effects alone, blurred at half and quarter size, added back over the picture (Skyrim's own glow, roughly)
  let rt = null, quad = null, qcam = null, blurM = null, addM = null;
  if (ok) {
    const mk = (w, h) => new THREE.WebGLRenderTarget(w, h, { minFilter: THREE.LinearFilter, magFilter: THREE.LinearFilter, type: THREE.HalfFloatType });
    rt = { a: mk(W / 2, H / 2), b: mk(W / 2, H / 2), c: mk(W / 4, H / 4), d: mk(W / 4, H / 4) };
    qcam = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);
    blurM = new THREE.ShaderMaterial({ uniforms: { t: { value: null }, dir: { value: new THREE.Vector2() } }, depthTest: false, depthWrite: false,
      vertexShader: 'varying vec2 u; void main(){ u = uv; gl_Position = vec4(position.xy, 0., 1.); }',
      fragmentShader: `uniform sampler2D t; uniform vec2 dir; varying vec2 u; void main(){ vec4 c = texture2D(t, u) * 0.2270270270;
        c += (texture2D(t, u + dir * 1.3846153846) + texture2D(t, u - dir * 1.3846153846)) * 0.3162162162;
        c += (texture2D(t, u + dir * 3.2307692308) + texture2D(t, u - dir * 3.2307692308)) * 0.0702702703; gl_FragColor = c; }` });
    addM = new THREE.ShaderMaterial({ uniforms: { a: { value: null }, c: { value: null }, k: { value: 1 } }, depthTest: false, depthWrite: false, transparent: true,
      blending: THREE.AdditiveBlending,
      vertexShader: 'varying vec2 u; void main(){ u = uv; gl_Position = vec4(position.xy, 0., 1.); }',
      fragmentShader: 'uniform sampler2D a, c; uniform float k; varying vec2 u; void main(){ vec3 g = texture2D(a, u).rgb * 0.6 + texture2D(c, u).rgb * 0.9; gl_FragColor = vec4(g * k, 1.); }' });
    quad = new THREE.Mesh(new THREE.PlaneGeometry(2, 2), blurM); quad.frustumCulled = false;
  }
  function pass(m, target) { quad.material = m; R.setRenderTarget(target); R.clear(); R.render(quad, qcam); }
  function draw(v) {
    const { scene, cam } = v;
    R.setRenderTarget(null); R.setClearColor(0x05070a, 1); R.clear();
    cam.layers.mask = 3; R.render(scene, cam);
    const bg = scene.background, fog = scene.fog; scene.background = null; scene.fog = null;
    R.setClearColor(0x000000, 1); cam.layers.mask = 2;
    R.setRenderTarget(rt.a); R.clear(); R.render(scene, cam);
    scene.background = bg; scene.fog = fog; cam.layers.mask = 3;
    blurM.uniforms.t.value = rt.a.texture; blurM.uniforms.dir.value.set(1.4 / (W / 2), 0); pass(blurM, rt.b);
    blurM.uniforms.t.value = rt.b.texture; blurM.uniforms.dir.value.set(0, 1.4 / (H / 2)); pass(blurM, rt.a);
    blurM.uniforms.t.value = rt.a.texture; blurM.uniforms.dir.value.set(2.2 / (W / 4), 0); pass(blurM, rt.c);
    blurM.uniforms.t.value = rt.c.texture; blurM.uniforms.dir.value.set(0, 2.2 / (H / 4)); pass(blurM, rt.d);
    blurM.uniforms.t.value = rt.d.texture; blurM.uniforms.dir.value.set(3.0 / (W / 4), 0); pass(blurM, rt.c);
    blurM.uniforms.t.value = rt.c.texture; blurM.uniforms.dir.value.set(0, 3.0 / (H / 4)); pass(blurM, rt.d);
    addM.uniforms.a.value = rt.a.texture; addM.uniforms.c.value = rt.d.texture; addM.uniforms.k.value = v.bloom == null ? 1 : v.bloom;
    quad.material = addM; R.setRenderTarget(null); R.render(quad, qcam);
    const ctx = v.ctx || (v.ctx = v.cv.getContext('2d'));
    ctx.drawImage(glc, 0, 0, v.cv.width, v.cv.height);
  }
  const io = 'IntersectionObserver' in window ? new IntersectionObserver(es => es.forEach(e => { const v = e.target.__view; if (v) v.visible = e.isIntersecting; }), { rootMargin: '80px' }) : null;
  let last = 0; const t0 = performance.now();
  function loop(now) {
    requestAnimationFrame(loop);
    if (now - last < 1000 / 30 - 2) return;   // (30 frames a second)
    last = now;
    const t = (now - t0) / 1000;
    let n = 0;
    for (const v of views) {
      if (!v.visible || !v.ready || document.hidden) continue;
      if (++n > 10) break;
      try { v.tick(t); draw(v); } catch (e) { console.error(e); v.ready = false; }
    }
  }
  if (ok) requestAnimationFrame(loop);
  return {
    ok, W, H,
    add(v) { v.cv.width = W; v.cv.height = H; v.cv.__view = v; views.add(v); if (io) io.observe(v.cv); else v.visible = true; return v; },
    remove(v) { views.delete(v); if (io) io.unobserve(v.cv); },
    drawNow(v, t) { v.tick(t); draw(v); },
    renderer: R,
  };
})();

// ================================================================ the effect shader: a greyscale texture coloured through a palette (Skyrim's greyscale-to-palette)
const FXVS = `attribute float va; varying vec2 vUv; varying float vA; void main(){ vUv = uv; vA = va; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.); }`;
const FXFS = `uniform sampler2D tex, pal; uniform float gain, alpha, clampU; uniform vec2 off, cell, cellAt; varying vec2 vUv; varying float vA;
void main(){ vec2 uv = vUv + off; if (cell.x < 1.0) uv = (clamp(vUv, 0.002, 0.998) + cellAt) * cell; if (clampU > 0.5) uv.x = clamp(uv.x, 0.002, 0.998);
  vec4 t = texture2D(tex, uv); vec4 p = texture2D(pal, vec2(clamp(t.r, 0.004, 0.996), clamp(vA, 0.01, 0.99)));
  gl_FragColor = vec4(p.rgb * gain, t.a * vA * alpha); }`;
const TEX = {};
function tex(name, game) {   // (game: laid out as the game reads it - v from the image's top, as a DDS is)
  const k = name + (game ? '|g' : '');
  if (TEX[k]) return TEX[k];
  const t = new THREE.TextureLoader().load('live/tex/' + name + '.png');
  t.wrapS = t.wrapT = THREE.RepeatWrapping; t.minFilter = THREE.LinearMipmapLinearFilter; t.anisotropy = 4; if (game) t.flipY = false;
  return (TEX[k] = t);
}
const PALS = {};
function pal(key) {   // (the same ramp tools/weaponfx.py palettes() writes: the colour heating to white at its core)
  const hex = COLS[key] || key;
  if (PALS[hex]) return PALS[hex];
  const c = hexrgb(hex), mx = Math.max(...c), deep = c.map(x => x * x * 0.4), hot = c.map((x, i) => x * 0.55 + [1, 0.86, 0.6][i] * mx * 0.45), core = [1, 0.97, 0.92];
  const w = 256, h = 64, d = new Uint8Array(w * h * 4);
  const xs = [0, 0.45, 0.85, 1], ys = [deep, c, hot, core];
  for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) {
    const x = (i + 0.5) / w, v = (j + 0.5) / h, p = x * (0.4 + 0.6 * v);
    let k = 0; while (k < 2 && p > xs[k + 1]) k++;
    const f = Math.min(1, Math.max(0, (p - xs[k]) / (xs[k + 1] - xs[k])));
    const o = (j * w + i) * 4;
    for (let q = 0; q < 3; q++) d[o + q] = Math.round(255 * Math.min(1, ys[k][q] + (ys[k + 1][q] - ys[k][q]) * f));
    d[o + 3] = Math.round(255 * Math.min(1, x * 1.3) * (0.35 + 0.65 * v));
  }
  const t = new THREE.DataTexture(d, w, h, THREE.RGBAFormat); t.magFilter = t.minFilter = THREE.LinearFilter; t.needsUpdate = true;
  return (PALS[hex] = t);
}
const PALDS = {};
function palD(key) {   // (the Shadow look's: near black where the smoke is thick, the colour at its thin rim - weaponfx textures_v2)
  const hex = COLS[key] || key;
  if (PALDS[hex]) return PALDS[hex];
  const c = hexrgb(hex), w = 256, h = 64, d = new Uint8Array(w * h * 4);
  for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) {
    const x = (i + 0.5) / w, v = (j + 0.5) / h, p = x * (0.5 + 0.5 * v), o = (j * w + i) * 4;
    const xs = [0, 0.35, 0.8, 1];
    for (let q = 0; q < 3; q++) { const ys = [0.01, 0.03 + c[q] * 0.05, c[q] * 0.85, Math.min(1, c[q] * 1.2 + 0.15)]; let k = 0; while (k < 2 && p > xs[k + 1]) k++;
      const f = Math.min(1, Math.max(0, (p - xs[k]) / (xs[k + 1] - xs[k]))); d[o + q] = Math.round(255 * (ys[k] + (ys[k + 1] - ys[k]) * f)); }
    d[o + 3] = Math.round(255 * Math.min(1, 0.55 + x * 0.6) * (0.35 + 0.65 * v));
  }
  const t = new THREE.DataTexture(d, w, h, THREE.RGBAFormat); t.magFilter = t.minFilter = THREE.LinearFilter; t.needsUpdate = true;
  return (PALDS[hex] = t);
}
function fxMat(texName, palKey, gain, o = {}) {
  const u = { tex: { value: tex(texName, o.game) }, pal: { value: (o.dark ? palD : pal)(palKey) }, gain: { value: gain }, alpha: { value: 1 }, off: { value: new THREE.Vector2() },
    cell: { value: new THREE.Vector2(1, 1) }, cellAt: { value: new THREE.Vector2() }, clampU: { value: o.clamp ? 1 : 0 } };
  if (o.flip) u.cell.value.set(1 / o.flip[0], 1 / o.flip[1]);
  return new THREE.ShaderMaterial({ uniforms: u, vertexShader: FXVS, fragmentShader: FXFS, transparent: true, depthWrite: false,
    blending: o.alpha ? THREE.NormalBlending : THREE.CustomBlending, blendSrc: THREE.SrcAlphaFactor, blendDst: o.alpha ? THREE.OneMinusSrcAlphaFactor : THREE.OneFactor, side: THREE.DoubleSide });
}
function fxLayer(m) { m.layers.enable(1); m.renderOrder = 2; m.frustumCulled = false; return m; }
function setCell(u, flip, fps, t, phase, game) { const n = flip[0] * flip[1], c = Math.floor((t + (phase || 0)) * fps) % n; u.cellAt.value.set(c % flip[0], game ? Math.floor(c / flip[0]) : flip[1] - 1 - Math.floor(c / flip[0])); }

// a night backdrop for the weapon views
function nightBg() {
  const c = document.createElement('canvas'); c.width = 512; c.height = 256; const x = c.getContext('2d');
  const g = x.createRadialGradient(256, 128, 10, 256, 128, 300); g.addColorStop(0, '#1b2130'); g.addColorStop(1, '#06080c'); x.fillStyle = g; x.fillRect(0, 0, 512, 256);
  const t = new THREE.CanvasTexture(c); t.encoding = THREE.sRGBEncoding; return t;
}
const BG = GL.ok ? nightBg() : null;

// ================================================================ a weapon, live: its model and its effect art in any look and colour
const WDATA = {};
async function loadWeapon(key) {
  if (WDATA[key]) return WDATA[key];
  WDATA[key] = (async () => {
    const r = await fetch('live/' + key + '.json'); if (!r.ok) throw new Error('missing ' + key);
    const d = await r.json();
    const bin = Uint8Array.from(atob(d.gz), ch => ch.charCodeAt(0));
    const buf = await new Response(new Blob([bin]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer();
    let off = 0;
    const take = (Ctor, n) => { const b = Ctor.BYTES_PER_ELEMENT * n; const a = new Ctor(buf.slice(off, off + b)); off += b; return a; };
    const deq = q => { const p = new Float32Array(q.length); for (let i = 0; i < q.length; i++) { const ax = i % 3; p[i] = (q[i] + 32768) / 65535 * d.span[ax] + d.lo[ax]; } return p; };
    const deuv = q => { const p = new Float32Array(q.length); for (let i = 0; i < q.length; i++) p[i] = q[i] / 2048; return p; };
    d.mesh = d.shapes.map(s => ({ s, pos: deq(take(Int16Array, s.n * 3)), uv: deuv(take(Int16Array, s.n * 2)), idx: take(s.i32 ? Uint32Array : Uint16Array, s.nt * 3) }));
    d.fx = {};
    for (const lk of Object.keys(d.looks)) d.fx[lk] = d.looks[lk].map(s => ({ s, pos: deq(take(Int16Array, s.n * 3)), uv: deuv(take(Int16Array, s.n * 2)), a: take(Uint8Array, s.n), idx: take(s.i32 ? Uint32Array : Uint16Array, s.nt * 3) }));
    return d;
  })();
  return WDATA[key];
}
const MAPS = {};
function weaponModel(d) {
  const g = new THREE.Group(), pts = [];
  for (const { s, pos, uv, idx } of d.mesh) {
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3)); geo.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
    geo.setIndex(new THREE.BufferAttribute(idx, 1)); geo.computeVertexNormals();
    let m;
    if (s.map) {
      let t = MAPS[s.map]; if (!t) { t = MAPS[s.map] = new THREE.TextureLoader().load(s.map); t.encoding = THREE.sRGBEncoding; t.flipY = false; t.wrapS = t.wrapT = THREE.RepeatWrapping; }
      let nm = null; if (s.nmap) { nm = MAPS[s.nmap]; if (!nm) { nm = MAPS[s.nmap] = new THREE.TextureLoader().load(s.nmap); nm.flipY = false; nm.wrapS = nm.wrapT = THREE.RepeatWrapping; } }
      m = new THREE.MeshStandardMaterial({ map: t, normalMap: nm, normalScale: new THREE.Vector2(1, -1), metalness: 0.6, roughness: 0.4, side: THREE.DoubleSide });
    } else m = new THREE.MeshStandardMaterial({ color: new THREE.Color(s.color).convertSRGBToLinear(), metalness: 0.65, roughness: 0.4, side: THREE.DoubleSide });
    if (s.glow) {   // (the model's own glow - a ghostly blade, a gem's light: drawn added, as the game's effect shader does)
      m = new THREE.MeshBasicMaterial({ map: m.map || null, color: m.map ? 0xffffff : new THREE.Color(s.color || '#ffffff'), transparent: true, opacity: 0.35, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide });
      const gm = new THREE.Mesh(geo, m); gm.layers.enable(1); gm.renderOrder = 2; g.add(gm);
    } else g.add(new THREE.Mesh(geo, m));
    for (let i = 0; i < pos.length; i += 9) pts.push([pos[i], pos[i + 1], pos[i + 2]]);
  }
  return { g, pts };
}
function weaponFx(d, look) {
  const g = new THREE.Group(), list = [], bbs = [], pts = [];
  for (const { s, pos, uv, a, idx } of d.fx[look]) {
    const m = fxMat(s.tex, s.pal, s.scale, { flip: s.flip, clamp: s.clamp, game: true, dark: s.dark, alpha: s.alpha });
    const geo = new THREE.BufferGeometry();
    if (s.bb) {
      const c = s.corner, P = new Float32Array(s.n * 3);
      for (let i = 0; i < s.n; i++) { P[i * 3] = c[i * 2]; P[i * 3 + 1] = c[i * 2 + 1]; }
      geo.setAttribute('position', new THREE.BufferAttribute(P, 3));
    } else geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    geo.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
    const A = new Float32Array(s.n); for (let i = 0; i < s.n; i++) A[i] = a[i] / 255; geo.setAttribute('va', new THREE.BufferAttribute(A, 1));
    geo.setIndex(new THREE.BufferAttribute(idx, 1));
    const mesh = fxLayer(new THREE.Mesh(geo, m));
    if (s.bb) { const h = new THREE.Group(); h.position.set(...s.at); h.add(mesh); g.add(h); bbs.push(h); pts.push(s.at); }
    else { g.add(mesh); for (let i = 0; i < pos.length; i += 30) pts.push([pos[i], pos[i + 1], pos[i + 2]]); }
    list.push({ s, u: m.uniforms });
  }
  return { g, list, bbs, pts };
}
function pca(pts) {
  const n = pts.length, C = [0, 0, 0];
  for (const p of pts) { C[0] += p[0]; C[1] += p[1]; C[2] += p[2]; }
  C[0] /= n; C[1] /= n; C[2] /= n;
  const A = [[0, 0, 0], [0, 0, 0], [0, 0, 0]];
  for (const p of pts) { const q = [p[0] - C[0], p[1] - C[1], p[2] - C[2]]; for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) A[i][j] += q[i] * q[j]; }
  let a = A.map(r => r.slice()), v = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];
  const mul = (X, Y) => X.map((r, i) => Y[0].map((_, j) => r.reduce((s, _, k) => s + X[i][k] * Y[k][j], 0)));
  for (let it = 0; it < 40; it++) {
    let p = 0, q = 1; if (Math.abs(a[0][2]) > Math.abs(a[p][q])) p = 0, q = 2; if (Math.abs(a[1][2]) > Math.abs(a[p][q])) p = 1, q = 2;
    if (Math.abs(a[p][q]) < 1e-9) break;
    const th = 0.5 * Math.atan2(2 * a[p][q], a[q][q] - a[p][p]), c = Math.cos(th), s = Math.sin(th);
    const J = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]; J[p][p] = c; J[q][q] = c; J[p][q] = s; J[q][p] = -s;
    a = mul(mul(J[0].map((_, i) => J.map(r => r[i])), a), J); v = mul(v, J);
  }
  const ev = [0, 1, 2].map(i => [a[i][i], [v[0][i], v[1][i], v[2][i]]]).sort((x, y) => y[0] - x[0]).map(x => x[1]);
  return { C, ev };
}
function weaponView(cv, onInfo) {
  const scene = new THREE.Scene(); scene.background = BG;
  const cam = new THREE.PerspectiveCamera(22, GL.W / GL.H, 1, 6000);
  scene.add(new THREE.HemisphereLight(0x8090b0, 0x1a1410, 0.6));
  const key = new THREE.DirectionalLight(0xffe2c0, 1.6); scene.add(key);
  const rim = new THREE.DirectionalLight(0x9fc0ff, 1.1); scene.add(rim);
  const pivot = new THREE.Group(), tilt = new THREE.Group(), root = new THREE.Group(); tilt.add(root); pivot.add(tilt); scene.add(pivot);
  const v = { cv, scene, cam, ready: false, visible: false, bloom: 1, cur: null, look: null, col: null, fx: null, d: null, model: null, key: null, yaw: 0, pitch: 0, zoom: 1, mid: new THREE.Vector3(), dist: 100 };
  // (Noah: "drag the model with my mouse cursor to see the other side ... the scroll wheel ... zoom in and out") drag to turn it,
  // the wheel (over the card) to zoom, a double click to put it back
  const placeCam = () => { cam.position.set(v.mid.x, v.mid.y, v.mid.z + v.dist * v.zoom); cam.lookAt(v.mid); pivot.rotation.set(v.pitch, v.yaw, 0, 'YXZ'); };
  v.placeCam = placeCam;
  let drag = null;
  cv.style.touchAction = 'pan-y'; cv.style.cursor = 'grab';
  cv.addEventListener('pointerdown', e => { drag = { x: e.clientX, y: e.clientY, yaw: v.yaw, pitch: v.pitch }; cv.setPointerCapture(e.pointerId); cv.style.cursor = 'grabbing'; });
  cv.addEventListener('pointermove', e => { if (!drag) return; v.yaw = drag.yaw + (e.clientX - drag.x) * 0.012; v.pitch = Math.max(-1.4, Math.min(1.4, drag.pitch + (e.clientY - drag.y) * 0.01)); placeCam(); });
  const up = () => { drag = null; cv.style.cursor = 'grab'; };
  cv.addEventListener('pointerup', up); cv.addEventListener('pointercancel', up);
  cv.addEventListener('wheel', e => { e.preventDefault(); v.zoom = Math.max(0.25, Math.min(2.5, v.zoom * Math.exp(e.deltaY * 0.0012))); placeCam(); }, { passive: false });
  cv.addEventListener('dblclick', () => { v.yaw = 0; v.pitch = 0; v.zoom = 1; placeCam(); });
  v.tick = t => {
    if (!v.fx) return;
    if (v.aura) v.aura.tick(t, root); if (v.aura2) v.aura2.tick(t, root);
    // (Noah, Nov 26 night: "make sure the look animations are 360") a slow turntable, whole turns, unless you're turning it
    if (v.spin && !drag) { v.yaw += 0.008; placeCam(); }
    for (const [fx, gm] of [[v.fx, v.gain == null ? 1 : v.gain], [v.fx2, v.gain2 == null ? 1 : v.gain2]]) {
      if (!fx) continue;
      for (const { s, u } of fx.list) {
        if (s.vscroll && !s.flip) u.off.value.set(0, -(t / s.vscroll) % 1);
        if (s.flip) setCell(u, s.flip, s.fps, t, s.phase, true);
        let g = s.scale; if (s.pulse) { const [per, lo, hi] = s.pulse; g *= lo + (hi - lo) * (0.5 - 0.5 * Math.cos(2 * Math.PI * ((t + (s.phase || 0)) / per))); }
        u.gain.value = g * gm;
      }
      const pq = new THREE.Quaternion();
      for (const b of fx.bbs) { b.parent.getWorldQuaternion(pq); b.quaternion.copy(pq.invert().multiply(cam.quaternion)); }
    }
  };
  v.show = async (keyName, look, col) => {
    const sameKey = v.key === keyName;
    v.key = keyName;
    let d; try { d = await loadWeapon(keyName); } catch (e) { onInfo && onInfo({ error: true }); return; }
    if (v.key !== keyName) return;
    if (!sameKey || !v.model) {
      while (root.children.length) root.remove(root.children[0]);
      v.model = weaponModel(d); root.add(v.model.g); v.d = d; v.fx = null; v.look = null;
      // frame it: the long axis across, the flat of the blade to you, the effect's end to the right
      const art = weaponFx(d, 'o1');
      const { C, ev } = pca(v.model.pts);
      let [ax, ay] = ev;
      const fc = art.pts.length ? art.pts.reduce((a, p) => [a[0] + p[0], a[1] + p[1], a[2] + p[2]], [0, 0, 0]).map(x => x / art.pts.length) : C;
      if ((fc[0] - C[0]) * ax[0] + (fc[1] - C[1]) * ax[1] + (fc[2] - C[2]) * ax[2] < 0) ax = ax.map(x => -x);
      const az = [ax[1] * ay[2] - ax[2] * ay[1], ax[2] * ay[0] - ax[0] * ay[2], ax[0] * ay[1] - ax[1] * ay[0]];
      root.matrixAutoUpdate = false;
      root.matrix.multiplyMatrices(new THREE.Matrix4().set(ax[0], ax[1], ax[2], 0, ay[0], ay[1], ay[2], 0, az[0], az[1], az[2], 0, 0, 0, 0, 1), new THREE.Matrix4().makeTranslation(-C[0], -C[1], -C[2]));
      pivot.rotation.set(0, 0, 0); tilt.rotation.set(0.32, -0.22, 0.18); pivot.updateMatrixWorld(true);
      const box = new THREE.Box3().setFromObject(v.model.g), sz = box.getSize(new THREE.Vector3()), mid = box.getCenter(new THREE.Vector3());
      const tn = Math.tan(THREE.MathUtils.degToRad(11));
      const dist = Math.max(sz.x * 1.06 / (2 * tn * (GL.W / GL.H)), sz.y * 1.16 / (2 * tn)) + sz.z;
      v.mid.copy(mid); v.dist = dist; placeCam();
      key.position.set(mid.x - 60, mid.y + 120, mid.z + 140); rim.position.set(mid.x + 80, mid.y + 40, mid.z - 160);
      // its length in game (the model's own scale, and the scythes' 25% more)
      const lens = v.model.pts.map(p => (p[0] - C[0]) * ax[0] + (p[1] - C[1]) * ax[1] + (p[2] - C[2]) * ax[2]);
      const sc = (d.scale || 1) * (SCYTHES.includes(keyName) ? 1.25 : 1);
      v.lengthM = (Math.max(...lens) - Math.min(...lens)) * sc * 0.01428; v.span = Math.max(...lens) - Math.min(...lens);
    }
    if (v.look !== look || !v.fx) {
      if (v.fx) root.remove(v.fx.g);
      v.fx = weaponFx(d, look); root.add(v.fx.g); v.look = look; v.col = null;
      if (v.aura) { root.remove(v.aura); v.aura = null; }
      if (v.vol && typeof weaponAura === 'function') {   // (the 3D look on the model itself; the game's old flat art hidden unless compared)
        v.aura = weaponAura(d, v.fx.pts, Object.assign(lookStyle(d, look), { k: v.gain == null ? 1 : v.gain, span: v.span }));
        root.add(v.aura); v.fx.g.visible = !!v.compare;
      }
    }
    if (v.col !== col) {
      for (const { s, u } of v.fx.list) u.pal.value = (s.dark ? palD : pal)(col && col !== 'own' && s.pal === d.c1 ? col : s.pal);   // (your colour on the look's own colour; its second stays)
      v.col = col;
    }
    v.ready = true;
    onInfo && onInfo({ length: v.lengthM });
  };
  // (a second look layered over the first: a Legendary tier)
  v.layer = (look2, gain2) => {
    if (v.fx2) { root.remove(v.fx2.g); v.fx2 = null; }
    if (v.aura2) { root.remove(v.aura2); v.aura2 = null; }
    if (!look2 || !v.d) return;
    v.fx2 = weaponFx(v.d, look2); root.add(v.fx2.g); v.gain2 = gain2;
    if (v.vol && typeof weaponAura === 'function') { v.aura2 = weaponAura(v.d, v.fx2.pts, Object.assign(lookStyle(v.d, look2), { k: gain2, span: v.span })); root.add(v.aura2); v.fx2.g.visible = !!v.compare; }
  };
  v.setCompare = on => { v.compare = on; if (v.fx) v.fx.g.visible = on || !v.aura; if (v.fx2) v.fx2.g.visible = on || !v.aura2; if (v.aura) v.aura.visible = !on; if (v.aura2) v.aura2.visible = !on; };
  return GL.add(v);
}


// a look's 3D style, read from the art it replaces: its colours, and what its textures are (rising fire, smoke, an edge of heat,
// runes, drops, embers, mist, streams, a halo, wisps, a sweeping band)
const TEXSTYLE = { fire_rise0: { pk: 'ember', pn: 1.2, rise: 1.4, vol: 1, mem: .8 }, fire_rise1: { pk: 'ember', pn: 1.2, rise: 1.4, vol: 1, mem: .8 },
  shade_strip: { pk: 'smoke', pn: .6, dark: true, vol: 1.3, mem: .6 }, molten_strip: { mem: 1.6, pn: .35 }, cracks: { mem: 1.3, pn: .3 }, sig_drops: { pk: 'mote', pn: 1, rise: .8 },
  sig_embers: { pk: 'ember', pn: 1.4, rise: 1 }, sig_runes: { mem: 1.3, pn: .4, runes: true }, mist_strip0: { vol: 1.6, pn: .4, pk: 'mote' }, mist_strip1: { vol: 1.6, pn: .4, pk: 'mote' },
  haze: { vol: 1.4, pn: .4 }, stream_strip: { pk: 'streak', pn: 1, rise: .3 }, ray_strip: { pk: 'streak', pn: .9, rise: .2 }, halo_shell: { vol: 1.5, pn: .5 },
  wisp_strip: { pk: 'wisp', pn: .9 }, sk_wisps: { pk: 'wisp', pn: .8 }, sig_band: { mem: 1.5, pn: .3, sweep: true }, sk_enchfill: { mem: 1, pn: .5 }, sk_swirls: { pk: 'wisp', pn: .8 } };
function lookStyle(d, look) {
  const shapes = (d.fx && d.fx[look]) || [], pals = [], st = { pn: .5, mem: .8, vol: .3, rise: null, pk: null, dark: false };
  for (const { s } of shapes) {
    if (!pals.includes(s.pal)) pals.push(s.pal);
    const t = TEXSTYLE[s.tex] || {};
    for (const k of ['pn', 'mem', 'vol']) if (t[k] != null) st[k] = Math.max(st[k], t[k]);
    if (t.rise != null) st.rise = Math.max(st.rise || 0, t.rise); if (t.pk && !st.pk) st.pk = t.pk; if (t.dark) st.dark = true; if (t.runes) st.runes = true; if (t.sweep) st.sweep = true;
  }
  const p1 = pals[0] || d.c1 || 'white', p2 = pals[1] || p1;
  const el = LOOK_ELEM[p1] || LOOK_ELEM[d.c1] || 'arcane';
  const h1 = COLS[p1] || p1, h2 = COLS[p2] || p2, c1 = new THREE.Color(h1), c2 = new THREE.Color(h2).lerp(new THREE.Color('#ffffff'), .55), c0 = new THREE.Color(h1).multiplyScalar(.18);
  return Object.assign(st, { el, c0: '#' + c0.getHexString(), c1: '#' + c1.getHexString(), c2: '#' + c2.getHexString() });
}
