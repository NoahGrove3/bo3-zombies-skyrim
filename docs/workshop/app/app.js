// ================================================================ helpers
const $ = (s, el = document) => el.querySelector(s);
const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const colName = c => String(c).split('+').map(x => CNAME[x] || x).join(' and ');
const srcTags = s => s.split(' ').filter(k => SRC[k]).map(k => `<span class="src" style="background:${SRC[k][1]}">${SRC[k][0]}</span>`).join('');
const sleep = ms => new Promise(r => setTimeout(r, ms));
function hexrgb(h) { h = h.replace('#', ''); return [0, 2, 4].map(i => parseInt(h.substr(i, 2), 16) / 255); }

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
      m = new THREE.MeshStandardMaterial({ map: t, metalness: 0.55, roughness: 0.45, side: THREE.DoubleSide });
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
    if (!v.fx) { zs.forEach((z, i) => z && zPose(z, [], t, zinfo[i])); if (plr) plrPose(plr, t, [], pinfo); return; }
    for (const { s, u } of v.fx.list) {
      if (s.vscroll && !s.flip) u.off.value.set(0, -(t / s.vscroll) % 1);
      if (s.flip) setCell(u, s.flip, s.fps, t, s.phase, true);
      let g = s.scale; if (s.pulse) { const [per, lo, hi] = s.pulse; g *= lo + (hi - lo) * (0.5 - 0.5 * Math.cos(2 * Math.PI * ((t + (s.phase || 0)) / per))); }
      u.gain.value = g;
    }
    const pq = new THREE.Quaternion();
    for (const b of v.fx.bbs) { b.parent.getWorldQuaternion(pq); b.quaternion.copy(pq.invert().multiply(cam.quaternion)); }
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
      v.lengthM = (Math.max(...lens) - Math.min(...lens)) * sc * 0.01428;
    }
    if (v.look !== look || !v.fx) {
      if (v.fx) root.remove(v.fx.g);
      v.fx = weaponFx(d, look); root.add(v.fx.g); v.look = look; v.col = null;
    }
    if (v.col !== col) {
      for (const { s, u } of v.fx.list) u.pal.value = (s.dark ? palD : pal)(col && col !== 'own' && s.pal === d.c1 ? col : s.pal);   // (your colour on the look's own colour; its second stays)
      v.col = col;
    }
    v.ready = true;
    onInfo && onInfo({ length: v.lengthM });
  };
  return GL.add(v);
}

// ================================================================ abilities: our own effects, drawn live over a patch of night ground
let GROUND = null;
function groundTex() {
  if (GROUND) return GROUND;
  const c = document.createElement('canvas'); c.width = c.height = 512; const x = c.getContext('2d');
  x.fillStyle = '#1b1c19'; x.fillRect(0, 0, 512, 512);
  let s = 7; const r = () => (s = (s * 16807) % 2147483647) / 2147483647;
  for (let i = 0; i < 5000; i++) { const v = 26 + r() * 30 | 0; x.fillStyle = `rgba(${v},${v},${v - 4},0.3)`; x.fillRect(r() * 512, r() * 512, 2 + r() * 8, 2 + r() * 8); }
  for (let i = 0; i < 900; i++) { x.fillStyle = `rgba(42,58,30,${0.15 + r() * 0.2})`; x.fillRect(r() * 512, r() * 512, 1 + r() * 2, 3 + r() * 7); }
  const t = new THREE.CanvasTexture(c); t.wrapS = t.wrapT = THREE.RepeatWrapping; t.repeat.set(6, 6); t.encoding = THREE.sRGBEncoding; return (GROUND = t);
}
function dummy(light) {   // one of the dead: a hunched figure (or you, paler)
  const g = new THREE.Group();
  const m = new THREE.MeshStandardMaterial({ color: light ? 0x55606e : 0x24231f, roughness: 0.85, metalness: 0.05 });
  const body = new THREE.Mesh(new THREE.CylinderGeometry(15, 19, 66, 10), m); body.position.y = 66; g.add(body);
  const hips = new THREE.Mesh(new THREE.CylinderGeometry(17, 13, 20, 10), m); hips.position.y = 34; g.add(hips);
  const head = new THREE.Mesh(new THREE.SphereGeometry(11, 14, 10), m); head.position.y = 112; g.add(head);
  for (const sx of [-1, 1]) {
    const leg = new THREE.Mesh(new THREE.CylinderGeometry(6, 5, 40, 8), m); leg.position.set(sx * 8, 14, 0); g.add(leg);
    const arm = new THREE.Mesh(new THREE.CylinderGeometry(5, 4, 52, 8), m); arm.position.set(sx * 23, 72, 6); arm.rotation.z = sx * 0.18; arm.rotation.x = -0.3; g.add(arm);
  }
  if (!light) g.rotation.y = -0.6;
  return g;
}
const SPOT = { wielder: new THREE.Vector3(-230, 0, 130), t0: new THREE.Vector3(40, 0, 0), others: [new THREE.Vector3(190, 0, -120), new THREE.Vector3(-40, 0, -190), new THREE.Vector3(200, 0, 120), new THREE.Vector3(-150, 0, -50)] };
function rnd(seed) { let s = seed >>> 0 || 1; return () => (s = (s * 16807) % 2147483647) / 2147483647; }
const ease = { out: x => 1 - Math.pow(1 - Math.min(1, Math.max(0, x)), 3), inout: x => { x = Math.min(1, Math.max(0, x)); return x * x * (3 - 2 * x); } };
const fade = (t, a, b, c, d) => t < a ? 0 : t < b ? (t - a) / (b - a) : t < c ? 1 : t < d ? 1 - (t - c) / (d - c) : 0;

