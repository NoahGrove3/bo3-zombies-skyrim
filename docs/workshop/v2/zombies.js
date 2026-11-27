// ================================================================ the night's own dead in the ability pictures (Noah, 2026-11-25: "make
// the enemy models in the previews actual zombie models with reactions to the ability effects" - "Your game's own", reacting "as
// the game does"): the six Thriceborn, Mihail's CC Zombies' bodies read from his game (tools/zombiepreview.py), on their own
// skeleton; each stands hunched, arms out, swaying, and reacts to what strikes it as the game makes it - a flinch, a stagger, flung
// far, frozen, burning, shackled, asleep, stunned, falling dead. Every pose is worked out from the time alone, so the loop and the
// page's time slider both just ask "where is it at t".
const ZOM = { skel: null, bodies: {}, maps: {} };
const ZOM_KEYS = ['mangled', 'foul', 'putrid', 'malignant', 'relentless', 'dread_putrid'];
async function zomData(key, pre) {   // (pre: 'zom' the dead, 'plr' you)
  pre = pre || 'zom';
  const sk = pre + '_skel';
  if (!ZOM[sk]) ZOM[sk] = fetch('live/' + sk + '.json').then(r => r.json());
  key = pre + '_' + key;
  if (!ZOM.bodies[key]) ZOM.bodies[key] = (async () => {
    const d = await (await fetch('live/' + key + '.json')).json();
    const bin = Uint8Array.from(atob(d.gz), ch => ch.charCodeAt(0));
    const buf = await new Response(new Blob([bin]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer();
    let off = 0; const take = (C, n) => { const b = C.BYTES_PER_ELEMENT * n; const a = new C(buf.slice(off, off + b)); off += b; return a; };
    for (const s of d.shapes) {
      const q = take(Int16Array, s.n * 3), p = new Float32Array(s.n * 3);
      for (let i = 0; i < q.length; i++) { const ax = i % 3; p[i] = (q[i] + 32768) / 65535 * s.span[ax] + s.lo[ax]; }
      const uq = take(Int16Array, s.n * 2), uv = new Float32Array(s.n * 2); for (let i = 0; i < uq.length; i++) uv[i] = uq[i] / 8192;
      s.pos = p; s.uv = uv; s.idx = take(Uint16Array, s.nt * 3);
      const bi = take(Uint8Array, s.n * 4), bw = take(Uint8Array, s.n * 4);
      s.si = new Uint16Array(bi); s.sw = new Float32Array(s.n * 4); for (let i = 0; i < bw.length; i++) s.sw[i] = bw[i] / 255;
    }
    return d;
  })();
  return Promise.all([ZOM[sk], ZOM.bodies[key]]);
}
function zomMap(name, color, ext) {
  if (!name) return null;
  if (!ZOM.maps[name]) { const t = new THREE.TextureLoader().load('live/tex/' + name + '.' + (ext || 'jpg')); if (color) t.encoding = THREE.sRGBEncoding; t.flipY = false; t.wrapS = t.wrapT = THREE.RepeatWrapping; ZOM.maps[name] = t; }
  return ZOM.maps[name];
}
const xfMat = x => new THREE.Matrix4().set(x.r[0] * x.s, x.r[1] * x.s, x.r[2] * x.s, x.t[0], x.r[3] * x.s, x.r[4] * x.s, x.r[5] * x.s, x.t[1], x.r[6] * x.s, x.r[7] * x.s, x.r[8] * x.s, x.t[2], 0, 0, 0, 1);

// one of the dead: holder (where it stands, which way it faces) > tilt (falling, tumbling) > inner (Skyrim's z-up) > its bones and body
function makeZombie(sk, body, seed) {
  const holder = new THREE.Group(), tilt = new THREE.Group(), inner = new THREE.Group();
  inner.rotation.x = -Math.PI / 2; holder.add(tilt); tilt.add(inner);
  const bones = {};
  for (const n of sk.nodes) { const b = new THREE.Bone(); b.name = n.name; b.matrixAutoUpdate = true; new THREE.Matrix4().copy(xfMat(n.x)).decompose(b.position, b.quaternion, b.scale); bones[n.name] = b; }
  for (const n of sk.nodes) (n.parent && bones[n.parent] ? bones[n.parent] : inner).add(bones[n.name]);
  const mats = [];
  for (const s of body.shapes) {
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(s.pos, 3)); g.setAttribute('uv', new THREE.BufferAttribute(s.uv, 2));
    g.setAttribute('skinIndex', new THREE.Uint16BufferAttribute(s.si, 4)); g.setAttribute('skinWeight', new THREE.BufferAttribute(s.sw, 4));
    g.setIndex(new THREE.BufferAttribute(s.idx, 1)); g.computeVertexNormals();
    const m = new THREE.MeshStandardMaterial({ map: zomMap(s.map, true, s.ext), normalMap: s.nrm ? zomMap(s.nrm, false) : null, normalScale: new THREE.Vector2(1, -1), roughness: 0.82, metalness: 0.0, skinning: true, emissive: new THREE.Color(0x000000),
      alphaTest: s.cut || 0, emissiveMap: zomMap(s.map, true, s.ext), side: s.cut ? THREE.DoubleSide : THREE.FrontSide, color: s.tint ? new THREE.Color(s.tint[0], s.tint[1], s.tint[2]) : 0xffffff });
    const mesh = new THREE.SkinnedMesh(g, m); mesh.frustumCulled = false; inner.add(mesh); mats.push(m);
    mesh.userData.s = s;
  }
  holder.updateMatrixWorld(true);
  for (const mesh of inner.children.filter(o => o.isSkinnedMesh)) {
    const s = mesh.userData.s, list = s.bones.map(n => bones[n] || inner);
    const inv = s.s2b.map(x => xfMat(x).multiply(new THREE.Matrix4().copy(mesh.matrixWorld).invert()));
    mesh.bind(new THREE.Skeleton(list, inv), mesh.matrixWorld.clone());
  }
  // each bone's rest, and its rest turn in the body's own space (x right, y forward, z up) - the frame its poses are given in
  const innerQ = new THREE.Quaternion(); inner.getWorldQuaternion(innerQ); const innerQi = innerQ.clone().invert();
  const rest = {};
  for (const [n, b] of Object.entries(bones)) { const w = new THREE.Quaternion(); b.getWorldQuaternion(w); w.premultiply(innerQi); const p = new THREE.Vector3(); b.getWorldPosition(p); inner.worldToLocal(p); rest[n] = { q: b.quaternion.clone(), w, wi: w.clone().invert(), p }; }
  const z = { holder, tilt, inner, bones, rest, mats, seed, D: {} };
  z.find = part => Object.keys(bones).find(n => n.indexOf(part) >= 0);
  z.B = {}; for (const [k, part] of Object.entries({ com: '[COM ]', pelvis: '[Pelv]', spine: '[Spn0]', spine1: '[Spn1]', spine2: '[Spn2]', neck: '[Neck]', head: '[Head]', lua: '[LUar]', rua: '[RUar]', lfa: '[LLar]', rfa: '[RLar]', lhand: '[LHnd]', rhand: '[RHnd]', lthigh: '[LThg]', rthigh: '[RThg]', lcalf: '[LClf]', rcalf: '[RClf]', lfoot: '[LLft', rfoot: '[Rft' })) z.B[k] = z.find(part);
  // its arms' and forearms' rest directions (for "reach forward")
  const dir = (a, b) => rest[z.B[b]].p.clone().sub(rest[z.B[a]].p).normalize();
  z.dir = { lua: dir('lua', 'lfa'), rua: dir('rua', 'rfa'), lfa: dir('lfa', 'lhand'), rfa: dir('rfa', 'rhand') };
  z.side = { lua: Math.sign(z.dir.lua.x) || -1, rua: Math.sign(z.dir.rua.x) || 1 };
  return z;
}
const ZX = new THREE.Vector3(1, 0, 0), ZY = new THREE.Vector3(0, 1, 0), ZZ = new THREE.Vector3(0, 0, 1);
function zRot(z, k, axis, a) { const n = z.B[k]; if (!n || !a) return; const q = new THREE.Quaternion().setFromAxisAngle(axis, a); z.D[n] = z.D[n] ? q.multiply(z.D[n]) : q; }
function zAim(z, k, to, amt) { const n = z.B[k]; if (!n) return; const from = z.dir[k]; const q = new THREE.Quaternion().setFromUnitVectors(from, to.clone().normalize()); if (amt != null && amt < 1) q.slerp(new THREE.Quaternion(), 1 - amt); z.D[n] = z.D[n] ? q.multiply(z.D[n]) : q; }
function zApply(z) {
  for (const [n, b] of Object.entries(z.bones)) {
    const r = z.rest[n], d = z.D[n];
    if (!d) { b.quaternion.copy(r.q); continue; }
    b.quaternion.copy(r.q).multiply(r.wi.clone().multiply(d).multiply(r.w));
  }
  z.D = {};
}

// ---- the poses: its stance (hunched, arms out, swaying), and what each reaction adds on top
const sm = x => x <= 0 ? 0 : x >= 1 ? 1 : x * x * (3 - 2 * x);
function zStance(z, t, k, arm) {   // k: how alive it still is (0 limp .. 1 reaching); arm: a reaction's own arms {u: upper arm dir, f: forearm dir, k}
  const ph = z.seed * 7.3, s = Math.sin(t * 1.6 + ph), c = Math.cos(t * 0.8 + ph);
  zRot(z, 'spine', ZX, (-0.22 + 0.04 * s) * k); zRot(z, 'spine1', ZX, -0.1 * k); zRot(z, 'spine2', ZX, -0.08 * k); zRot(z, 'spine', ZY, 0.06 * c * k);
  zRot(z, 'neck', ZX, 0.18 * k); zRot(z, 'head', ZX, (-0.05 + 0.06 * c) * k); zRot(z, 'head', ZY, 0.18 * Math.sin(t * 0.7 + ph) * k);
  for (const [a, f, sd] of [['lua', 'lfa', z.side.lua], ['rua', 'rfa', z.side.rua]]) {
    const bob = 0.08 * Math.sin(t * 1.6 + ph + (sd > 0 ? 1.4 : 0));
    // (limp: hanging at its sides; alive: reaching for you)
    let up = new THREE.Vector3(sd * (0.12 + 0.1 * k), 0.08 + 0.92 * k, -1 + 0.68 * k + bob * k), fo = new THREE.Vector3(sd * (0.2 + 0.35 * k), 0.15 + 0.7 * k, -0.9 + 1.02 * k);
    if (arm && arm.k > 0) { up.normalize().lerp(new THREE.Vector3(sd * arm.u[0], arm.u[1], arm.u[2]).normalize(), arm.k); fo.normalize().lerp(new THREE.Vector3(sd * arm.f[0], arm.f[1], arm.f[2]).normalize(), arm.k); }
    zAim(z, a, up); zAim(z, f, fo, 0.85);
  }
  zRot(z, 'lthigh', ZX, 0.12 * k + 0.08 * Math.max(0, s) * k); zRot(z, 'rthigh', ZX, 0.12 * k + 0.08 * Math.max(0, -s) * k);
  zRot(z, 'lcalf', ZX, -0.25 * k); zRot(z, 'rcalf', ZX, -0.25 * k);
}
const ZTINT = { burn: [1, 0.42, 0.08], freeze: [0.55, 0.8, 1], shock: [0.6, 0.75, 1], poison: [0.35, 0.9, 0.2], bleed: [0.9, 0.08, 0.05], shackle: [0.3, 1, 0.45],
  slow: [0.45, 0.6, 1], raise: [0.5, 1, 0.75], turn: [1, 0.8, 0.3], drain: [0.4, 0.8, 1], blind: [0.85, 0.85, 1] };
// where the dead stand (the page's SPOT): t0 the one struck, then the others
function zPose(z, rx, t, info) {
  // rx: this one's reactions [{k, t, o}] sorted by time; info: {from (where it came from), home, face}
  let arm = null, alive = 1, idleT = t, yaw = 0, fall = 0, fallDir = 1, lift = 0, tint = null, tintA = 0, scale = 1, shake = 0;
  const pos = info.home.clone();
  for (const r of rx) {
    const u = t - r.t; if (u < 0) continue;
    const away = info.home.clone().sub(r.from || info.from).setY(0); if (away.lengthSq() < 1) away.set(1, 0, 0); away.normalize();
    switch (r.k) {
      case 'flinch': { const e = Math.exp(-u * 7) * Math.min(1, u * 30); zRot(z, 'spine1', ZX, 0.35 * e); zRot(z, 'head', ZX, 0.5 * e); pos.addScaledVector(away, 8 * sm(u / 0.15)); break; }
      case 'stagger': { const e = Math.exp(-u * 4) * Math.min(1, u * 20); zRot(z, 'spine1', ZX, 0.45 * e); zRot(z, 'head', ZX, 0.4 * e); pos.addScaledVector(away, 55 * sm(u / 0.45)); shake = Math.max(shake, e); break; }
      case 'knock': case 'die': case 'shatter': case 'fling': {
        const fd = r.k === 'die' ? 0.75 : r.k === 'fling' ? 1.1 : 0.45;
        alive = Math.min(alive, 1 - sm(u / 0.35));
        if (r.k === 'fling') { const k = Math.min(1, u / fd), d = (r.o && r.o.dist) || 400; pos.addScaledVector(away, d * (1 - (1 - k) * (1 - k))); lift = Math.max(lift, 4 * 120 * k * (1 - k)); fall = sm(k) * Math.PI / 2 + (1 - k) * k * 6; fallDir = 1; }
        else { pos.addScaledVector(away, (r.k === 'knock' ? 60 : 15) * sm(u / fd)); fall = sm(u / fd) * Math.PI / 2; fallDir = r.k === 'die' ? -1 : 1; }
        if (r.k === 'shatter') { tint = ZTINT.freeze; tintA = Math.max(0, 1 - u * 2); scale = u < 0.12 ? 1 : 0.001; }
        break; }
      case 'freeze': idleT = Math.min(idleT, r.t); tint = ZTINT.freeze; tintA = Math.min(1, u * 5) * 0.85; break;
      case 'shackle': idleT = Math.min(idleT, r.t); tint = ZTINT.shackle; tintA = 0.35 + 0.25 * Math.sin(u * 9); zRot(z, 'spine', ZX, 0.1); break;
      case 'burn': tint = r.o && r.o.col ? r.o.col : ZTINT.burn; tintA = 0.45 + 0.35 * Math.abs(Math.sin(u * 13 + z.seed)); idleT += u * 1.6;
        zRot(z, 'spine1', ZY, 0.25 * Math.sin(u * 11)); zRot(z, 'head', ZY, 0.4 * Math.sin(u * 9)); break;
      case 'shock': { const j = u < 1.2 ? 1 : Math.max(0, 1 - (u - 1.2) * 3); tint = ZTINT.shock; tintA = j * (Math.sin(u * 60) > 0 ? 0.6 : 0.12); idleT = Math.min(idleT, r.t);
        zRot(z, 'spine1', ZX, 0.25 * j * Math.sin(u * 47)); zRot(z, 'head', ZY, 0.3 * j * Math.sin(u * 53)); zRot(z, 'spine', ZY, 0.15 * j * Math.sin(u * 41)); break; }
      case 'poison': case 'bleed': case 'drain': tint = ZTINT[r.k]; tintA = 0.3 + 0.25 * Math.sin(u * 5); zRot(z, 'spine1', ZX, -0.15 * sm(u)); break;
      case 'slow': idleT = r.t + u * 0.3; tint = ZTINT.slow; tintA = 0.25; break;
      case 'sleep': { const k = sm(u / 0.8); alive = Math.min(alive, 1 - k); zRot(z, 'spine', ZX, -0.5 * k); zRot(z, 'head', ZX, -0.6 * k); zRot(z, 'lthigh', ZX, 1.2 * k); zRot(z, 'rthigh', ZX, 1.1 * k); zRot(z, 'lcalf', ZX, -2.1 * k); zRot(z, 'rcalf', ZX, -2.1 * k); lift = Math.min(lift, -38 * k); break; }
      case 'blind': { const k = sm(u / 0.25); arm = { u: [0.35, 0.6, 0.7], f: [-0.7, 0.3, 0.6], k };
        zRot(z, 'head', ZY, 0.35 * Math.sin(u * 6) * k); pos.addScaledVector(away, 25 * k); tint = ZTINT.blind; tintA = 0.3 * Math.max(0, 1 - u); break; }
      case 'stun': { const w = Math.max(0, 1 - u / 2.2); zRot(z, 'spine', ZY, 0.3 * w * Math.sin(u * 5)); zRot(z, 'spine', ZX, 0.2 * w * Math.cos(u * 5)); zRot(z, 'head', ZX, -0.5 * w); idleT = Math.min(idleT, r.t); alive = Math.min(alive, 1 - 0.7 * w); break; }
      case 'cower': { const k = sm(u / 0.4); pos.addScaledVector(away, 35 * k); zRot(z, 'spine', ZX, 0.2 * k); zRot(z, 'spine1', ZX, -0.25 * k);
        arm = { u: [0.45, 0.55, 0.55], f: [-0.4, 0.4, 0.8], k }; break; }
      case 'lost': { const k = sm(u / 1.2); yaw += (z.seed > 0.5 ? 1 : -1) * 2.2 * k; pos.addScaledVector(new THREE.Vector3(Math.sin(z.seed * 9), 0, Math.cos(z.seed * 9)), 40 * k); idleT = r.t + u * 0.5; break; }
      case 'pull': { const to = r.o.to.clone().setY(0), k = sm(u / 0.7), dirv = to.clone().sub(info.home); const d = Math.max(0, dirv.length() - 75 - 25 * z.seed); dirv.normalize();
        pos.addScaledVector(dirv, d * k); alive = Math.min(alive, 1 - 0.6 * k); zRot(z, 'spine', ZX, 0.3 * k * Math.exp(-u)); idleT = Math.min(idleT, r.t); tint = ZTINT.shackle; tintA = 0.3 * k; break; }
      case 'lift': { const k = Math.min(1, u / 1.6); lift = Math.max(lift, 140 * Math.sin(Math.PI * k)); yaw += u * 7; alive = Math.min(alive, 0.4); if (r.o && r.o.col) { tint = r.o.col; tintA = 0.5; } break; }
      case 'raise': { const down = sm(u / 0.5), up = sm((u - 1.0) / 0.8); alive = Math.min(alive, 1 - down + up); fall = Math.max(fall, (down - up) * Math.PI / 2); fallDir = -1; if (u > 0.9) { tint = ZTINT.raise; tintA = 0.5 * up; yaw += Math.PI * up; } break; }
      case 'turn': { const k = sm(u / 0.8); yaw += Math.PI * k; tint = ZTINT.turn; tintA = 0.45 * k; break; }
      case 'shrink': scale = Math.min(scale, 1 - 0.65 * sm(u / 0.35)); break;
    }
  }
  zStance(z, idleT, alive, arm);
  if (shake) zRot(z, 'head', ZY, 0.3 * shake * Math.sin(t * 40));
  zApply(z);
  z.holder.position.copy(pos); z.holder.position.y = Math.max(-40, lift);
  z.holder.rotation.y = info.face + yaw; z.holder.scale.setScalar(scale);
  z.tilt.rotation.x = -fallDir * fall;
  const glow = tint && [ZTINT.freeze, ZTINT.shock, ZTINT.shackle, ZTINT.slow, ZTINT.raise, ZTINT.blind].includes(tint);   // (a cold or spectral light over it; fire, poison, blood colour its skin)
  for (const m of z.mats) { if (m.emissiveMap !== (glow ? null : m.map)) { m.emissiveMap = glow ? null : m.map; m.needsUpdate = true; }
    if (tint && tintA > 0) { m.emissive.setRGB(tint[0], tint[1], tint[2]); m.emissiveIntensity = tintA * (glow ? 0.3 : 1.6); } else m.emissive.setRGB(0, 0, 0); }
}

// ---- how each ability moves the dead (from what the game does to them, src/*.cpp): [who, when s, reaction, options]
// who: t0 (the one struck) .. t4, 'near:R' (within R of where it strikes), 'cone:R' (in front of you), 'line' (along your aim)
const ZBURN = c => ({ col: c });
const REACT = {
  vahlok_hit: [['t0', 0.05, 'shackle']], vahlok_heavy: [['t0', 0.05, 'shackle'], ['near:480', 0.15, 'pull', { to: 't0' }]],
  nahkriin_hit: [['t0', 0.05, 'blind']], nahkriin_heavy: [['near:420', 0.3, 'lost']],
  volsung_light: [['cone:560', 0.05, 'stagger']], volsung_heavy: [['cone:840', 0.05, 'knock']],
  zahkriisos_hit: [['t0', 0.05, 'freeze']], zahkriisos_heavy: [['near:520', -1, 'freeze'], ['near:520', 0.12, 'shatter']],
  otar_hit: [['t0', 0.05, 'shock'], ['t1', 0.15, 'shock'], ['t2', 0.25, 'shock']], otar_heavy: [['near:520', 0.1, 'shock']],
  rahgot_hit: [['t0', 0.05, 'poison'], ['t1', 0.6, 'poison']], rahgot_heavy: [['near:520', -1, 'poison'], ['near:520', 0.1, 'die']],
  krosis_hit: [['t0', 0.05, 'flinch'], ['t0', 0.05, 'bleed']], krosis_heavy: [['near:460', 0.1, 'cower']],
  hevnoraak_hit: [['t0', 0.05, 'stagger']], hevnoraak_heavy: [['near:460', 0.05, 'stagger']],
  vokun_hit: [['t0', 0.05, 'sleep']], vokun_heavy: [['near:460', 0.3, 'slow']],
  morokei_hit: [['t0', 0.05, 'flinch']], morokei_heavy: [['near:460', 0.05, 'stagger']],
  stormtalons_third: [['t0', 0.05, 'shock'], ['t1', 0.15, 'shock'], ['t2', 0.25, 'shock']], stormtalons_heavy: [['t0', 0.05, 'stun']],
  gravebell_fifth: [['near:360', 0.05, 'stun']], talons_hit: [['t0', 0.05, 'flinch'], ['t0', 0.05, 'bleed']],
  riverstone_hit: [['t0', 0.05, 'knock']], riverstone_heavy: [['line', 0.1, 'knock']],
  wyrmspine_pike_hit: [['line', 0.05, 'flinch']], wyrmspine_pike_heavy: [['near:560', 0.05, 'stagger']],
  lanternwick_light: [['t0', 0.05, 'freeze']], lanternwick_heavy: [['near:700', 0.3, 'slow']],
  cinderclaws_hit: [['t0', 0.05, 'burn']], cinderclaws_heavy: [['cone:520', 0.1, 'burn']],
  eclipse_hit: [['t0', 0.05, 'freeze']], eclipse_heavy: [['near:700', 0.05, 'stagger'], ['near:700', 0.6, 'burn', ZBURN([1, 0.75, 0.3])]],
  wanderstar_cast: [['t0', 0.35, 'flinch'], ['t1', 0.8, 'flinch'], ['t2', 1.25, 'flinch']], wanderstar_fifth: [['t0', 0.05, 'fling']],
  frostmourn_hit: [['t0', 0.05, 'slow']], frostmourn_heavy: [['near:460', 0.05, 'freeze']], silverscythe_fourth: [['t0', 0.05, 'slow']],
  bonereaper_kill: [['t0', 0.05, 'raise']], wyrmtooth_hit: [['t0', 0.05, 'flinch'], ['t0', 0.05, 'bleed']], wyrmtooth_heavy: [['near:560', 0.05, 'stagger']],
  bloodmoon_hit: [['t0', 0.05, 'drain']], bloodmoon_heavy: [['near:460', 0.1, 'drain']],
  javelin_throw: [['t0', 0.45, 'die']], javelin_land: [['t0', 0.05, 'die'], ['near:260', 0.05, 'stagger']],
  javelin_bounce: [['t0', 0.0, 'die'], ['t1', 0.9, 'die'], ['t2', 1.7, 'die']],
  soulcaller_beam: [['t0', 0.1, 'drain'], ['t0', 1.2, 'die']], soulcaller_wolves: [['near:520', 0.3, 'cower']],
  mutagen_flasks: [['t0', 0.45, 'burn'], ['t1', 1.0, 'freeze'], ['t2', 1.55, 'shock']], mutagen_elixir: [['near:460', 0.3, 'turn']],
  slayer_light: [['t0', 0.05, 'burn', ZBURN([1, 0.75, 0.25])]], slayer_heavy: [['t0', 0.9, 'die'], ['t1', 1.5, 'die'], ['t2', 2.1, 'die']],
  bane_light: [['t0', 0.05, 'die']], bane_heavy: [['near:800', 0.05, 'fling']],
  scythe_light: [['t0', 0.05, 'burn', ZBURN([0.7, 0.3, 1])]], scythe_heavy: [['near:420', 0.2, 'lift', ZBURN([0.7, 0.3, 1])]],
  wand_light: [['t0', 0.45, 'burn'], ['t1', 1.0, 'shock'], ['t2', 1.55, 'freeze']], wand_hit: [['t0', 0.05, 'flinch']],
  wand_heavy: [['t0', 0.45, 'burn'], ['t0', 0.95, 'die'], ['t1', 1.05, 'shock'], ['t2', 1.1, 'shock'], ['t1', 2.05, 'die'], ['t2', 2.1, 'die']],
  staff_light: [['t0', 0.05, 'burn', ZBURN([1, 0.95, 0.8])]], staff_heavy: [['near:420', 0.93, 'knock'], ['near:420', 0.93, 'burn', ZBURN([1, 0.95, 0.8])]],
  rapier_light: [['t0', 0.05, 'burn']], rapier_heavy: [['near:420', 0.2, 'lift']], rapier_line: [['line', 0.05, 'freeze']],
  ffire_land: [['t0', 0.05, 'burn']], ffire_power: [['near:520', 0.05, 'stagger'], ['near:520', 0.1, 'burn']],
  ffrost_land: [['near:300', 0.1, 'slow']], ffrost_power: [['near:520', 0.05, 'freeze']],
  fshock_land: [['near:300', 0.1, 'shock']], fshock_power: [['t0', 0.05, 'die'], ['near:400', 0.1, 'shock']],
  facid_land: [['near:300', 0.1, 'poison']], fholy_land: [['near:300', 0.1, 'burn', ZBURN([1, 0.8, 0.3])]],
  fvoid_land: [['near:420', 0.1, 'pull', { to: 't0' }]], fvoid_power: [['near:560', 0.05, 'fling', { dist: 380 }]],
};
// the reactions one play gives each of the dead: [[{k, t, from, o}], ...] in the order of SPOT's dead (t0, then the others)
function zReactions(key, at, spots, wielder, area) {
  area = area || 1;
  const rows = REACT[key] || [['t0', 0.05, 'flinch']], out = spots.map(() => []);
  const aim = spots[0].clone().sub(wielder).setY(0).normalize();
  for (const [who, t, k, o0] of rows) {
    const o = o0 ? Object.assign({}, o0) : null; if (o && o.to === 't0') o.to = spots[0];
    let hit = [], from = at;
    if (/^t\d$/.test(who)) { hit = [+who[1]]; from = wielder; }
    else if (who.startsWith('near:')) { const R = +who.slice(5) * area; hit = spots.map((p, i) => [p, i]).filter(([p]) => p.distanceTo(at.clone().setY(0)) <= R).map(x => x[1]); }
    else if (who.startsWith('cone:')) { const R = +who.slice(5); from = wielder; hit = spots.map((p, i) => [p, i]).filter(([p]) => { const d = p.clone().sub(wielder).setY(0); return d.length() <= R && d.normalize().dot(aim) > 0.8; }).map(x => x[1]); }
    else if (who === 'line') { from = wielder; hit = spots.map((p, i) => [p, i]).filter(([p]) => { const d = p.clone().sub(wielder).setY(0), along = d.dot(aim); return along > 0 && d.sub(aim.clone().multiplyScalar(along)).length() < 90; }).map(x => x[1]); }
    for (const i of hit) out[i].push({ k, t: t < 0 ? -10 : t + (who.startsWith('near:') ? spots[i].distanceTo(at) / 2400 : 0), from, o });
  }
  for (const r of out) r.sort((a, b) => a.t - b.t);
  return out;
}


// ================================================================ you (Noah, 2026-11-25: "update the players model in the previews too to
// skyrim player model" - a male Nord, in the class set of whoever uses the weapon - the relics, Tablet weapons and Alduin's Bane
// in the Summoner's - holding the weapon and moving with each ability; tools/playerpreview.py)
const PLR_OF = {   // item -> [class, its weapon's live model (its Tier III), what it's held like]
  vahlok: ['summoner', 'priest_vahlok'], nahkriin: ['summoner', 'priest_nahkriin'], volsung: ['summoner', 'priest_volsung'], zahkriisos: ['summoner', 'priest_zahkriisos'],
  otar: ['summoner', 'priest_otar'], rahgot: ['summoner', 'priest_rahgot'], krosis: ['summoner', 'priest_krosis'], hevnoraak: ['summoner', 'priest_hevnoraak'],
  vokun: ['summoner', 'priest_vokun'], morokei: ['summoner', 'priest_morokei'],
  stormtalons: ['summoner', 'tab_stormtalons'], gravebell: ['summoner', 'tab_gravebell'], talons: ['summoner', 'tab_talons'], riverstone: ['summoner', 'tab_riverstone'],
  wyrmspine_pike: ['summoner', 'tab_wyrmspine'], lanternwick: ['summoner', 'tab_lanternwick'], cinderclaws: ['summoner', 'tab_cinderclaws'], eclipse: ['summoner', 'tab_eclipse'],
  wanderstar: ['summoner', null], frostmourn: ['summoner', 'tab_frostmourn'], silverscythe: ['summoner', 'tab_silverscythe'], bonereaper: ['summoner', 'tab_bonereaper'],
  wyrmtooth: ['summoner', 'tab_wyrmtooth'], bloodmoon: ['summoner', 'tab_bloodmoon'], bane: ['summoner', 'fang'],
  javelin: ['destruction', 'javelin'], soulcaller: ['summoner', null], mutagen: ['alchemist', null], slayer: ['spellsword', 'cls_dragonslayer_slayer'],
  scythe: ['alchemist', 'cls_dragonpriest_alch'], wand: ['destruction', 'cls_daedricwand_carian'], staff: ['summoner', 'cls_dragonbonestaff_radiant'], rapier: ['spellsword', 'cls_dragonbonerapier_ss_fire'],
  ffire: ['alchemist', null], ffrost: ['alchemist', null], fshock: ['alchemist', null], facid: ['alchemist', null], fholy: ['alchemist', null], fvoid: ['alchemist', null],
  summon: ['summoner', null],
};
function plrFor(key) {
  const id = Object.keys(PLR_OF).sort((a, b) => b.length - a.length).find(k => key.startsWith(k + '_'));
  return id ? { id, cls: PLR_OF[id][0], weapon: PLR_OF[id][1] } : { id: '', cls: 'summoner', weapon: null };
}
// what you do: [{k: swing|slam|cast|throw, t}] - a blow for a weapon's hit, both hands over and down for a heavy, the arm out for a
// bolt or a spell (a little kick at each), a throw for the javelin and the flasks
function plrActs(key, style, show) {
  const row = key.split('_').pop(), id = plrFor(key).id;
  if (show) {
    const casts = show.filter(e => e.fly && e.fly[0] === 'hand').map(e => e.t);
    if (casts.length) return casts.map(t => ({ k: /javelin|mutagen|^f/.test(id) ? 'throw' : 'cast', t }));
    if (/summon|soulcaller/.test(id)) return [{ k: 'cast', t: 0 }];
    if (id === 'staff') return [{ k: 'slam', t: 0 }];
  }
  if (/^f(fire|frost|shock|acid|holy|void)$/.test(id)) return [{ k: 'throw', t: 0 }];
  if (id === 'javelin') return [{ k: 'throw', t: 0 }];
  if (style === 'bolt' || /wand|staff|soulcaller|summon/.test(id) && row !== 'heavy') return [{ k: 'cast', t: 0 }];
  if (/heavy/.test(row) || ['fifth', 'kill', 'power'].includes(row)) return [{ k: 'slam', t: 0 }];
  return [{ k: 'swing', t: 0 }];
}
function plrPose(z, t, acts, info) {
  const sd = z.side.rua, sl = z.side.lua;
  let ru = new THREE.Vector3(sd * 0.25, 0.35, -1), rf = new THREE.Vector3(sd * 0.1, 1, -0.25), lu = new THREE.Vector3(sl * 0.2, 0.05, -1), lf = new THREE.Vector3(sl * 0.15, 0.4, -0.9);
  let twist = 0, bend = 0.04 * Math.sin(t * 1.3), lean = 0;
  const L = (a, b, k) => a.clone().normalize().lerp(b.clone().normalize(), Math.max(0, Math.min(1, k)));
  for (const a of acts) {
    const u = t - a.t;
    if (u < -0.12 || u > 1.4) continue;
    const back = Math.max(0, Math.min(1, 1 - (u - 0.45) / 0.6));   // (back to rest after)
    if (a.k === 'swing') {
      const w = sm((u + 0.12) / 0.12), s2 = sm(u / 0.12);
      ru = L(ru, L(new THREE.Vector3(sd * 0.95, -0.3, 0.35), new THREE.Vector3(-sd * 0.45, 0.95, -0.1), s2), w * back);
      rf = L(rf, L(new THREE.Vector3(sd * 0.2, -0.2, 1), new THREE.Vector3(-sd * 0.6, 0.8, 0), s2), w * back);
      twist = (0.45 * (1 - s2) - 0.45 * s2) * w * back;
    } else if (a.k === 'slam') {
      const w = sm((u + 0.12) / 0.12), s2 = sm(u / 0.14);
      const up = new THREE.Vector3(sd * 0.15, 0.25, 1), down = new THREE.Vector3(sd * 0.05, 1, -0.55);
      ru = L(ru, L(up, down, s2), w * back); lu = L(lu, L(new THREE.Vector3(-sl * 0.05, 0.3, 1), new THREE.Vector3(-sl * 0.25, 1, -0.5), s2), w * back);
      rf = L(rf, L(new THREE.Vector3(0, -0.3, 1), new THREE.Vector3(0, 1, -0.3), s2), w * back); lf = L(lf, L(new THREE.Vector3(0, -0.3, 1), new THREE.Vector3(-sl * 0.4, 1, -0.2), s2), w * back);
      bend = -0.35 * s2 * w * back;
    } else if (a.k === 'cast') {
      const w = sm((u + 0.12) / 0.12), kick = Math.exp(-Math.max(0, u) * 9) * (u > 0 ? 1 : 0);
      ru = L(ru, new THREE.Vector3(sd * 0.12, 1, 0.12 + 0.2 * kick), w * back); rf = L(rf, new THREE.Vector3(sd * 0.05, 1, 0.1 + 0.25 * kick), w * back);
      lean = -0.06 * kick;
    } else if (a.k === 'throw') {
      const w = sm((u + 0.12) / 0.12), s2 = sm(u / 0.12);
      ru = L(ru, L(new THREE.Vector3(sd * 0.4, -0.75, 0.6), new THREE.Vector3(sd * 0.1, 1, 0.1), s2), w * back);
      rf = L(rf, L(new THREE.Vector3(0, -0.2, 1), new THREE.Vector3(0, 1, 0.05), s2), w * back);
      twist = (0.35 * (1 - s2) - 0.25 * s2) * w * back;
    }
  }
  zRot(z, 'spine', ZZ, twist * 0.5); zRot(z, 'spine1', ZZ, twist * 0.5); zRot(z, 'spine', ZX, bend + lean);
  zAim(z, 'rua', ru); zAim(z, 'rfa', rf, 0.9); zAim(z, 'lua', lu); zAim(z, 'lfa', lf, 0.9);
  zRot(z, 'lthigh', ZX, 0.08); zRot(z, 'rthigh', ZX, -0.05); zRot(z, 'lcalf', ZX, -0.1); zRot(z, 'rcalf', ZX, -0.1);
  zApply(z);
  z.holder.position.copy(info.home); z.holder.rotation.y = info.face;
}
