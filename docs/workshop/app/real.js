// ================================================================ the real parts, played as the game plays them
// (Noah, 2026-11-25: "Build from real parts") each part is the effect itself, read out of Skyrim's, Dawnguard's, Dragonborn's,
// Apocalypse's and Mysticism's own files in your game (tools/realpreview.py): its node tree and the keys that move it, its
// effect meshes, and its particle systems - every particle born where its emitter says, at its speed and spread, then moved,
// grown, spun, coloured and faded by that system's own settings, in its own textures (or our ramp, as the game tints it).
const REALD = { data: null, tex: {}, pal: {} };
function realData() { if (!REALD.data) REALD.data = fetch('real/parts.json').then(r => { if (!r.ok) throw new Error('parts'); return r.json(); }); return REALD.data; }
function realTex(name, clamp) {
  const k = name + (clamp ? '|c' : '');
  if (!REALD.tex[k]) { const t = new THREE.TextureLoader().load('real/tex/' + name); t.wrapS = t.wrapT = clamp ? THREE.ClampToEdgeWrapping : THREE.RepeatWrapping; t.flipY = false; REALD.tex[k] = t; }
  return REALD.tex[k];
}
// (Noah, 2026-11-25: "I should be able to change it any color" ... "the vibrance") a ramp for any colour: our white ramp's own
// shape (its rows and alpha), recoloured dark -> the colour -> white, its colour's saturation scaled by the vibrance
function realHexPal(hex, vib) {
  const c = document.createElement('canvas'); c.width = 128; c.height = 32;
  const t = new THREE.CanvasTexture(c); t.wrapS = t.wrapT = THREE.ClampToEdgeWrapping; t.flipY = false;
  const col = new THREE.Color('#' + hex), hsl = {}; col.getHSL(hsl);
  const v = vib / 100; col.setHSL(hsl.h, Math.max(0, Math.min(1, hsl.s * v)), hsl.l);
  const base = [col.r, col.g, col.b];
  const stops = [[0, [0, 0, 0]], [0.18, base.map(x => x * 0.2)], [0.4, base.map(x => x * 0.68)], [0.62, base], [0.82, base.map(x => x + (1 - x) * 0.55)], [1, [1, 1, 1]]];
  const ramp = w => { for (let i = 1; i < stops.length; i++) if (w <= stops[i][0]) { const [a, A] = stops[i - 1], [b, B] = stops[i], f = (w - a) / (b - a); return A.map((x, j) => x + (B[j] - x) * f); } return [1, 1, 1]; };
  const img = new Image(); img.onload = () => { const x = c.getContext('2d'); x.drawImage(img, 0, 0, 128, 32); const d = x.getImageData(0, 0, 128, 32);
    for (let i = 0; i < d.data.length; i += 4) { const w = d.data[i] / 255, r = ramp(w); d.data[i] = r[0] * 255; d.data[i + 1] = r[1] * 255; d.data[i + 2] = r[2] * 255; }
    x.putImageData(d, 0, 0); t.needsUpdate = true; };
  img.src = 'real/pal/white.png';
  return t;
}
function realPalTex(name) {
  if (!REALD.pal[name] && name.startsWith('hex:')) { const [, hex, vib] = name.split(':'); REALD.pal[name] = realHexPal(hex, isNaN(+vib) ? 100 : +vib); }
  if (!REALD.pal[name]) { const t = new THREE.TextureLoader().load(name.indexOf('/') >= 0 ? name : 'real/pal/' + name + '.png'); t.wrapS = t.wrapT = THREE.ClampToEdgeWrapping; t.flipY = false; REALD.pal[name] = t; }
  return REALD.pal[name];
}
const REAL_VS = `attribute vec4 rgba; varying vec2 vUv; varying vec4 vC; varying vec3 vN; varying vec3 vV; varying float vWY;
void main(){ vUv = uv; vC = rgba; vWY = (modelMatrix * vec4(position,1.0)).y; vec4 mv = modelViewMatrix * vec4(position,1.0); vV = -mv.xyz; vN = normalize(normalMatrix * normal); gl_Position = projectionMatrix * mv; }`;
const REAL_FS = `uniform sampler2D map; uniform sampler2D pal; uniform float hasMap, usePal, palA, alpha, useFall; uniform vec4 base; uniform vec2 uvOff, uvSc; uniform vec4 fall; uniform float soft; uniform float gain; uniform float palG; uniform float palFlip;
varying vec2 vUv; varying vec4 vC; varying vec3 vN; varying vec3 vV; varying float vWY;
void main(){
  vec4 tx = hasMap > 0.5 ? texture2D(map, vUv * uvSc + uvOff) : vec4(1.0);
  vec3 rgb = tx.rgb; float a = tx.a;
  float va = clamp(vC.a, 0.0, 1.0);
  if (usePal > 0.5) { float lum = palG > 0.5 ? tx.g : max(tx.r, max(tx.g, tx.b)); float pv = palG > 0.5 ? clamp(va * base.a, 0.0, 1.0) : va; if (palFlip > 0.5) pv = 1.0 - pv; vec4 p = texture2D(pal, vec2(clamp(lum, 0.004, 0.996), 0.06 + 0.88 * pv)); rgb = p.rgb; if (palA > 0.5) a *= p.a; }
  rgb *= base.rgb * vC.rgb * gain; a *= base.a * va * alpha;
  if (useFall > 0.5) { float d = abs(dot(normalize(vN), normalize(vV))); float k = clamp((d - fall.y) / max(0.0001, fall.x - fall.y), 0.0, 1.0); a *= mix(fall.w, fall.z, k); }
  if (soft > 0.5) a *= smoothstep(0.0, 18.0, vWY);   // (a soft particle: no hard line where a sprite meets the ground, as the game's depth fade)
  if (a < 0.003) discard;
  gl_FragColor = vec4(rgb, a);
}`;
function realMat(sh, palName) {
  const pal = palName || null;
  const u = {
    map: { value: sh.tex ? realTex(sh.tex, (sh.clamp & 0xff) === 0) : null }, hasMap: { value: sh.tex ? 1 : 0 },
    pal: { value: pal ? realPalTex(pal) : (sh.grey ? realTex(sh.grey, true) : null) },
    usePal: { value: (pal || (sh.palC && sh.grey)) ? 1 : 0 }, palA: { value: (sh.palA || pal) ? 1 : 0 },
    base: { value: new THREE.Vector4(sh.color[0] * sh.cs, sh.color[1] * sh.cs, sh.color[2] * sh.cs, sh.color[3] == null ? 1 : sh.color[3]) },
    alpha: { value: 1 }, uvOff: { value: new THREE.Vector2(...(sh.uvoff || [0, 0])) }, uvSc: { value: new THREE.Vector2(...(sh.uvscale || [1, 1])) },
    useFall: { value: sh.fall && sh.falloff ? 1 : 0 }, fall: { value: new THREE.Vector4(...(sh.falloff || [1, 1, 1, 1])) }, soft: { value: 0 }, gain: { value: 1 }, palG: { value: window.__palG == null ? 1 : window.__palG }, palFlip: { value: window.__palFlip ? 1 : 0 },
  };
  const m = new THREE.ShaderMaterial({ uniforms: u, vertexShader: REAL_VS, fragmentShader: REAL_FS, transparent: true, depthWrite: false, side: THREE.DoubleSide,
    blending: sh.add ? THREE.AdditiveBlending : THREE.NormalBlending });
  m.userData.base0 = u.base.value.clone(); m.userData.cs = sh.cs; m.userData.col = sh.color.slice(0, 3); m.userData.a0 = sh.color[3] == null ? 1 : sh.color[3];
  return m;
}
// ---- keys
function keyAt(keys, t, dflt) {
  if (!keys || !keys.length) return dflt;
  if (t <= keys[0][0]) return keys[0][1];
  for (let i = 1; i < keys.length; i++) if (t <= keys[i][0]) {
    const a = keys[i - 1], b = keys[i], f = (t - a[0]) / Math.max(1e-6, b[0] - a[0]);
    if (typeof a[1] === 'number') return a[1] + (b[1] - a[1]) * f;
    return a[1].map((x, j) => x + (b[1][j] - x) * f);
  }
  return keys[keys.length - 1][1];
}
function trackTime(tr, t) {
  let tt = t * (tr.freq || 1);
  const s = tr.start || 0, e = tr.stop > s && tr.stop < 1e6 ? tr.stop : s + 1e6;
  if (tr.cycle === 0 && e - s < 1e5) tt = s + ((tt - s) % (e - s) + (e - s)) % (e - s);
  else tt = Math.min(e, Math.max(s, tt));
  return tt;
}
function trackVal(tr, t) {
  const tt = trackTime(tr, t);
  const v = keyAt(tr.keys, tt, tr.v);
  return v == null || (typeof v === 'number' && Math.abs(v) > 1e30) ? null : v;
}
// ---- one real part: its tree, meshes and particles, at its place, in its colour and size
function realPart(D, opt, rng) {
  const root = new THREE.Group(), inner = new THREE.Group();
  inner.rotation.x = -Math.PI / 2; root.add(inner);
  const objs = {};
  const mk = n => { const o = new THREE.Object3D(); const M = new THREE.Matrix4().makeBasis(new THREE.Vector3(...n.x), new THREE.Vector3(...n.y), new THREE.Vector3(...n.z));
    o.quaternion.setFromRotationMatrix(M); o.position.set(...n.t); o.scale.setScalar(n.s || 1); o.visible = !n.hidden; o.userData.base = { p: o.position.clone(), q: o.quaternion.clone(), s: n.s || 1 }; return o; };
  for (const n of D.tree) if (!objs[n.name]) objs[n.name] = mk(n);
  for (const n of D.tree) { const o = objs[n.name]; if (o.parent) continue; (n.parent && objs[n.parent] && objs[n.parent] !== o ? objs[n.parent] : inner).add(o); }
  const meshes = {}, emitM = {};
  for (const s of D.shapes) {
    const o = objs[s.name] || inner;
    if (s.emit) { emitM[s.name] = { v: s.v, t: s.t, o }; continue; }
    if (!s.sh.tex) continue;   // (a glow with no texture: the stage's light stands for it)
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(s.v, 3));
    if (s.uv.length) g.setAttribute('uv', new THREE.Float32BufferAttribute(s.uv, 2));
    const n = s.v.length / 3, rgba = new Float32Array(n * 4).fill(1);
    if (s.col.length === n * 4) for (let i = 0; i < n; i++) { if (s.sh.vcol) { rgba[i * 4] = s.col[i * 4]; rgba[i * 4 + 1] = s.col[i * 4 + 1]; rgba[i * 4 + 2] = s.col[i * 4 + 2]; } if (s.sh.valpha) rgba[i * 4 + 3] = s.col[i * 4 + 3]; }
    g.setAttribute('rgba', new THREE.BufferAttribute(rgba, 4));
    g.setIndex(s.t); g.computeVertexNormals();
    const m = new THREE.Mesh(g, realMat(s.sh, opt.pal && !s.sh.natural ? opt.pal : null)); m.frustumCulled = false; m.renderOrder = 2;
    { g.computeBoundingBox(); const b = g.boundingBox, h = b.max.z - b.min.z, w = Math.max(b.max.x - b.min.x, b.max.y - b.min.y);
      if (h > 0.3 * w) m.material.uniforms.soft.value = 1; }   // (standing sheets and domes fade where they meet the ground; flat rings on it stay)
    o.add(m); (meshes[s.name] = meshes[s.name] || []).push(m);
  }
  const P = D.psys.map(p => {
    const em = p.mods.find(m => /Emitter$/.test(m.type)) || null;
    const dens = (opt.mod && opt.mod.density) || 1;   // (Noah's density slider: more of each system's particles)
    const max = Math.min(1200, Math.max(8, Math.round((p.max || 64) * Math.max(1, dens))));
    const g = new THREE.BufferGeometry();
    const pos = new Float32Array(max * 12), uv = new Float32Array(max * 8), rgba = new Float32Array(max * 16), idx = [];
    for (let i = 0; i < max; i++) idx.push(i * 4, i * 4 + 1, i * 4 + 2, i * 4, i * 4 + 2, i * 4 + 3);
    g.setAttribute('position', new THREE.BufferAttribute(pos, 3)); g.setAttribute('uv', new THREE.BufferAttribute(uv, 2)); g.setAttribute('rgba', new THREE.BufferAttribute(rgba, 4));
    g.setAttribute('normal', new THREE.BufferAttribute(new Float32Array(max * 12), 3)); g.setIndex(idx);
    const mesh = new THREE.Mesh(g, realMat(p.sh, opt.pal || null)); mesh.frustumCulled = false; mesh.renderOrder = 3; mesh.material.uniforms.soft.value = 1;
    inner.add(mesh);
    return { p, em, max, mesh, g, live: [], acc: 0, mods: p.mods };
  });
  const st = { root, inner, objs, meshes, P, D, opt, rng, tPrev: 0, invInner: new THREE.Matrix4(), dur: D.dur, subs: [] };
  const gainV = (opt.mod && opt.mod.bright) || 1; root.traverse(o => { if (o.material && o.material.uniforms && o.material.uniforms.gain) o.material.uniforms.gain.value = gainV; });   // (his brightness slider)
  // (its addon nodes: Skyrim's own shared particle systems - fire, sparks, smoke - that the game hangs at its AddOnNode points)
  if (opt.all) for (const n of D.tree) {
    const mm = /^AddOnNode(\d+)/.exec(n.name), A = mm && opt.all['addn:' + parseInt(mm[1], 10)];
    if (!A || !objs[n.name]) continue;
    const sub = realPart(A, Object.assign({}, opt, { all: null }), rng);
    sub.inner.rotation.x = 0; objs[n.name].add(sub.root); sub.window = Math.min(0.7, Math.max(0.25, D.dur * 0.5));
    st.subs.push(sub);
  }
  return st;
}
const _v = new THREE.Vector3(), _w = new THREE.Vector3(), _q = new THREE.Quaternion(), _m = new THREE.Matrix4();
function realApplyTracks(st, t) {
  const { D, objs, meshes } = st;
  const rate = {}, emit = {}, grav = {}, speed = {}, life = {}, active = {};
  const mats = new Map();
  const matOf = m => { if (!mats.has(m)) mats.set(m, { emis: m.userData.cs, col: m.userData.col, a: m.userData.a0 }); return mats.get(m); };
  for (const m of Object.values(meshes)) for (const x of m) { const u = x.material.uniforms; u.alpha.value = 1; u.base.value.copy(x.material.userData.base0); }
  for (const tr of D.tracks) {
    const o = objs[tr.target];
    if (tr.k === 'xf') {
      if (!o) continue;
      const x = tr.x, tt = trackTime(tr, t), b = o.userData.base;
      if (x.pos && x.pos.length) o.position.set(...keyAt(x.pos, tt)); else o.position.copy(b.p);
      if (x.scale && x.scale.length) o.scale.setScalar(keyAt(x.scale, tt)); else o.scale.setScalar(b.s);
      if (x.quat && x.quat.length) { const q = keyAt(x.quat.map(k => [k[0], [k[1], k[2], k[3], k[4]]]), tt); o.quaternion.set(q[1], q[2], q[3], q[0]).normalize(); }
      else if ((x.zrot && x.zrot.length) || (x.xrot && x.xrot.length) || (x.yrot && x.yrot.length)) o.quaternion.setFromEuler(new THREE.Euler(keyAt(x.xrot, tt, 0), keyAt(x.yrot, tt, 0), keyAt(x.zrot, tt, 0), 'XYZ'));
      else o.quaternion.copy(b.q);
    } else if (tr.k === 'vis') { const v = trackVal(tr, t); if (o && v != null) o.visible = v > 0.5; }
    else if (tr.k === 'sf' || tr.k === 'sc') {
      const v = trackVal(tr, t); if (v == null) continue;
      const pq = st.P.find(q => q.p.name === tr.target);
      const ms = meshes[tr.target] || (pq ? [pq.mesh] : []);
      for (const m of ms) {
        const u = m.material.uniforms, e = matOf(m.material);
        if (tr.k === 'sc') { e.col = v.slice(0, 3); continue; }
        if (tr.var === 5) e.a = Math.max(0, Math.min(1, v));
        else if (tr.var === 0) e.emis = Math.max(0, v);
        else if (tr.var === 6) u.uvOff.value.x = v; else if (tr.var === 8) u.uvOff.value.y = v;
        else if (tr.var === 7) u.uvSc.value.x = v; else if (tr.var === 9) u.uvSc.value.y = v;
      }
    } else {
      const v = trackVal(tr, t); if (v == null) continue;
      const key = tr.target + '|' + (tr.mod || '');
      ({ rate, emit, grav, speed, life, active })[tr.k][key] = v;
    }
  }
  for (const [m, e] of mats) { const b = m.uniforms.base.value; b.set(e.col[0] * e.emis, e.col[1] * e.emis, e.col[2] * e.emis, e.a); }
  return { rate, emit, grav, speed, life, active };
}
function realShown(o) { for (let x = o; x; x = x.parent) if (x.visible === false) return false; return true; }
function realRel(st, o) { o.updateWorldMatrix(true, false); return _m.multiplyMatrices(st.invInner, o.matrixWorld); }
function realSpawn(st, q, ctl) {
  const em = q.em, rng = st.rng, R = () => rng() * 2 - 1;
  const key = q.p.name + '|' + em.name;
  const spd = (ctl.speed[key] != null ? ctl.speed[key] : em.speed) + R() * em.speedv;
  const lf = Math.max(0.05, (ctl.life[key] != null ? ctl.life[key] : em.life) + R() * em.lifev) * ((st.opt.mod && st.opt.mod.linger) || 1);   // (his linger slider)
  const p = new THREE.Vector3(), d = new THREE.Vector3();
  if (em.type === 'NiPSysMeshEmitter') {
    const M = (em.meshes || []).map(n => st.emitM[n]).filter(Boolean);
    const mm = M.length ? M[Math.floor(rng() * M.length)] : null;
    if (mm && mm.v.length >= 9) {
      const W = realRel(st, mm.o);
      const nt = mm.t.length / 3, ti = Math.floor(rng() * nt) * 3;
      const A = new THREE.Vector3().fromArray(mm.v, mm.t[ti] * 3), B = new THREE.Vector3().fromArray(mm.v, mm.t[ti + 1] * 3), C = new THREE.Vector3().fromArray(mm.v, mm.t[ti + 2] * 3);
      if (em.etype === 0) p.copy([A, B, C][Math.floor(rng() * 3)]);
      else { let a = rng(), b = rng(); if (a + b > 1) { a = 1 - a; b = 1 - b; } p.copy(A).addScaledVector(B.clone().sub(A), a).addScaledVector(C.clone().sub(A), b); }
      const n = B.clone().sub(A).cross(C.clone().sub(A)).normalize();
      if (em.vtype === 1) d.set(R(), R(), R()).normalize(); else if (em.vtype === 2) d.set(...em.eaxis).normalize(); else d.copy(n);
      p.applyMatrix4(W); d.transformDirection(W);
    } else { p.set(0, 0, 0); d.set(R(), R(), R()).normalize(); }
    if (em.declv) { d.x += R() * Math.sin(em.declv) * 0.8; d.y += R() * Math.sin(em.declv) * 0.8; d.z += R() * Math.sin(em.declv) * 0.8; d.normalize(); }
  } else {
    const n = em.node || { t: [0, 0, 0], x: [1, 0, 0], y: [0, 1, 0], z: [0, 0, 1], s: 1 };
    const X = new THREE.Vector3(...n.x), Y = new THREE.Vector3(...n.y), Z = new THREE.Vector3(...n.z);
    let lx = 0, ly = 0, lz = 0;
    if (em.box) { lx = R() * em.box[0] / 2; ly = R() * em.box[1] / 2; lz = R() * em.box[2] / 2; }
    else if (em.type === 'NiPSysSphereEmitter') { let r; do { lx = R(); ly = R(); lz = R(); r = lx * lx + ly * ly + lz * lz; } while (r > 1); lx *= em.radius; ly *= em.radius; lz *= em.radius; }
    else if (em.type === 'NiPSysCylinderEmitter') { const a = rng() * 6.283, r = Math.sqrt(rng()) * em.radius; lx = Math.cos(a) * r; ly = Math.sin(a) * r; lz = R() * em.height / 2; }
    p.set(...n.t).addScaledVector(X, lx * n.s).addScaledVector(Y, ly * n.s).addScaledVector(Z, lz * n.s);
    const dec = em.decl + R() * em.declv, pl = em.plan + R() * em.planv;
    d.set(0, 0, 0).addScaledVector(X, Math.sin(dec) * Math.cos(pl)).addScaledVector(Y, Math.sin(dec) * Math.sin(pl)).addScaledVector(Z, Math.cos(dec)).normalize();
  }
  const rot = q.mods.find(m => m.type === 'NiPSysRotationModifier');
  const sub = q.p.sub.length ? Math.floor(rng() * q.p.sub.length) : -1;
  return { p, v: d.multiplyScalar(spd), age: 0, life: lf, size: Math.max(0.1, em.size + R() * em.sizev),
    a: rot ? rot.rangle + R() * rot.ranglev : rng() * 6.283, w: rot ? (rot.rspeed + R() * rot.rspeedv) * (rot.rsign && rng() < 0.5 ? -1 : 1) : 0,
    c: em.color || [1, 1, 1, 1], sub };
}
function realColour(q, x) {
  const u = x.age / x.life;
  let c = x.c.slice();
  for (const m of q.mods) {
    if (m.type === 'BSPSysSimpleColorModifier') {
      const L = (a, b, f) => a.map((v, i) => v + (b[i] - v) * Math.min(1, Math.max(0, f)));
      let k;
      if (u <= m.c1end) k = m.c1; else if (u < m.c2start) k = L(m.c1, m.c2, (u - m.c1end) / Math.max(1e-4, m.c2start - m.c1end));
      else if (u <= m.c2end) k = m.c2; else if (u < m.c3start) k = L(m.c2, m.c3, (u - m.c2end) / Math.max(1e-4, m.c3start - m.c2end)); else k = m.c3;
      let fade = 1;
      if (m.fin > 0 && u < m.fin) fade = u / m.fin;
      if (m.fout > 0 && u > 1 - m.fout) fade = Math.min(fade, (1 - u) / m.fout);
      c = [c[0] * k[0], c[1] * k[1], c[2] * k[2], fade * (k[3] > 0 || m.c1[3] > 0 || m.c2[3] > 0 ? 1 : 1)];
      c[3] = fade;
    } else if (m.type === 'NiPSysColorModifier' && m.keys && m.keys.length) {
      const k = keyAt(m.keys.map(kk => [kk[0], kk[1]]), u * (m.keys[m.keys.length - 1][0] || 1));
      c = [c[0] * k[0], c[1] * k[1], c[2] * k[2], c[3] * k[3]];
    }
  }
  return c;
}
function realSize(q, x) {
  const u = Math.min(1, x.age / x.life);
  let f = 1;
  for (const m of q.mods) {
    if (m.type === 'BSPSysScaleModifier' && m.scales && m.scales.length) { const n = m.scales.length - 1, i = Math.min(n, u * n), i0 = Math.floor(i); f *= m.scales[i0] + (m.scales[Math.min(n, i0 + 1)] - m.scales[i0]) * (i - i0); }
    else if (m.type === 'NiPSysGrowFadeModifier') { if (m.grow > 0 && x.age < m.grow) f *= x.age / m.grow; if (m.fade > 0 && x.life - x.age < m.fade) f *= Math.max(0, (x.life - x.age) / m.fade); }
  }
  return f;
}
function realStep(st, t, dt, cam) {
  st.root.updateMatrixWorld(true);
  st.invInner.copy(st.inner.matrixWorld).invert();
  const ctl = realApplyTracks(st, t);
  st.root.updateMatrixWorld(true);
  st.emitM = st.emitM || {};
  for (const s of st.D.shapes) if (s.emit) st.emitM[s.name] = { v: s.v, t: s.t, o: st.objs[s.name] || st.inner };
  // the camera's right and up, in the part's own space (its billboards face you)
  const R = new THREE.Vector3(), U = new THREE.Vector3();
  cam.matrixWorld.extractBasis(R, U, _w);
  const inv3 = new THREE.Matrix3().setFromMatrix4(st.invInner);
  R.applyMatrix3(inv3).normalize(); U.applyMatrix3(inv3).normalize();
  for (const sub of st.subs) { sub.noEmit = t > sub.window || !realShown(sub.root); realStep(sub, t, dt, cam); }
  for (const q of st.P) {
    const node = st.objs[q.p.name];
    const shown = !node || realShown(node);
    if (q.em && dt > 0 && shown && !st.noEmit) {
      const key = q.p.name + '|' + q.em.name;
      const on = ctl.emit[key] == null ? true : ctl.emit[key] > 0.5;
      const r = ctl.rate[key] == null ? 0 : ctl.rate[key];
      if (on && r > 0 && t >= 0) { q.acc += r * dt * ((st.opt.mod && st.opt.mod.density) || 1); while (q.acc >= 1 && q.live.length < q.max) { q.live.push(realSpawn(st, q, ctl)); q.acc -= 1; } if (q.live.length >= q.max) q.acc = 0; }
    }
    for (const x of q.live) {
      x.age += dt;
      for (const m of q.mods) {
        const k = q.p.name + '|' + m.name;
        if (ctl.active[k] != null && ctl.active[k] < 0.5) continue;
        if (m.type === 'NiPSysGravityModifier') {
          const s = ctl.grav[k] != null ? ctl.grav[k] : m.strength;
          const n = m.node || { t: [0, 0, 0], x: [1, 0, 0], y: [0, 1, 0], z: [0, 0, 1] };
          if (m.force === 1) { _v.set(...n.t).sub(x.p); const L = _v.length(); if (L > 1e-3) x.v.addScaledVector(_v, s * dt / L); }
          else { _v.set(0, 0, 0).addScaledVector(new THREE.Vector3(...n.x), m.axis[0]).addScaledVector(new THREE.Vector3(...n.y), m.axis[1]).addScaledVector(new THREE.Vector3(...n.z), m.axis[2]); x.v.addScaledVector(_v.normalize(), s * dt); }
          if (m.turb) { x.v.x += (st.rng() * 2 - 1) * m.turb * dt * 60; x.v.y += (st.rng() * 2 - 1) * m.turb * dt * 60; x.v.z += (st.rng() * 2 - 1) * m.turb * dt * 60; }
        } else if (m.type === 'NiPSysDragModifier') {
          const k2 = Math.pow(Math.max(0, 1 - Math.min(0.95, m.drag)), dt * 30);
          if (/X-Axis/.test(m.name)) x.v.x *= k2; else if (/Y-Axis/.test(m.name)) x.v.y *= k2; else if (/Z-Axis/.test(m.name)) x.v.z *= k2; else x.v.multiplyScalar(k2);
        }
      }
      x.p.addScaledVector(x.v, dt); x.a += x.w * dt;
    }
    q.live = q.live.filter(x => x.age < x.life);
    // the quads
    const pos = q.g.attributes.position.array, uv = q.g.attributes.uv.array, col = q.g.attributes.rgba.array;
    const subm = q.mods.find(m => m.type === 'BSPSysSubTexModifier');
    let n = 0;
    if (shown) for (const x of q.live) {
      if (n >= q.max) break;
      const s = x.size * realSize(q, x), c = realColour(q, x);
      const ca = Math.cos(x.a), sa = Math.sin(x.a);
      const ax = R.x * ca + U.x * sa, ay = R.y * ca + U.y * sa, az = R.z * ca + U.z * sa;
      const bx = -R.x * sa + U.x * ca, by = -R.y * sa + U.y * ca, bz = -R.z * sa + U.z * ca;
      const corners = [[-1, -1], [1, -1], [1, 1], [-1, 1]];
      let sub = x.sub;
      if (subm && q.p.sub.length) { const fr = Math.floor(subm.f0 + (subm.fc > 0 ? (x.age / x.life) * Math.max(1, subm.fc) : 0)); sub = ((fr % q.p.sub.length) + q.p.sub.length) % q.p.sub.length; }
      const so = sub >= 0 ? q.p.sub[sub] : [0, 1, 0, 1];
      for (let k = 0; k < 4; k++) {
        const [i, j] = corners[k], o = (n * 4 + k);
        pos[o * 3] = x.p.x + (ax * i + bx * j) * s; pos[o * 3 + 1] = x.p.y + (ay * i + by * j) * s; pos[o * 3 + 2] = x.p.z + (az * i + bz * j) * s;
        uv[o * 2] = so[0] + (i > 0 ? so[1] : 0); uv[o * 2 + 1] = so[2] + (j > 0 ? 0 : so[3]);
        col[o * 4] = c[0]; col[o * 4 + 1] = c[1]; col[o * 4 + 2] = c[2]; col[o * 4 + 3] = c[3];
      }
      n++;
    }
    for (let o = n * 4; o < q.max * 4; o++) { pos[o * 3] = pos[o * 3 + 1] = pos[o * 3 + 2] = 0; col[o * 4 + 3] = 0; }
    q.g.attributes.position.needsUpdate = q.g.attributes.uv.needsUpdate = q.g.attributes.rgba.needsUpdate = true;
    q.g.setDrawRange(0, n * 6);
  }
}
// ---- an ability's effect: its style's roles filled from its theme's real parts (tools/realfx.py compose, the same rules)
function realCompose(card, style, colKey, sizePct) {
  const R = REALFX, th = R.parts[card.theme];
  if (!th) return [];
  const k = (sizePct || 100) / 100, native = (th.native || []).includes(colKey), ramp = R.rampOf[colKey] || colKey;
  const roles = (R.styles[style] || R.styles.burst).slice();
  if (sizePct >= R.bigAt && !roles.some(r => ['big', 'nova', 'sky'].includes(r[0]))) roles.push(['big', 0.1, 1.0, roles[0][3]]);
  const out = [];
  for (const [role, delay, rk, ground] of roles) {
    const p = role === 'ring' ? R.ring : th[role];
    if (!p) continue;
    if (role !== 'ring' && out.some(q => q.model === p.model)) continue;
    const pal = (!p.nat && !native) || role === 'ring' ? ramp : null;
    out.push({ role, model: p.model, label: p.label, s: p.s * k * rk, delay, ground, pal });
  }
  return out;
}