// the pieces each effect is built from
function Card(fx, texName, gain, o = {}) {   // a card that always faces you
  const geo = new THREE.PlaneGeometry(1, 1);
  geo.setAttribute('va', new THREE.BufferAttribute(new Float32Array(4).fill(o.va == null ? 1 : o.va), 1));
  const m = fxMat(texName, fx.col, gain, o); const mesh = fxLayer(new THREE.Mesh(geo, m));
  mesh.userData.bb = true; fx.g.add(mesh); fx.bbs.push(mesh); return mesh;
}
function Ring(fx, texName, gain, o = {}) {   // a flat ring on the ground: u across its band, v round it
  const seg = o.seg || 96, P = [], U = [], A = [], I = [];
  for (let i = 0; i <= seg; i++) {
    const a = i / seg * Math.PI * 2;
    for (const [r, u, al] of (o.rows || [[0, 0, 0], [0.35, 0.35, 1], [0.65, 0.65, 1], [1, 1, 0]])) { P.push(Math.cos(a) * r, 0, Math.sin(a) * r); U.push(u, i / seg * (o.vrep || 8)); A.push(al); }
  }
  const k = (o.rows || [0, 0, 0, 0]).length;
  for (let i = 0; i < seg; i++) for (let j = 0; j < k - 1; j++) { const a = i * k + j, b = a + 1, c = a + k, d = c + 1; I.push(a, b, d, a, d, c); }
  const geo = new THREE.BufferGeometry(); geo.setAttribute('position', new THREE.Float32BufferAttribute(P, 3)); geo.setAttribute('uv', new THREE.Float32BufferAttribute(U, 2));
  geo.setAttribute('va', new THREE.Float32BufferAttribute(A, 1)); geo.setIndex(I);
  const mesh = fxLayer(new THREE.Mesh(geo, fxMat(texName, fx.col, gain, o))); fx.g.add(mesh); return mesh;
}
function Tube(fx, texName, gain, o = {}) {   // a standing sheet round a circle (a wall of light, a funnel): u round it, v up
  const seg = o.seg || 48, rows = o.rows || 6, P = [], U = [], A = [], I = [];
  for (let j = 0; j <= rows; j++) {
    const f = j / rows, r = (o.r0 == null ? 1 : o.r0) + ((o.r1 == null ? 1 : o.r1) - (o.r0 == null ? 1 : o.r0)) * f, tw = (o.twist || 0) * f;
    for (let i = 0; i <= seg; i++) { const a = i / seg * Math.PI * 2 * (o.arc || 1) + tw + (o.a0 || 0); P.push(Math.cos(a) * r, f, Math.sin(a) * r); U.push(i / seg * (o.urep || 3), f * (o.vrep || 1)); A.push(o.fadeTop === false ? 1 : Math.sin(Math.PI * Math.min(1, f * 1.1)) ** 0.6 * (f < 0.95 ? 1 : 0)); }
  }
  for (let j = 0; j < rows; j++) for (let i = 0; i < seg; i++) { const a = j * (seg + 1) + i, b = a + 1, c = a + seg + 1, d = c + 1; I.push(a, b, d, a, d, c); }
  const geo = new THREE.BufferGeometry(); geo.setAttribute('position', new THREE.Float32BufferAttribute(P, 3)); geo.setAttribute('uv', new THREE.Float32BufferAttribute(U, 2));
  geo.setAttribute('va', new THREE.Float32BufferAttribute(A, 1)); geo.setIndex(I);
  const mesh = fxLayer(new THREE.Mesh(geo, fxMat(texName, fx.col, gain, o))); fx.g.add(mesh); return mesh;
}
function Ribbon(fx, texName, gain, n, o = {}) {   // a strip that follows a line of points, turned to face you (bolts, trails, chains)
  const P = new Float32Array(n * 2 * 3), U = new Float32Array(n * 2 * 2), A = new Float32Array(n * 2), I = [];
  for (let i = 0; i < n - 1; i++) { const a = i * 2; I.push(a, a + 1, a + 3, a, a + 3, a + 2); }
  const geo = new THREE.BufferGeometry(); geo.setAttribute('position', new THREE.BufferAttribute(P, 3)); geo.setAttribute('uv', new THREE.BufferAttribute(U, 2));
  geo.setAttribute('va', new THREE.BufferAttribute(A, 1)); geo.setIndex(I);
  const mesh = fxLayer(new THREE.Mesh(geo, fxMat(texName, fx.col, gain, o))); fx.g.add(mesh);
  const tmp = new THREE.Vector3(), side = new THREE.Vector3(), view = new THREE.Vector3();
  mesh.userData.set = (pts, width, alphaAt) => {
    for (let i = 0; i < n; i++) {
      const p = pts[Math.min(i, pts.length - 1)], q = pts[Math.min(i + 1, pts.length - 1)], pp = pts[Math.min(Math.max(i - 1, 0), pts.length - 1)];
      tmp.copy(q).sub(pp); view.copy(fx.cam.position).sub(p);
      side.crossVectors(tmp, view).normalize();
      const w = typeof width === 'function' ? width(i / (n - 1)) : width;
      P.set([p.x + side.x * w, p.y + side.y * w, p.z + side.z * w, p.x - side.x * w, p.y - side.y * w, p.z - side.z * w], i * 6);
      U.set([0, i / (n - 1), 1, i / (n - 1)], i * 4);
      const al = alphaAt ? alphaAt(i / (n - 1)) : 1; A[i * 2] = A[i * 2 + 1] = al;
    }
    geo.attributes.position.needsUpdate = geo.attributes.uv.needsUpdate = geo.attributes.va.needsUpdate = true;
  };
  return mesh;
}
function jag(a, b, n, amp, r, keep) {   // a jagged line from a to b (lightning)
  const pts = [], d = b.clone().sub(a), up = new THREE.Vector3(0, 1, 0), s1 = new THREE.Vector3().crossVectors(d, up); if (s1.lengthSq() < 1e-3) s1.set(1, 0, 0);
  s1.normalize(); const s2 = new THREE.Vector3().crossVectors(d, s1).normalize();
  let o1 = 0, o2 = 0;
  for (let i = 0; i < n; i++) {
    const f = i / (n - 1), env = Math.sin(Math.PI * f);
    o1 = o1 * 0.55 + (r() - 0.5) * amp; o2 = o2 * 0.55 + (r() - 0.5) * amp;
    pts.push(a.clone().addScaledVector(d, f).addScaledVector(s1, o1 * env).addScaledVector(s2, o2 * env));
  }
  return pts;
}
function sparkSet(fx, n, r, o = {}) {   // sparks flung out, falling
  return Array.from({ length: n }, () => {
    const c = Card(fx, 't_glow', o.gain || 2.2); const dir = new THREE.Vector3(r() - 0.5, (r() * 0.8 + (o.up || 0.2)), r() - 0.5).normalize();
    return { c, dir, sp: (o.speed || 260) * (0.4 + r() * 0.8), sz: (o.size || 7) * (0.6 + r() * 0.8), life: (o.life || 0.8) * (0.6 + r() * 0.6) };
  });
}
function sparksAt(list, t, at, s, grav) {
  for (const p of list) {
    const k = t / p.life, vis = t >= 0 && k < 1;
    p.c.visible = vis; if (!vis) continue;
    const dist = p.sp * s * (1 - Math.exp(-t * 3)) / 3;
    p.c.position.copy(at).addScaledVector(p.dir, dist); p.c.position.y -= (grav == null ? 160 : grav) * t * t;
    p.c.scale.setScalar(p.sz * s * (1 - k * 0.6)); p.c.material.uniforms.alpha.value = 1 - k;
  }
}
// a burst at a point: flash, a ball of the element, sparks, a wisp of smoke (used alone, and at the end of bolts and strikes)
function burstPart(fx, s, r) {
  const flash = Card(fx, 't_glow', 3.0), fire = [0, 1, 2].map(() => Card(fx, 't_spiritflame', 1.6, { flip: [4, 4] })), smoke = [0, 1].map(() => Card(fx, 't_puff', 0.35, { va: 0.25 }));
  const sp = sparkSet(fx, 22, r, { speed: 300, size: 6, life: 0.9, up: 0.3 }); const ph = fire.map(() => r() * 2), off = fire.map(() => new THREE.Vector3(r() - 0.5, r() * 0.6, r() - 0.5));
  return (t, at) => {
    const on = t >= 0;
    flash.visible = on && t < 0.5; if (flash.visible) { flash.position.copy(at); flash.scale.setScalar(150 * s * ease.out(t / 0.12)); flash.material.uniforms.alpha.value = 1 - t / 0.5; }
    fire.forEach((c, i) => { const k = t / 0.9; c.visible = on && k < 1; if (!c.visible) return; c.position.copy(at).addScaledVector(off[i], 30 * s).add(new THREE.Vector3(0, 30 * s * k, 0));
      c.scale.setScalar(90 * s * (0.5 + 0.7 * ease.out(k * 1.6))); c.material.uniforms.alpha.value = Math.min(1, (1 - k) * 1.6); setCell(c.material.uniforms, [4, 4], 22, t, ph[i]); });
    smoke.forEach((c, i) => { const k = (t - 0.15) / 1.6; c.visible = on && k > 0 && k < 1; if (!c.visible) return; c.position.copy(at).add(new THREE.Vector3((i - 0.5) * 20 * s, 40 * s + 90 * s * k, 0));
      c.scale.setScalar(100 * s * (0.6 + k)); c.material.uniforms.alpha.value = Math.sin(Math.PI * k) * 0.6; });
    sparksAt(sp, t, at, s);
  };
}
function ringPart(fx, s, R, o = {}) {   // the shockwave ring: a band racing out, a low wall of light on its edge, dust lifting
  const band = Ring(fx, 't_band', o.gain || 2.2, { vrep: 10, clamp: true, rows: [[0.78, 0, 0], [0.88, 0.35, 1], [0.95, 0.7, 1], [1.0, 1, 0]] }), spill = Ring(fx, 't_glow', 0.6, { rows: [[0, 0.5, 0], [0.5, 0.5, 0.6], [1, 0.5, 0]], vrep: 1 });
  const wall = Tube(fx, 't_curtain', 1.1, { urep: 6, vrep: 1 }), r = rnd(o.seed || 11);
  const motes = Array.from({ length: 26 }, () => ({ c: Card(fx, 't_glow', 1.8), a: r() * Math.PI * 2, h: r(), l: 0.4 + r() * 0.4 }));
  return (t, at) => {
    const k = t / (o.dur || 0.7), rad = R * s * ease.out(k), on = t >= 0 && k < 1;
    for (const m of [band, spill, wall]) m.visible = on;
    if (on) {
      band.position.copy(at).setY(at.y + 2); band.scale.set(rad, 1, rad); band.material.uniforms.alpha.value = (1 - k) ** 1.2;
      band.material.uniforms.off.value.set(0, t * 0.5);
      spill.position.copy(at).setY(at.y + 1); spill.scale.set(rad * 1.25 + 1, 1, rad * 1.25 + 1); spill.material.uniforms.alpha.value = (1 - k) * 0.8;
      wall.position.copy(at); wall.scale.set(rad + 1, 60 * s * (1 - k * 0.6), rad + 1); wall.material.uniforms.alpha.value = (1 - k) ** 1.5; wall.material.uniforms.off.value.set(t * 0.3, -t * 1.5);
    }
    for (const m of motes) { const kk = (t - 0.05) / m.l; m.c.visible = t >= 0 && kk > 0 && kk < 1; if (!m.c.visible) continue;
      const rr = R * s * ease.out(Math.min(1, (t - 0.05) / (o.dur || 0.7))); m.c.position.set(at.x + Math.cos(m.a) * rr, at.y + 8 + 70 * s * m.h * kk, at.z + Math.sin(m.a) * rr);
      m.c.scale.setScalar(9 * s); m.c.material.uniforms.alpha.value = 1 - kk; }
  };
}
function boltPart(fx, s, r, o = {}) {   // a bolt flying from a to b, a trail of fire behind it
  const head = Card(fx, 't_glow', 1.3), fire = Card(fx, 't_spiritflame', 1.9, { flip: [4, 4] })   // (the Issues tab: a softer core, more of the flame's colour - the bolts read pale before), trail = Ribbon(fx, 't_trailfire', 1.5, 18, { clamp: true }), hist = [];
  return (t, a, b, dur, arc) => {
    const k = t / dur, on = t >= 0 && k < 1;
    head.visible = fire.visible = trail.visible = on; if (!on) { hist.length = 0; return; }
    const p = a.clone().lerp(b, k); p.y += Math.sin(Math.PI * k) * (arc || 0);
    hist.unshift(p.clone()); while (hist.length > 18) hist.pop();
    head.position.copy(p); head.scale.setScalar(52 * s); fire.position.copy(p); fire.scale.setScalar(46 * s); setCell(fire.material.uniforms, [4, 4], 24, t, 0);
    const pts = hist.length > 1 ? hist : [p, p.clone().add(new THREE.Vector3(-1, 0, 0))];
    trail.userData.set(pts, f => 16 * s * (1 - f), f => (1 - f) ** 1.4); trail.material.uniforms.off.value.set(0, t * 2);
  };
}

// each style: build(fx, s, at, r) -> update(t). `at` is where it happens; fx.wielder and fx.targets are the scene's people
const STYLE_FN = {
  burst(fx, s, at, r) { const b = burstPart(fx, s, r); return { dur: 1.8, f: t => b(t - 0.1, at.clone().setY(70)) }; },
  shockwave(fx, s, at, r) { const flash = Card(fx, 't_glow', 2.4), ring = ringPart(fx, s, 300); return { dur: 1.6, f: t => {
    ring(t - 0.05, at); flash.visible = t < 0.4; flash.position.copy(at).setY(25); flash.scale.setScalar(200 * s * ease.out(t / 0.1)); flash.material.uniforms.alpha.value = 1 - t / 0.4; } }; },
  nova(fx, s, at, r) {
    const ring = ringPart(fx, s, 280, { gain: 2 }), shards = Array.from({ length: 28 }, () => ({ c: Card(fx, 't_glow', 2.2), a: r() * Math.PI * 2, d: 0.35 + r() * 0.65, h: 0.5 + r() }));
    const mist = Array.from({ length: 8 }, () => ({ c: Card(fx, 't_puff', 0.6, { va: 0.6 }), a: r() * Math.PI * 2 }));
    return { dur: 2.0, f: t => { ring(t, at);
      for (const p of shards) { const k = (t - p.d * 0.5) / 0.9; p.c.visible = k > 0 && k < 1; if (!p.c.visible) continue;
        const rr = 280 * s * p.d; p.c.position.set(at.x + Math.cos(p.a) * rr, 20 * s + 120 * s * p.h * ease.out(k), at.z + Math.sin(p.a) * rr);
        p.c.scale.set(7 * s, 46 * s * (1 - k * 0.5), 1); p.c.material.uniforms.alpha.value = 1 - k; }
      for (const m of mist) { const k = (t - 0.2) / 1.6; m.c.visible = k > 0 && k < 1; if (!m.c.visible) continue; const rr = 230 * s * (0.6 + 0.5 * k);
        m.c.position.set(at.x + Math.cos(m.a) * rr, 30 * s, at.z + Math.sin(m.a) * rr); m.c.scale.setScalar(140 * s); m.c.material.uniforms.alpha.value = Math.sin(Math.PI * k) * 0.55; } } };
  },
  pillar(fx, s, at, r) {
    const beam = Tube(fx, 't_curtain', 1.8, { urep: 3, vrep: 2, fadeTop: false }), core = Tube(fx, 't_band', 2.4, { urep: 2, vrep: 3, fadeTop: false }), glow = Ring(fx, 't_glow', 1.2, { rows: [[0, 0.5, 1], [1, 0.5, 0]], vrep: 1 });
    const b = burstPart(fx, s * 1.1, r), ring = ringPart(fx, s, 200);
    return { dur: 2.2, f: t => {
      const a = fade(t, 0, 0.12, 0.7, 1.2), w = 40 * s * (1 - 0.6 * Math.max(0, (t - 0.7) / 0.5));
      beam.visible = core.visible = glow.visible = a > 0;
      beam.position.copy(at); beam.scale.set(w, 1400, w); beam.material.uniforms.alpha.value = a; beam.material.uniforms.off.value.set(t * 0.4, t * 2.2);
      core.position.copy(at); core.scale.set(w * 0.35, 1400, w * 0.35); core.material.uniforms.alpha.value = a; core.material.uniforms.off.value.set(0, t * 3);
      glow.position.copy(at).setY(2); glow.scale.set(150 * s, 1, 150 * s); glow.material.uniforms.alpha.value = a;
      b(t - 0.12, at.clone().setY(30)); ring(t - 0.12, at); } };
  },
  strike(fx, s, at, r) {
    const top = at.clone().add(new THREE.Vector3(-120 + r() * 240, 1100, -60)), bolts = [0, 1].map(() => Ribbon(fx, 't_glow', 3.0, 26)), halo = Ribbon(fx, 't_glow', 1.0, 26);
    const b = burstPart(fx, s, r), ring = ringPart(fx, s, 160, { dur: 0.5 }); let seed = 1, last = -1;
    return { dur: 1.6, f: t => {
      const flick = [[0, 0.07], [0.12, 0.19], [0.26, 0.36]].some(([a, c]) => t >= a && t < c);
      const id = Math.floor(t / 0.06); if (id !== last) { last = id; seed++; }
      const rr = rnd(seed * 97), pts = jag(top, at.clone().setY(60), 26, 70 * s, rr);
      bolts.forEach((m, i) => { m.visible = flick; if (flick) m.userData.set(i ? jag(pts[8], pts[8].clone().add(new THREE.Vector3(80 * s, -200 * s, 40)), 26, 30 * s, rr) : pts, 5 * s * (i ? 0.6 : 1)); });
      halo.visible = flick; if (flick) halo.userData.set(pts, 30 * s, () => 0.5);
      b(t - 0.02, at.clone().setY(50)); ring(t - 0.02, at); } };
  },
  chain(fx, s, at, r) {
    const nodes = [fx.wielderHand, at.clone().setY(80), ...fx.targets.slice(1, 3).map(p => p.clone().setY(80))];
    const links = [0, 1, 2].map(() => [Ribbon(fx, 't_glow', 3.0, 20), Ribbon(fx, 't_glow', 0.9, 20)]), hits = [0, 1, 2].map(() => burstPart(fx, s * 0.6, r)); let seed = 3;
    return { dur: 1.8, f: t => {
      seed = Math.floor(t / 0.05);
      links.forEach(([m, h], i) => {
        const t0 = i * 0.14, on = t >= t0 && t < t0 + 0.5 && ((t * 20) | 0) % 3 !== 0;
        m.visible = h.visible = on; if (!on) return;
        const pts = jag(nodes[i], nodes[i + 1], 20, 40 * s, rnd(seed * 31 + i));
        m.userData.set(pts, 4 * s); h.userData.set(pts, 22 * s, () => 0.6);
        hits[i](t - t0 - 0.02, nodes[i + 1]);
      });
      for (let i = 0; i < 3; i++) if (t < i * 0.14) hits[i](-1, nodes[i + 1]); } };
  },
  bolt(fx, s, at, r) { const b = boltPart(fx, s, r), hit = burstPart(fx, s * 0.8, r), to = at.clone().setY(70), dur = 0.55;
    return { dur: 1.9, f: t => { b(t, fx.wielderHand, to, dur, 30); hit(t - dur, to); } }; },
  meteor(fx, s, at, r) {
    const drops = [0, 1, 2].map(i => ({ b: boltPart(fx, s * 1.6, r), hit: burstPart(fx, s * 1.3, r), ring: ringPart(fx, s * 0.7, 260, { dur: 0.6 }),
      to: i === 0 ? at.clone().setY(20) : fx.targets[i].clone().setY(20), t0: i * 0.45 }));
    drops.forEach(d => d.from = d.to.clone().add(new THREE.Vector3(-500, 1100, -260)));
    return { dur: 2.8, f: t => drops.forEach(d => { d.b(t - d.t0, d.from, d.to, 0.5, 0); d.hit(t - d.t0 - 0.5, d.to.clone().setY(40)); d.ring(t - d.t0 - 0.5, d.to); }) };
  },
  cyclone(fx, s, at, r) {
    const funnels = [0, 1, 2].map(i => Tube(fx, i === 1 ? 't_smoke' : 't_curtain', i === 1 ? 0.35 : 0.55, { r0: 0.25, r1: 1.2, twist: 2.4, urep: 2, vrep: 1 }));
    const deb = Array.from({ length: 30 }, () => ({ c: Card(fx, 't_glow', 2.0), a: r() * Math.PI * 2, h: r(), sp: 2 + r() * 3 })), base = Ring(fx, 't_glow', 0.9, { rows: [[0, 0.5, 1], [1, 0.5, 0]], vrep: 1 });
    return { dur: 3.2, f: t => {
      const a = fade(t, 0, 0.4, 2.5, 3.1), p = at.clone().add(new THREE.Vector3(60 * s * Math.sin(t * 1.3), 0, 40 * s * Math.cos(t * 1.1)));
      funnels.forEach((m, i) => { m.visible = a > 0; m.position.copy(p); m.scale.set(80 * s * (1 + i * 0.18), 330 * s, 80 * s * (1 + i * 0.18)); m.rotation.y = -t * (3 + i * 1.4);
        m.material.uniforms.alpha.value = a * (i === 1 ? 0.7 : 1); m.material.uniforms.off.value.set(t * 0.2 * (i + 1), -t * 0.9); });
      base.visible = a > 0; base.position.copy(p).setY(2); base.scale.set(200 * s, 1, 200 * s); base.material.uniforms.alpha.value = a * 0.8;
      for (const d of deb) { const h = (d.h + t * 0.25) % 1, ang = d.a + t * d.sp, rr = 110 * s * (0.4 + 1.1 * h) * 0.9; d.c.visible = a > 0;
        d.c.position.set(p.x + Math.cos(ang) * rr, 380 * s * h, p.z + Math.sin(ang) * rr); d.c.scale.setScalar(8 * s); d.c.material.uniforms.alpha.value = a * (1 - h * 0.7); } } };
  },
  cloud(fx, s, at, r) {
    const puffs = Array.from({ length: 14 }, () => ({ c: Card(fx, 't_puff', 0.9, { va: 0.7 }), o: new THREE.Vector3((r() - 0.5) * 2, r() * 0.7, (r() - 0.5) * 2), d: r() * 0.5, sp: r() * 0.4 }));
    const motes = sparkSet(fx, 16, r, { speed: 40, size: 5, life: 2.2, up: 0.8 }), glow = Ring(fx, 't_glow', 0.7, { rows: [[0, 0.5, 1], [1, 0.5, 0]], vrep: 1 });
    return { dur: 3.0, f: t => {
      for (const p of puffs) { const k = (t - p.d) / 2.4; p.c.visible = k > 0 && k < 1; if (!p.c.visible) continue;
        p.c.position.set(at.x + p.o.x * 140 * s * (0.6 + 0.5 * k), 40 * s + p.o.y * 90 * s + 30 * s * k, at.z + p.o.z * 140 * s * (0.6 + 0.5 * k));
        p.c.scale.setScalar(170 * s * (0.6 + 0.6 * k)); p.c.material.uniforms.alpha.value = Math.sin(Math.PI * k) * 0.75; }
      sparksAt(motes, t - 0.3, at.clone().setY(30), s, -20);
      glow.position.copy(at).setY(2); glow.scale.set(220 * s, 1, 220 * s); glow.material.uniforms.alpha.value = fade(t, 0, 0.4, 2.0, 2.8) * 0.6; } };
  },
  pool(fx, s, at, r) {
    const flames = Array.from({ length: 22 }, () => ({ c: Card(fx, 't_spiritflame', 1.4, { flip: [4, 4] }), o: (() => { const a = r() * Math.PI * 2, d = Math.sqrt(r()); return new THREE.Vector3(Math.cos(a) * d, 0, Math.sin(a) * d); })(), ph: r() * 2, sz: 0.6 + r() * 0.7 }));
    const glow = Ring(fx, 't_glow', 1.4, { rows: [[0, 0.5, 1], [1, 0.5, 0]], vrep: 1 }), b = burstPart(fx, s, r);
    return { dur: 3.0, f: t => {
      const a = fade(t, 0.1, 0.35, 2.3, 2.9);
      for (const f of flames) { f.c.visible = a > 0; if (!a) continue; f.c.position.set(at.x + f.o.x * 150 * s, 26 * s * f.sz, at.z + f.o.z * 150 * s); f.c.scale.set(44 * s * f.sz, 64 * s * f.sz, 1);
        f.c.material.uniforms.alpha.value = a; setCell(f.c.material.uniforms, [4, 4], 20, t, f.ph); }
      glow.visible = a > 0; glow.position.copy(at).setY(2); glow.scale.set(190 * s, 1, 190 * s); glow.material.uniforms.alpha.value = a;
      b(t, at.clone().setY(40)); } };
  },
  drain(fx, s, at, r) {
    const n = 3, streams = Array.from({ length: n }, (_, i) => ({ m: Ribbon(fx, 't_trailfire', 1.6, 24, { clamp: true }), ph: i / n, side: (i - 1) * 60 }));
    const src = Card(fx, 't_glow', 1.6), dst = Card(fx, 't_glow', 1.8), motes = Array.from({ length: 12 }, (_, i) => ({ c: Card(fx, 't_glow', 2.4), ph: i / 12, k: i % n }));
    const A = at.clone().setY(80), B = fx.wielderHand;
    const curve = (st, f) => { const mid = A.clone().lerp(B, 0.5).add(new THREE.Vector3(0, 90 * s, st.side * s)); const v = 1 - f; return A.clone().multiplyScalar(v * v).add(mid.multiplyScalar(2 * v * f)).add(B.clone().multiplyScalar(f * f)); };
    return { dur: 2.4, f: t => {
      const a = fade(t, 0, 0.25, 1.9, 2.3);
      streams.forEach(st => { st.m.visible = a > 0; const pts = Array.from({ length: 24 }, (_, i) => curve(st, i / 23)); st.m.userData.set(pts, f => 7 * s * (0.6 + 0.4 * Math.sin(Math.PI * f)), f => a * Math.sin(Math.PI * f));
        st.m.material.uniforms.off.value.set(0, -t * 1.8 - st.ph); });
      motes.forEach(m => { const f = ((t * 0.7 + m.ph) % 1); m.c.visible = a > 0; m.c.position.copy(curve(streams[m.k], f)); m.c.scale.setScalar(12 * s); m.c.material.uniforms.alpha.value = a * Math.sin(Math.PI * f); });
      src.position.copy(A); src.scale.setScalar(90 * s * (0.8 + 0.2 * Math.sin(t * 9))); src.material.uniforms.alpha.value = a; src.visible = a > 0;
      dst.position.copy(B); dst.scale.setScalar(80 * s * (0.6 + 0.4 * a)); dst.material.uniforms.alpha.value = a * 0.8; dst.visible = a > 0; } };
  },
  bind(fx, s, at, r) {
    const circle = Ring(fx, 'runes', 2.2, { rows: [[0.55, 0, 0], [0.72, 0.4, 1], [0.86, 0.75, 1], [1, 1, 0]], vrep: 10 }), inner = Ring(fx, 't_glow', 0.8, { rows: [[0, 0.5, 1], [1, 0.5, 0]], vrep: 1 });
    const chains = [0, 1, 2, 3].map(i => ({ m: Ribbon(fx, 'stream_strip', 2.0, 16, { clamp: true }), a: i * Math.PI / 2 + 0.4 }));
    return { dur: 2.6, f: t => {
      const a = fade(t, 0, 0.25, 2.0, 2.5);
      circle.visible = inner.visible = a > 0; circle.position.copy(at).setY(3); circle.scale.set(95 * s, 1, 95 * s); circle.rotation.y = t * 0.6; circle.material.uniforms.alpha.value = a;
      inner.position.copy(at).setY(2); inner.scale.set(110 * s, 1, 110 * s); inner.material.uniforms.alpha.value = a * 0.7;
      chains.forEach(c => { const k = ease.out((t - 0.1) / 0.35); c.m.visible = a > 0 && k > 0; if (!c.m.visible) return;
        const base = at.clone().add(new THREE.Vector3(Math.cos(c.a + t * 0.6) * 90 * s, 4, Math.sin(c.a + t * 0.6) * 90 * s)), tip = at.clone().setY(70);
        const pts = Array.from({ length: 16 }, (_, i) => { const f = i / 15 * k; const p = base.clone().lerp(tip, f); p.y += Math.sin(Math.PI * f) * 30; return p; });
        c.m.userData.set(pts, 3.5 * s, () => a); c.m.material.uniforms.off.value.set(0, -t * 2); }); } };
  },
  freeze(fx, s, at, r) {
    const shards = Array.from({ length: 13 }, () => {
      const geo = new THREE.OctahedronGeometry(1, 0); geo.scale(0.35, 1.6, 0.35);
      const m = new THREE.MeshStandardMaterial({ color: new THREE.Color(COLS[fx.colKey] || '#8fd8ff').convertSRGBToLinear(), roughness: 0.15, metalness: 0.1, transparent: true, opacity: 0.55, emissive: new THREE.Color(COLS[fx.colKey] || '#8fd8ff').convertSRGBToLinear(), emissiveIntensity: 0.35 });
      const mesh = new THREE.Mesh(geo, m); fx.g.add(mesh); const a = r() * Math.PI * 2, d = 14 + r() * 16;
      return { mesh, a, d, h: 10 + r() * 100, tilt: (r() - 0.5) * 0.9, sz: 14 + r() * 18, t0: r() * 0.3 };
    });
    const mist = Array.from({ length: 6 }, () => ({ c: Card(fx, 't_puff', 0.7, { va: 0.6 }), o: new THREE.Vector3(r() - 0.5, r(), r() - 0.5) })), glint = Card(fx, 't_glow', 2.2);
    return { dur: 2.6, f: t => {
      const out = fade(t, 0, 0.01, 2.0, 2.5);
      for (const p of shards) { const k = ease.out((t - p.t0) / 0.3); p.mesh.visible = k > 0 && out > 0; if (!p.mesh.visible) continue;
        p.mesh.position.set(at.x + Math.cos(p.a) * p.d * s, p.h * s, at.z + Math.sin(p.a) * p.d * s); p.mesh.rotation.set(p.tilt, p.a, Math.cos(p.a) * 0.4);
        p.mesh.scale.setScalar(p.sz * s * k); p.mesh.material.opacity = 0.55 * out; }
      mist.forEach((m, i) => { const k = (t - 0.1) / 2.2; m.c.visible = k > 0 && k < 1; if (!m.c.visible) return; m.c.position.set(at.x + m.o.x * 80 * s, 20 + m.o.y * 90 * s, at.z + m.o.z * 80 * s); m.c.scale.setScalar(110 * s); m.c.material.uniforms.alpha.value = Math.sin(Math.PI * k) * 0.5; });
      glint.visible = t < 0.5; glint.position.copy(at).setY(70); glint.scale.setScalar(160 * s * ease.out(t / 0.15)); glint.material.uniforms.alpha.value = 1 - t / 0.5; } };
  },
  toll(fx, s, at, r) {
    const rings = [0, 1, 2].map(() => Tube(fx, 't_band', 2.0, { rows: 2, urep: 8, vrep: 1, fadeTop: false })), ground = ringPart(fx, s, 300, { dur: 0.9 }), flash = Card(fx, 't_glow', 2.2);
    return { dur: 2.0, f: t => {
      rings.forEach((m, i) => { const k = (t - i * 0.22) / 0.9; m.visible = k > 0 && k < 1; if (!m.visible) return; const rad = 320 * s * ease.out(k);
        m.position.copy(at).setY(60 * s); m.scale.set(rad, 28 * s * (1 - k * 0.5), rad); m.material.uniforms.alpha.value = (1 - k) ** 1.3; m.material.uniforms.off.value.set(t * 0.3, 0); });
      ground(t, at); flash.visible = t < 0.35; flash.position.copy(at).setY(70); flash.scale.setScalar(160 * s * ease.out(t / 0.1)); flash.material.uniforms.alpha.value = 1 - t / 0.35; } };
  },
  wave(fx, s, at, r) {
    const from = fx.wielder.clone(), dir = at.clone().sub(from).setY(0).normalize(), yaw = Math.atan2(dir.z, dir.x);
    const sheets = [0, 1].map(i => Tube(fx, i ? 't_smoke' : 't_curtain', i ? 0.6 : 1.5, { arc: 0.22, a0: -0.22 * Math.PI, seg: 24, urep: 2, vrep: 1 }));
    const dust = sparkSet(fx, 24, r, { speed: 120, size: 8, life: 1.0, up: 0.6 });
    return { dur: 1.8, f: t => {
      const k = t / 0.8, on = k >= 0 && k < 1;
      sheets.forEach((m, i) => { m.visible = on; if (!on) return; const rad = 60 + 520 * s * ease.out(k); m.position.copy(from).setY(0); m.rotation.y = -yaw;
        m.scale.set(rad, 90 * s * (1 - 0.4 * k) * (i ? 1.2 : 1), rad); m.material.uniforms.alpha.value = (1 - k) * (i ? 0.6 : 1); m.material.uniforms.off.value.set(0, -t * 2); });
      sparksAt(dust, t - 0.2, from.clone().addScaledVector(dir, 300 * s).setY(10), s, 60); } };
  },
  summon(fx, s, at, r) {
    const portal = Ring(fx, 'runes', 2.4, { rows: [[0.5, 0, 0], [0.7, 0.4, 1], [0.85, 0.75, 1], [1, 1, 0]], vrep: 12 }), disc = Ring(fx, 't_glow', 1.2, { rows: [[0, 0.5, 1], [1, 0.5, 0]], vrep: 1 });
    const col = Tube(fx, 't_curtain', 0.55, { urep: 4, vrep: 1 }), smoke = Array.from({ length: 6 }, () => ({ c: Card(fx, 't_puff', 0.5, { va: 0.5 }), a: r() * Math.PI * 2 }));
    const who = dummy(true); who.traverse(o => { if (o.material) { o.material = o.material.clone(); o.material.transparent = true; o.material.emissive = new THREE.Color(COLS[fx.colKey] || '#a8c4ff').convertSRGBToLinear(); o.material.emissiveIntensity = 0.5; } }); fx.g.add(who);
    return { dur: 3.0, f: t => {
      const a = fade(t, 0, 0.3, 2.3, 2.9), k = ease.inout((t - 0.5) / 0.9);
      portal.visible = disc.visible = col.visible = a > 0; portal.position.copy(at).setY(3); portal.scale.set(100 * s, 1, 100 * s); portal.rotation.y = -t * 0.8; portal.material.uniforms.alpha.value = a;
      disc.position.copy(at).setY(2); disc.scale.set(130 * s, 1, 130 * s); disc.material.uniforms.alpha.value = a * 0.8;
      col.position.copy(at); col.scale.set(60 * s, 220 * s, 60 * s); col.material.uniforms.alpha.value = 0.7 * fade(t, 0.2, 0.5, 0.9, 1.5); col.material.uniforms.off.value.set(t * 0.3, -t * 1.6);
      who.visible = t > 0.5; who.position.copy(at).setY(-130 * (1 - k)); who.traverse(o => { if (o.material) o.material.opacity = Math.min(1, k * 1.2) * (t > 2.4 ? Math.max(0, 1 - (t - 2.4) / 0.5) : 1); });
      smoke.forEach((m, i) => { const kk = (t - 0.4) / 2; m.c.visible = kk > 0 && kk < 1; if (!m.c.visible) return; m.c.position.set(at.x + Math.cos(m.a) * 70 * s, 30 + 120 * s * kk, at.z + Math.sin(m.a) * 70 * s); m.c.scale.setScalar(130 * s); m.c.material.uniforms.alpha.value = Math.sin(Math.PI * kk) * 0.5; }); } };
  },
  mark(fx, s, at, r) {
    const sign = Ring(fx, 'runes', 2.2, { rows: [[0.45, 0, 0], [0.65, 0.4, 1], [0.85, 0.75, 1], [1, 1, 0]], vrep: 6 }), halo = Card(fx, 't_glow', 1.2), feet = Ring(fx, 't_glow', 0.8, { rows: [[0, 0.5, 1], [1, 0.5, 0]], vrep: 1 });
    const motes = sparkSet(fx, 10, r, { speed: 30, size: 5, life: 2.0, up: 1 });
    return { dur: 2.6, f: t => {
      const a = fade(t, 0, 0.3, 2.0, 2.5);
      sign.visible = halo.visible = feet.visible = a > 0; sign.position.copy(at).setY(150 + 4 * Math.sin(t * 3)); sign.scale.set(46 * s, 1, 46 * s); sign.rotation.y = t * 1.2; sign.material.uniforms.alpha.value = a;
      halo.position.copy(at).setY(150); halo.scale.setScalar(70 * s); halo.material.uniforms.alpha.value = a * 0.6;
      feet.position.copy(at).setY(2); feet.scale.set(80 * s, 1, 80 * s); feet.material.uniforms.alpha.value = a * 0.7;
      sparksAt(motes, (t % 1.2), at.clone().setY(80), s, -30); } };
  },
  slash(fx, s, at, r) {
    const arcs = [0, 1].map(() => Ribbon(fx, 'flame_strip0', 1.8, 28, { clamp: true })), b = burstPart(fx, s * 0.7, r), from = fx.wielder.clone().setY(90);
    return { dur: 1.6, f: t => {
      arcs.forEach((m, i) => { const k = (t - i * 0.12) / 0.32; m.visible = k > 0 && k < 1.6; if (!m.visible) return;
        const c = at.clone().lerp(from, 0.35).setY(85), R = 150 * s, pts = [];
        for (let j = 0; j < 28; j++) { const f = j / 27, ang = (-0.9 + 1.8 * f) * (i ? -1 : 1), on = f <= Math.min(1, k); const p = c.clone().add(new THREE.Vector3(Math.sin(ang) * R * 0.6, Math.cos(ang * 1.3) * R * 0.25 * (i ? -1 : 1), Math.cos(ang) * R * 0.5)); pts.push(on ? p : pts[pts.length - 1] || p); }
        m.userData.set(pts, f => 14 * s * Math.sin(Math.PI * f), f => Math.max(0, 1 - Math.max(0, k - 1) / 0.6) * Math.sin(Math.PI * f));
        m.material.uniforms.off.value.set(0, t); });
      b(t - 0.22, at.clone().setY(70)); } };
  },
};

// ================================================================ each ability's own effect (Noah, 2026-11-25: "One custom effect each";
// "Same effect in doc and game") - the elements tools/abilityfx.py builds for the game, grown and faded here exactly as the game does
const AFX = { geos: null };
function afxGeos() { return AFX.geos || (AFX.geos = fetch('abil/geos.json').then(r => r.json())); }
function afxJitter(owner, i) { let h = 2166136261 >>> 0; for (const ch of new TextEncoder().encode(owner + '#' + i)) h = Math.imul(h ^ ch, 16777619) >>> 0; return 1 + ((h % 1000) / 1000 - 0.5) * 0.24; }
function afxCompose(ab, style, size) {   // (tools/abilityfx.py compose)
  const k = size / 100, out = [];
  const TILE = { fire: 'fill', ice: 'frostc', bolt: 'shock', caustic: 'swirl', squig: 'swirl', wisps: 'vapour', sparks: 'fill' };
  (AB_STYLES[style] || AB_STYLES.burst).forEach((e, i) => { const j = afxJitter(ab.owner, i); let tx = { A: ab.A, B: ab.B, G: 'glow', R: 'runes' }[e.slot];
    if (['sphere', 'ring', 'wall', 'column', 'spiral', 'disc', 'crescent'].includes(e.geo)) tx = TILE[tx] || tx;
    out.push(Object.assign({}, e, { tex: tx, s0: e.s0 * k, s1: e.s1 * k * j, grow: e.grow * j, life: e.life * j, fade: e.fade * j })); });
  return out;
}
function afxElement(G, e, col) {   // one element: its shape wearing its texture (game coordinates z up -> here y up)
  const g = G.geos[e.geo], grid = G.tex[e.tex] || null, fps = e.anim > 6 ? e.anim : 14;
  const mk = (verts, tris, bb) => {
    const pos = [], uv = [], va = [], idx = [];
    const um = Math.max(1e-6, ...verts.map(p => p[3])), vm = Math.max(1e-6, ...verts.map(p => p[4]));
    for (const p of verts) {
      if (bb) pos.push(p[0], p[2], 0); else pos.push(p[0], p[2], -p[1]);
      uv.push(grid && !bb ? p[3] / um : p[3], grid && !bb ? p[4] / vm : p[4]); va.push(p[5]);
    }
    for (const t of tris) idx.push(t[0], t[1], t[2]);
    const geo = new THREE.BufferGeometry(); geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3)); geo.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
    geo.setAttribute('va', new THREE.Float32BufferAttribute(va, 1)); geo.setIndex(idx);
    const m = fxLayer(new THREE.Mesh(geo, fxMat('fxa_' + e.tex, col, e.gain, { game: true, flip: grid || undefined })));
    m.userData.grid = grid; m.userData.fps = fps; return m;
  };
  const root = new THREE.Group(), parts = [], bbs = [];
  if (g.face === 'bbmany') {
    const nv = g.v.length / 5, nt = g.t.length / 5;
    for (let q = 0; q < 5; q++) {
      const vs = g.v.slice(q * nv, q * nv + nv), c = [0, 1, 2].map(k => vs.reduce((s, p) => s + p[k], 0) / nv);
      const m = mk(vs.map(p => [p[0] - c[0], p[1] - c[1], p[2] - c[2], p[3], p[4], p[5]]), g.t.slice(q * nt, q * nt + nt).map(t => t.map(i => i - q * nv)), true);
      const holder = new THREE.Group(); holder.position.set(c[0], c[2], -c[1]); holder.add(m); root.add(holder); parts.push(m); bbs.push(m);
    }
  } else { const m = mk(g.v, g.t, g.face === 'bb'); root.add(m); parts.push(m); if (g.face === 'bb') bbs.push(m); }
  return { root, parts, bbs, e };
}
// (Noah, 2026-11-25) an ability's picked option as beats for the player: its parts where the game plays them - where it strikes,
// leaping on for a chain, beams for a pull or a drain, or on each beat of its show - in its colour, at his sizes
function optPal(pk, theme, p) {
  const native = ((REALFX.parts[theme] || {}).native || []).includes(pk.col) && pk.vib === 100;
  if (p.nat && p.model.indexOf('labyrinthianshockwave') < 0) return null;
  if (native && p.model.indexOf('labyrinthianshockwave') < 0) return null;
  const hex = String(pk.col).startsWith('#') ? pk.col : (COLS[pk.col] || '#ffffff');
  if (!String(pk.col).startsWith('#') && pk.vib === 100) return REALFX.rampOf[pk.col] || pk.col;
  return 'hex:' + hex.slice(1) + ':' + pk.vib;
}
function optRows(key, pk, at, spots) {
  const info = (REALFX.opts || {})[key]; if (!info) return null;
  const o = info.opts.find(x => x.id === pk.opt) || info.opts[0];
  const k = pk.size / 100, area = pk.area / 100;
  const themeOf = m => Object.keys(REALFX.parts).find(t => Object.entries(REALFX.parts[t]).some(([r, q]) => r !== 'native' && q.model && q.model.toLowerCase() === m));
  const parts = (kk, ev) => o.parts.map(p0 => {
    let p = p0, th = info.theme;
    if (ev && p0.role) { const et = themeOf(ev); const q = et && REALFX.parts[et][p0.role]; if (q) { p = Object.assign({}, p0, { model: q.model.toLowerCase(), s: q.s * p0.k }); th = et; } }
    return Object.assign({}, p, { s: p.s * k * kk * (p.ground ? area : 1), pal: ev && pk.col === pk.defCol && pk.vib === 100 ? null : optPal(pk, th, p) });
  });
  const beam = (o.link || null) && [{ model: o.link.toLowerCase(), s: 0.6 * k, delay: 0, ground: false, pal: optPal(pk, info.theme, { model: o.link }) }];
  const show = (REALFX.shows || {})[key];
  if (show) {   // (its show's beats: each burst takes the option's parts; what flies stays)
    const each = /^(wand|mutagen)_/.test(key);   // (beats each their own element: the option's shape in each beat's element)
    const rows = show.map(e => e.fly ? e : Object.assign({}, e, { parts: parts(e.k || 1, each && e.parts[0] ? e.parts[0].model : null) }));
    rows.fromShow = true; return rows;
  }
  const t0 = spots[0].clone().setY(70), y = p => p.clone().setY(70);
  const hit = at.distanceTo(spots[0]) < 1 ? y(at) : at.clone().setY(1);
  const rows = [{ t: 0, at: hit, parts: parts(1) }];
  const pat = info.pattern;
  if (pat === 'chain') {
    [[spots[0], spots[1], 0.1], [spots[1], spots[2], 0.22]].forEach(([a, b, t], i) => {
      if (beam) rows.push({ t, fly: [y(a), y(b), 0, 0], parts: beam });
      rows.push({ t: t + 0.05, at: y(b), parts: parts(0.8 - 0.1 * i) });
    });
  } else if (pat === 'arcs') {
    spots.forEach((p, i) => { if (p.distanceTo(at) > 420 * area) return; if (beam) rows.push({ t: 0.05 + 0.04 * i, fly: [at.clone().setY(80), y(p), 0, 0], parts: beam }); rows.push({ t: 0.08 + 0.04 * i, at: y(p), parts: parts(0.6) }); });
  } else if (pat === 'pullbeam') {
    spots.forEach((p, i) => { if (i === 0 || p.distanceTo(spots[0]) > 480 * area) return; if (beam) rows.push({ t: 0.1, fly: [t0, y(p), 0, 0], parts: beam }); });
  } else if (pat === 'leech') {
    if (beam) rows.push({ t: 0.1, fly: [t0, SPOT.wielder.clone().add(new THREE.Vector3(30, 80, -10)), 0, 0], parts: beam });
  }
  return rows;
}
// (Noah, 2026-11-25: "can you make the preview 3D so I can zoom in and rotate to get a different angle?") drag to turn round the
// scene, the wheel or a pinch to zoom, a double click (or tap) to put it back
function orbitCam(cv, cam, target, home) {
  const o = { az: home.az, el: home.el, r: home.r };
  const place = () => { const ce = Math.cos(o.el); cam.position.set(target.x + o.r * ce * Math.sin(o.az), target.y + o.r * Math.sin(o.el), target.z + o.r * ce * Math.cos(o.az)); cam.lookAt(target); cam.updateMatrixWorld(); };
  const pts = new Map(); let pinch = null;
  cv.style.cursor = 'grab'; cv.style.touchAction = 'pan-y';
  cv.addEventListener('pointerdown', e => { pts.set(e.pointerId, { x: e.clientX, y: e.clientY, az: o.az, el: o.el }); cv.setPointerCapture(e.pointerId); cv.style.cursor = 'grabbing';
    if (pts.size === 2) { const [a, b] = [...pts.values()]; pinch = { d: Math.hypot(a.x - b.x, a.y - b.y), r: o.r }; } });
  cv.addEventListener('pointermove', e => { const p = pts.get(e.pointerId); if (!p) return;
    if (pts.size === 2 && pinch) { p.x = e.clientX; p.y = e.clientY; const [a, b] = [...pts.values()]; o.r = Math.max(home.r * 0.25, Math.min(home.r * 2.5, pinch.r * pinch.d / Math.max(10, Math.hypot(a.x - b.x, a.y - b.y)))); place(); return; }
    o.az = p.az - (e.clientX - p.x) * 0.008; o.el = Math.max(0.04, Math.min(1.45, p.el + (e.clientY - p.y) * 0.006)); place(); });
  const up = e => { pts.delete(e.pointerId); if (pts.size < 2) pinch = null; cv.style.cursor = 'grab'; };
  cv.addEventListener('pointerup', up); cv.addEventListener('pointercancel', up);
  cv.addEventListener('wheel', e => { e.preventDefault(); o.r = Math.max(home.r * 0.25, Math.min(home.r * 2.5, o.r * Math.exp(e.deltaY * 0.0012))); place(); }, { passive: false });
  cv.addEventListener('dblclick', () => { Object.assign(o, home); place(); });
  place();
  return { place, o };
}
// the beats a loop plays: the swing (or slam, cast, throw) from the player's acts, the hit after it, each impact of the effect
// (a bolt's where it lands), and what lingers after the first
function beatsOf(pact, show, dur) {
  const B = [];
  for (const a of pact || []) {
    if (a.k === 'swing') { B.push({ t: a.t, k: 'swing' }); B.push({ t: a.t + 0.09, k: 'hit' }); }
    else if (a.k === 'slam') { B.push({ t: a.t, k: 'heavy' }); B.push({ t: a.t + 0.13, k: 'hit' }); }
    else B.push({ t: a.t, k: 'cast' });
  }
  const imp = show ? show.map(e => e.fly && e.fly[2] ? e.t + e.fly[2] : e.t + 0.02) : [0.1];
  imp.sort((a, b) => a - b); let last = -1, n = 0;
  for (const t of imp) { if (t - last < 0.12 || n >= 6) continue; B.push({ t, k: 'impact' }); last = t; n++; }
  if (imp.length) B.push({ t: imp[0] + 0.05, k: 'linger', d: Math.max(1.2, (dur || 2) - imp[0]) });
  return B;
}
function abilityView(cv) {
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x070a12); scene.fog = new THREE.FogExp2(0x070a12, 0.00045);
  const cam = new THREE.PerspectiveCamera(38, GL.W / GL.H, 5, 8000);
  { const tgt = new THREE.Vector3(10, 70, -30), d = new THREE.Vector3(-70, 250, 560).sub(tgt), r = d.length();
    orbitCam(cv, cam, tgt, { az: Math.atan2(d.x, d.z), el: Math.asin(d.y / r), r }); }
  scene.add(new THREE.HemisphereLight(0x6f84b8, 0x14110c, 0.45));
  const moon = new THREE.DirectionalLight(0xbcd0ff, 0.75); moon.position.set(-600, 900, 500); scene.add(moon);
  const gm = new THREE.Mesh(new THREE.CircleGeometry(3000, 48), new THREE.MeshStandardMaterial({ map: GL.ok ? groundTex() : null, roughness: 0.95 })); gm.rotation.x = -Math.PI / 2; scene.add(gm);
  const you = dummy(true); you.position.copy(SPOT.wielder); you.rotation.y = 0.9; scene.add(you);
  // (Noah, 2026-11-25: you, Skyrim's male Nord in the weapon's class set, holding it - zombies.js plrFor / plrPose)
  let plr = null, plrWant = '';
  const pdir = SPOT.t0.clone().sub(SPOT.wielder).setY(0).normalize(), pinfo = { home: SPOT.wielder.clone().setY(0), face: Math.atan2(-pdir.x, -pdir.z) };
  const usePlayer = key => {
    if (!GL.ok || !key) return;
    const cfg = plrFor(key), want = cfg.cls + '|' + (cfg.weapon || '');
    if (want === plrWant) return; plrWant = want;
    Promise.all([zomData(cfg.cls, 'plr'), cfg.weapon ? loadWeapon(cfg.weapon).catch(() => null) : null]).then(([[sk, body], wd]) => {
      if (plrWant !== want) return;
      const z = makeZombie(sk, body, 0.5);
      if (wd && z.bones.WEAPON) { const wm = weaponModel(wd).g; wm.scale.setScalar((wd.scale || 1) * (SCYTHES.includes(cfg.weapon) ? 1.25 : 1)); z.bones.WEAPON.add(wm); z.weapon = wm; z.thrown = cfg.weapon === 'javelin'; }
      if (plr) scene.remove(plr.holder); plr = z; scene.remove(you); scene.add(z.holder); plrPose(z, 0, [], pinfo);
    }).catch(e => console.warn('player', e));
  };
  const spots = [SPOT.t0, ...SPOT.others];
  const dead = spots.map(p => { const d = dummy(false); d.position.copy(p); scene.add(d); return d; });
  // (Noah, 2026-11-25: the night's own dead, reacting to each ability as the game makes them - zombies.js)
  const zs = [], zinfo = spots.map(p => { const d = SPOT.wielder.clone().sub(p).setY(0).normalize(); return { home: p.clone().setY(0), from: SPOT.wielder.clone(), face: Math.atan2(-d.x, -d.z) }; });
  if (GL.ok) spots.forEach((p, i) => { const key = ZOM_KEYS[i % ZOM_KEYS.length];
    zomData(key).then(([sk, body]) => { const z = makeZombie(sk, body, (i * 0.37 + 0.11) % 1); zs[i] = z; scene.remove(dead[i]); scene.add(z.holder); zPose(z, [], 0, zinfo[i]); }).catch(e => console.warn('zombie', key, e)); });
  const light = new THREE.PointLight(0xffffff, 0, 900, 2); scene.add(light);   // (the effect's own light on the ground and the dead)
  const v = { cv, scene, cam, ready: false, visible: false, bloom: 1.1, fx: null, light };
  v.tick = t => {
    if (!v.fx) { zs.forEach((z, i) => z && zPose(z, [], t, zinfo[i])); if (plr) plrPose(plr, t, [], pinfo); return; }
    v._now = t;
    // (Noah, 2026-11-25: hit feel) a hit-stop holds the whole picture still for its milliseconds; then it plays on from there
    let tt;
    if (v._hold && t < v._hold.until) tt = v._hold.tt;
    else { if (v._hold) { v.fx.t0 += v._hold.until - v._hold.from; v._hold = null; } tt = window.__abT != null ? window.__abT : (t - v.fx.t0) % (v.fx.dur + 0.6); }
    if (v._sh) { cam.position.sub(v._sh); v._sh = null; }
    if (v._shake && t < v._shake.until) { const k = (v._shake.until - t) / v._shake.len, a = v._shake.amp * k * k; v._sh = new THREE.Vector3((Math.random() - 0.5) * a, (Math.random() - 0.5) * a, (Math.random() - 0.5) * a * 0.5); cam.position.add(v._sh); }
    v.fx.f(tt);
    // (Noah, 2026-11-25: the sound workshop - each swing, hit, cast and impact heard on its beat)
    if (v.fx !== v._bfx) { v._bfx = v.fx; v.beats = beatsOf(v.pact, v.fx.show, v.fx.dur); v._ptt = null; }
    if (v.onBeat && v.beats) { const p = v._ptt; if (p != null) for (const b of v.beats) if (tt >= b.t && (p < b.t || tt < p)) v.onBeat(b); v._ptt = tt; }
    zs.forEach((z, i) => z && zPose(z, (v.rx && v.rx[i]) || [], tt, zinfo[i]));
    if (plr) { plrPose(plr, tt, v.pact || [], pinfo); if (plr.weapon && plr.thrown) plr.weapon.visible = !(v.pact || []).some(a => a.k === 'throw' && tt > a.t + 0.06 && tt < a.t + 1.3); }
    for (const b of v.fx.bbs) { b.quaternion.copy(cam.quaternion); if (b.parent && b.parent.parent) { const q = new THREE.Quaternion(); b.parent.getWorldQuaternion(q); b.quaternion.premultiply(q.invert()); } }
    if (v.fx.light) { light.intensity = v.fx.light(tt) * 2.2; light.position.copy(v.fx.lightFollow ? v.fx.lightFollow(tt) : v.fx.lightAt); } else light.intensity = 0;
  };
  v.hitstop = ms => { if (ms > 0 && v._now != null && !v._hold) v._hold = { from: v._now, until: v._now + ms / 1000, tt: v._ptt == null ? 0 : v._ptt }; };
  v.shake = (pct, len) => { if (pct > 0 && v._now != null) v._shake = { until: v._now + (len || 0.3), len: len || 0.3, amp: pct * 0.6 }; };
  const clear = () => { if (v.fx) { scene.remove(v.fx.g); v.fx.g.traverse(o => { if (o.geometry) o.geometry.dispose(); if (o.material && o.material.dispose) o.material.dispose(); }); v.fx = null; } };
  v.play = async (style, colKey, sizePct, where, ab) => {
    const token = (v.token = (v.token || 0) + 1);
    const at = where === 'self' ? SPOT.wielder.clone().add(new THREE.Vector3(60, 0, -40)) : where === 'area' ? new THREE.Vector3(60, 0, -60) : SPOT.t0.clone();
    v.rx = zReactions(v.key, at, spots, SPOT.wielder);
    v.pact = plrActs(v.key || '', style, ab && ab.show); usePlayer(v.key);
    if (ab && ab.real) {   // (its real parts, played as the game plays them: real.js)
      let D;
      try { D = await realData(); } catch (e) { D = null; }
      if (token !== v.token) return;
      if (D) {
        clear();
        const parts = realCompose(ab.real, style, colKey, sizePct || 100).filter(x => D[x.model]);
        const g = new THREE.Group(), rng = rnd(style.length * 977 + 13);
        const toward = Math.atan2(-(at.z - SPOT.wielder.z), at.x - SPOT.wielder.x);
        const sts = parts.map((x, i) => {
          const st = realPart(D[x.model], Object.assign({ all: D }, x), rng);
          st.root.position.set(at.x, x.ground ? 1 : 70, at.z); st.root.rotation.y = i * 2.1 + 0.7 + toward * 0; st.root.scale.setScalar(x.s);
          st.delay = x.delay; g.add(st.root); return st;
        });
        const dur = Math.min(5.5, Math.max(1.2, ...sts.map(s2 => s2.delay + Math.min(4, s2.dur) + 0.6)));
        let last = -1;
        const step = tt => {
          if (tt < last) { const keep = sts.slice(); sts.length = 0; g.clear(); keep.forEach((s2, i) => { const x = parts[i]; const n = realPart(D[x.model], Object.assign({ all: D }, x), rnd(style.length * 977 + 13 + i)); n.root.position.copy(s2.root.position); n.root.rotation.copy(s2.root.rotation); n.root.scale.copy(s2.root.scale); n.delay = s2.delay; g.add(n.root); sts.push(n); }); last = -1; }
          const from = Math.max(0, last);
          for (let t1 = from; t1 < tt - 1e-6;) {
            const t2 = Math.min(tt, t1 + 1 / 30), dt = t2 - t1;
            for (const s2 of sts) { const lt = t2 - s2.delay; s2.root.visible = lt >= 0; if (lt >= 0) realStep(s2, lt, Math.min(dt, lt), cam); }
            t1 = t2;
          }
          last = tt;
        };
        const fx = { g, bbs: [], dur, t0: performance.now() / 1000, lightAt: at.clone().setY(70),
          light: t => Math.max(0, 1 - t / Math.max(0.4, dur * 0.5)) * (t > 0 ? 0.8 : 0), f: step, real: parts };
        light.color.set(COLS[colKey] || colKey || '#ffffff');
        scene.add(g); v.fx = fx; v.ready = true; return;
      }
    }
    if (ab && ab.pick) {   // (Noah, 2026-11-25: one of the ability's own five options, with his sliders - tools/abilityopts.py)
      const rows = optRows(v.key, ab.pick, at, spots);
      if (rows) { const pk = ab.pick; ab = { show: rows, mod: { bright: pk.bright / 100, density: pk.density / 100, linger: pk.linger / 100, speed: pk.speed / 100, light: pk.light / 100 } }; v.rx = zReactions(v.key, at, spots, SPOT.wielder, pk.area / 100); v.pact = plrActs(v.key || '', style, rows.fromShow ? rows : null); }
      else ab = null;
    }
    if (ab && ab.show) {   // (its real parts on the beats and paths the game gives them: a bolt from the hand, a bounce, a sunbeam - tools/docshow.py)
      let D;
      try { D = await realData(); } catch (e) { D = null; }
      if (token !== v.token) return;
      if (D) {
        clear();
        const g = new THREE.Group(), V = (x, y, z) => new THREE.Vector3(x, y, z);
        const hand = SPOT.wielder.clone().add(V(30, 80, -10)), tg = [SPOT.t0, ...SPOT.others];
        const aim = SPOT.t0.clone().sub(SPOT.wielder).setY(0).normalize();
        const spot = (name, air) => {
          if (name && name.isVector3) return name.clone();
          const [k, n] = name.split(':');
          if (k === 'hand') return hand.clone();
          if (k === 'sky') return spot(n, true).add(V(-160, 1300, -90));
          if (/^t\dg?$/.test(k)) return tg[+k[1]].clone().setY(k.endsWith('g') ? 1 : 70);
          if (k === 'area') return V(60, 1, -60).add([V(0, 0, 0), V(-150, 0, 70), V(140, 0, 90)][+(n || 0)]);
          if (k === 'line') return SPOT.wielder.clone().add(aim.clone().multiplyScalar(110 + 62 * +n)).setY(1);
          return at.clone().setY(1);
        };
        let jav = null;
        loadWeapon('javelin').then(d => { jav = weaponModel(d).g; jav.scale.setScalar(d.scale || 1); }).catch(() => {});
        const NEG_Z = V(0, 0, -1), UP = V(0, 1, 0);
        let sts = [], jg = [];
        const javs = new THREE.Group();
        const make = () => {
          g.clear(); g.add(javs); sts = []; jg = [];
          const rng = rnd(style.length * 977 + 13);
          ab.show.forEach((e, ei) => {
            const from = e.fly ? spot(e.fly[0]) : null, to = e.fly ? spot(e.fly[1]) : spot(e.at);
            const beam = e.fly && !e.fly[2];
            for (const [pi, x] of e.parts.entries()) {
              if (!D[x.model]) continue;
              const st = realPart(D[x.model], Object.assign({ all: D, mod: ab.mod }, x), rng);
              st.delay = e.t + (x.delay || 0); st.e = e; st.from = from; st.to = to; st.beam = beam;
              st.root.scale.setScalar(x.s);
              if (beam) { const d = to.clone().sub(from); st.root.position.copy(from); st.root.quaternion.setFromUnitVectors(NEG_Z, d.clone().normalize()); st.root.scale.set(x.s, x.s, d.length() / 512); }
              else if (!e.fly) { st.root.position.copy(to); if (x.ground) st.root.position.y = 1; st.root.rotation.y = (ei * 2.1 + pi * 1.3 + 0.7) % 6.283; }
              g.add(st.root); sts.push(st);
            }
            if (e.jav) jg.push(e);
          });
        };
        make();
        const flyAt = (e, k) => { const a = spot(e.fly[0]), b = spot(e.fly[1]), p = a.lerp(b, k); p.y += Math.sin(Math.PI * k) * (e.fly[3] || 0); return p; };
        const SP = (ab.mod && ab.mod.speed) || 1, LG = (ab.mod && ab.mod.linger) || 1;
        const dur = Math.min(9, Math.max(1.4, ...sts.map(s2 => s2.delay + (s2.e.fly && s2.e.fly[2] ? s2.e.fly[2] : Math.min(3.5, s2.dur) * (0.35 + 0.65 * LG) / SP) + 0.5)));
        let last = -1;
        const step = tt => {
          if (tt < last) { make(); last = -1; }
          const from = Math.max(0, last);
          for (let t1 = from; t1 < tt - 1e-6;) {
            const t2 = Math.min(tt, t1 + 1 / 30), dt = t2 - t1;
            for (const s2 of sts) {
              const lt = t2 - s2.delay, e = s2.e;
              if (e.fly && e.fly[2]) {   // (a bolt in flight: carried along its path, point first; gone where it strikes)
                const k = lt / e.fly[2]; s2.root.visible = k >= 0 && k < 1; if (!s2.root.visible) continue;
                const p = flyAt(e, k), q = flyAt(e, Math.min(1, k + 0.03)).sub(flyAt(e, Math.max(0, k - 0.03)));
                s2.root.position.copy(p); if (q.lengthSq() > 1e-6) s2.root.quaternion.setFromUnitVectors(NEG_Z, q.normalize());
              } else if (s2.beam) { s2.root.visible = lt >= 0 && lt < 1.6; if (!s2.root.visible) continue; }
              else { s2.root.visible = lt >= 0; if (!s2.root.visible) continue; }
              // (his speed slider plays it faster or slower; his linger slider holds its second half longer)
              const sp = (ab.mod && ab.mod.speed) || 1, lg = (ab.mod && ab.mod.linger) || 1, a0 = 0.35 * Math.min(3.5, s2.dur || 1);
              const at = lt * sp, et = at <= a0 ? at : a0 + (at - a0) / lg, ed = (at <= a0 ? 1 : 1 / lg) * sp;
              realStep(s2, et, Math.min(dt, lt) * ed, cam);
            }
            t1 = t2;
          }
          // the Wyrmspine Javelin itself, in flight on its bolt
          javs.clear();
          if (jav) for (const e of jg) { const k = (tt - e.t) / e.fly[2]; if (k < 0 || k >= 1) continue;
            const j = jav.clone(), p = flyAt(e, k), q = flyAt(e, Math.min(1, k + 0.03)).sub(flyAt(e, Math.max(0, k - 0.03)));
            j.position.copy(p); if (q.lengthSq() > 1e-6) j.quaternion.setFromUnitVectors(UP, q.normalize()); javs.add(j); }
          last = tt;
        };
        const fx = { g, bbs: [], dur, t0: performance.now() / 1000, lightAt: at.clone().setY(70),
          light: t => (t > 0 ? 1.1 * Math.pow((ab.mod && ab.mod.light) != null ? ab.mod.light : 1, 1.2) * Math.max(0.25, 1 - t / Math.max(0.6, dur)) * 1.6 : 0), f: step, show: ab.show };
        light.color.set(COLS[colKey] || colKey || '#ffffff');
        scene.add(g); v.fx = fx; v.ready = true; return;
      }
    }
    if (!ab || ab.real || ab.show) {   // (an ability with no effect of its own: the style's sketch)
      clear();
      const fx = { g: new THREE.Group(), bbs: [], col: colKey, colKey, cam, wielder: SPOT.wielder.clone(), wielderHand: SPOT.wielder.clone().add(new THREE.Vector3(30, 80, -10)), targets: [SPOT.t0, ...SPOT.others] };
      const built = (STYLE2[style] || STYLE_FN[style] || STYLE2.burst)(fx, (sizePct || 100) / 100, at, rnd(style.length * 977 + 13));
      fx.f = built.f; fx.dur = built.dur; fx.light = built.light; fx.lightAt = built.lightAt; fx.lightFollow = built.lightFollow; fx.t0 = performance.now() / 1000;
      light.color.set(COLS[colKey] || colKey || '#ffffff'); scene.add(fx.g); v.fx = fx; v.ready = true; return;
    }
    const G = await afxGeos(); if (token !== v.token) return;
    clear();
    const els = afxCompose(ab, style, sizePct || 100).map(e => afxElement(G, e, colKey));
    const g = new THREE.Group(), bbs = [];
    const toward = Math.atan2(-(at.z - SPOT.wielder.z), at.x - SPOT.wielder.x);
    els.forEach((x, i) => { g.add(x.root); bbs.push(...x.bbs); x.yaw = x.e.face ? toward - Math.PI / 2 : i * 2.1 + 0.7; });
    const dur = Math.max(...els.map(x => x.e.delay + x.e.life));
    const fx = { g, bbs, dur, t0: performance.now() / 1000, lightAt: at.clone().setY(80),
      light: t => Math.max(0, 1 - t / Math.max(0.3, dur * 0.6)) * (t > 0 ? 1 : 0),
      f: t => {
        for (const x of els) {
          const e = x.e, lt = t - e.delay, on = lt >= 0 && lt < e.life;
          x.root.visible = on; if (!on) continue;
          const gg = Math.min(1, lt / Math.max(0.01, e.grow)), ee = 1 - (1 - gg) * (1 - gg), sc = e.s0 + (e.s1 - e.s0) * ee;
          const a = lt <= e.fade ? 1 : Math.max(0, 1 - (lt - e.fade) / Math.max(0.01, e.life - e.fade));
          x.root.position.set(at.x, e.ground ? 2 : 70 + (e.z - 70), at.z); x.root.rotation.set(0, x.yaw, 0); x.root.scale.setScalar(sc);
          for (const m of x.parts) {
            const u = m.material.uniforms; u.alpha.value = a;
            if (m.userData.grid) setCell(u, m.userData.grid, m.userData.fps, lt, 0, true);
            else if (e.anim) u.off.value.set(0, -(lt / e.anim) % 1);
          }
        }
      } };
    light.color.set(COLS[colKey] || colKey || '#ffffff');
    scene.add(g); v.fx = fx; v.ready = true;
  };
  return GL.add(v);
}

// ================================================================ the shared database: every choice saved for me to build from
const DB = (function () {
  let db = null; const subs = {}; const data = {}, local = {};   // (local: what you picked here, seen at once even before - or without - its save)
  const ready = (async () => {
    try { db = window.claude && window.claude.use ? await window.claude.use('db') : null; } catch (e) { db = null; }
    return db;
  })();
  return {
    // (CARRY: what you set in the last doc, this one's starting point until you change it here)
    ready, get: (c, id) => { const a = (data[c] || {})[id], b = (local[c] || {})[id]; return (!b ? a : !a ? b : (a.savedAt || '') >= (b.savedAt || '') ? a : b) || (CARRY[c] || {})[id]; },
    all: c => Object.assign({}, CARRY[c] || {}, local[c] || {}, data[c] || {}),
    on(c, fn) { (subs[c] = subs[c] || []).push(fn); ready.then(d => { if (!d) { fn(null, 'offline'); return; }
      d.collection(c).onSnapshot(snap => { data[c] = {}; snap.docs.forEach(x => { const v = x.data(); if (v) data[c][x.id] = v; }); (subs[c] || []).forEach(f => f(data[c])); }, () => fn(null, 'error')); }); },
    async set(c, id, row) {
      row.savedAt = new Date().toISOString(); (local[c] = local[c] || {})[id] = row;
      const d = await ready; if (!d) throw { code: 'offline' };
      await d.collection(c).doc(id).set(row); (data[c] = data[c] || {})[id] = row; return row;
    },
    can: () => !!db,
  };
})();
function saveMsg(el, e) { el.textContent = !e ? 'Saved.' : e.code === 'not_granted' ? 'You can view this page but not save on it.' : e.code === 'offline' ? 'Saving works when this page is opened in Claude.' : 'That didn\'t save. Check your connection and try again.'; }
