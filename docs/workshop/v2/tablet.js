// ================================================================ the Tablet's roll (Noah, Nov 26 night: "remake the roll animation in the tablet ... give me 4 roll
// options in a preview for me to select"): the Tablet of Mysteries itself (our model, read from the mod), the weapons it holds, and four
// ways for it to roll - each ending in the won weapon's rarity colour: Rare blue, Epic purple, Legendary gold.
const RARITY = { rare: { n: 'Rare', c: ['#06163a', '#3d8bff', '#d8e8ff'] }, epic: { n: 'Epic', c: ['#14052e', '#a34bff', '#f0dcff'] }, legendary: { n: 'Legendary', c: ['#2e1600', '#ffb52e', '#fff4d6'] } };
const ROLL_KEYS = ['tab_stormtalons', 'tab_cinderclaws', 'tab_frostmourn', 'tab_lanternwick', 'tab_wyrmspine', 'tab_eclipse'];
function standUp(d, len) {   // a weapon stood up on its long axis, centred, len units long
  const m = weaponModel(d), { C, ev } = pca(m.pts), ax = ev[0], ay = ev[1];
  const az = [ax[1] * ay[2] - ax[2] * ay[1], ax[2] * ay[0] - ax[0] * ay[2], ax[0] * ay[1] - ax[1] * ay[0]];
  const inner = new THREE.Group(); inner.add(m.g); inner.matrixAutoUpdate = false;
  inner.matrix.multiplyMatrices(new THREE.Matrix4().set(ay[0], ay[1], ay[2], 0, ax[0], ax[1], ax[2], 0, az[0], az[1], az[2], 0, 0, 0, 0, 1), new THREE.Matrix4().makeTranslation(-C[0], -C[1], -C[2]));
  const lens = m.pts.map(p => (p[0] - C[0]) * ax[0] + (p[1] - C[1]) * ax[1] + (p[2] - C[2]) * ax[2]), L = Math.max(...lens) - Math.min(...lens);
  const g = new THREE.Group(); g.add(inner); g.scale.setScalar(len / Math.max(1, L)); g.meshes = []; m.g.traverse(x => x.isMesh && g.meshes.push(x));
  return g;
}
function ghostOf(w, col) {   // the same weapon as a ghost of light (the reel's flicker)
  const g = w.clone(true);
  g.traverse(x => { if (x.isMesh) { x.material = new THREE.MeshBasicMaterial(Object.assign({ color: v3c(col), transparent: true, depthWrite: false, side: THREE.DoubleSide }, ADD())); x.layers.enable(1); } });
  return g;
}
function rollView(cv, kind) {
  const v = fxStage(cv, { az: 0.15, el: .2, r: 520, ty: 150, spin: false });
  const scene = v.scene, top = 175;   // (the tablet's face, and where the weapon shows above it)
  const stand = new THREE.Group(); scene.add(stand);
  loadWeapon('tablet').then(d => { const m = weaponModel(d).g; m.rotation.x = -Math.PI / 2; stand.add(m); }).catch(() => {});
  let W = [], G = [], rar = 'epic', P = null, disc = null, vols = [], winner = null;
  const pool = Promise.all(ROLL_KEYS.map(k => loadWeapon(k).catch(() => null))).then(ds => { W = ds.filter(Boolean).map(d => standUp(d, 110)); build(); });
  function build() {
    for (const x of [...G, ...W, ...vols, P, disc]) if (x && x.parent) x.parent.remove(x);
    const R = RARITY[rar];
    G = W.map(w => ghostOf(w, R.c[1]));
    for (const w of W) { w.visible = false; scene.add(w); }
    for (const g of G) { g.visible = false; scene.add(g); }
    winner = W[W.length - 1];
    P = Particles({ el: 'arcane', cap: 900, c: R.c, gain: 2.6, grav: [0, kind === 'storm' ? 90 : 20, 0], turb: kind === 'storm' ? 90 : 25, psz: 3 }); scene.add(P);
    vols = [];
    const vol = (o, f) => { const x = Volume(Object.assign({ el: 'arcane' }, o)); x.u.c0.value = v3c(R.c[0]); x.u.c1.value = v3c(R.c[1]); x.u.c2.value = v3c(R.c[2]); scene.add(x); vols.push({ x, f }); return x; };
    if (kind === 'reel') {
      disc = RuneDisc('arcane', 46); disc.u.c1.value = v3c(R.c[1]); disc.u.c2.value = v3c(R.c[2]); disc.rotation.set(0, 0, 0); disc.position.set(0, top - 62, 14); scene.add(disc);
      vol({ shape: 0, sp: [.9, 0, 0, 0], dens: 2, cut: .4, swirl: 1, gain: .5 }, (x, t) => { x.position.set(0, top + 30, 0); const s = 55 + 8 * Math.sin(t * 5); x.scale.set(s, s * 1.3, s); x.u.life.value = env(t, 0, .3, 3.2, 4.2) * .6; });
      for (let i = 0; i < 400; i++) { const a = Math.random() * 6.283; P.add1([Math.cos(a) * 40, top - 40 + Math.sin(a) * 40, 18], [Math.cos(a) * 160, Math.sin(a) * 160, 60], Math.random() * 3.2, .5, 2.5, .6, 2, 1); }
    } else if (kind === 'pillar') {
      vol({ shape: 4, sp: [.16, .26, 0, 0], dens: 5, cut: .05, rise: 2.5, gain: .55 }, (x, t) => { x.position.set(0, 600, 0); x.scale.set(70, 600, 70); x.u.life.value = env(t, .1, .35, 4.6, 5.2); x.u.sp.value.x = .12 + .1 * env(t, 3.2, 3.4, 4.2, 5); });
      for (let i = 0; i < 500; i++) P.add1([(Math.random() - .5) * 40, top - 30, (Math.random() - .5) * 40], [(Math.random() - .5) * 40, 300 + Math.random() * 500, (Math.random() - .5) * 40], .1 + Math.random() * 4.4, .9, 3, .8, .4, 0);
    } else if (kind === 'wheel') {
      disc = RuneDisc('arcane', 170); disc.u.c1.value = v3c(R.c[1]); disc.u.c2.value = v3c(R.c[2]); scene.add(disc);
      for (let i = 0; i < 400; i++) { const a = Math.random() * 6.283; P.add1([Math.cos(a) * 150, 6, Math.sin(a) * 150], [0, 70 + Math.random() * 90, 0], Math.random() * 4.6, 1, 2.5, .5, .2, 0); }
    } else {   // storm
      vol({ shape: 1, sp: [.55, .9, 0, 0], dens: 5, cut: .2, swirl: 2.2, rise: 1.8 }, (x, t) => { x.position.set(0, top + 120, 0); x.scale.set(110 * (1 - .7 * env(t, 3, 3.6, 9, 9)), 140, 110 * (1 - .7 * env(t, 3, 3.6, 9, 9))); x.u.life.value = env(t, 0, .4, 3.2, 3.7); });
      vol({ shape: 0, sp: [.9, 0, 0, 0], dens: 12, cut: .05 }, (x, t) => { x.position.set(0, top + 60, 0); const s = 20 + 90 * ease((t - 3.3) / .4); x.scale.set(s, s, s); x.u.life.value = env(t, 3.3, 3.35, 3.5, 4); });
      for (let i = 0; i < 700; i++) { const a = Math.random() * 6.283, r = 30 + Math.random() * 90; P.add1([Math.cos(a) * r, top - 20, Math.sin(a) * r], [-Math.sin(a) * 260, 90 + Math.random() * 160, Math.cos(a) * 260], Math.random() * 3.1, 1.1, 3, .8, .6, 0); }
    }
    v.ready = true;
  }
  v.setRarity = r => { rar = r; if (W.length) build(); };
  const DUR = 5.2;
  v.play({ g: new THREE.Group(), dur: DUR, lightAt: new THREE.Vector3(0, top + 40, 40), col: '#ffffff', f: () => {}, light: t => .4 + 1.6 * env(t, 3.2, 3.4, 4.6, 5.2) });
  const base = v.tick;
  v.tick = t => {
    base(t);
    if (!W.length) return;
    const tt = v.t0 == null ? 0 : (t - v.t0) % (DUR + .5), R = RARITY[rar];
    P.u.time.value = tt; for (const { x, f } of vols) { x.u.time.value = t; f(x, tt); x.visible = x.u.life.value > .003; }
    if (disc) { disc.u.time.value = t * (kind === 'wheel' ? 2.2 : 3.5) * (1 - .8 * ease(tt / 3.2)); disc.u.on.value = env(tt, 0, .3, 4.4, 5.2) * (kind === 'reel' ? .55 : 1); }
    for (const w of W) w.visible = false; for (const g of G) g.visible = false;
    // a reel slowing: which weapon shows at tt (fast to slow), the winner after 3.2 s
    const step = tt < 3.2 ? Math.floor(Math.pow(tt / 3.2, .45) * 26) : -1, cur = step < 0 ? W.length - 1 : step % W.length;
    const spinY = t * 1.4;
    if (kind === 'reel') {
      if (step >= 0) { const g = G[cur]; g.visible = true; g.position.set(0, top + 20, 0); g.rotation.set(0, spinY, 0); }
      else { const k = ease((tt - 3.2) / .8); winner.visible = true; winner.position.set(0, top - 10 + 40 * k, 0); winner.rotation.set(0, spinY, 0); }
    } else if (kind === 'pillar') {
      if (step >= 0) { const w = W[cur]; w.visible = true; const ph = (tt * 26 / Math.max(1, step + 1)) % 1; w.position.set(0, top + 10 + 30 * Math.sin(step * 2.1), 0); w.rotation.set(0, spinY * 2, 0); }
      else { winner.visible = true; winner.position.set(0, top + 30 + 6 * Math.sin(t * 2), 0); winner.rotation.set(0, spinY, 0); }
    } else if (kind === 'wheel') {
      const ang = tt < 3.4 ? 14 * (1 - Math.pow(1 - tt / 3.4, 2.4)) : 14;   // (fast, then easing to a stop with the winner in front)
      W.forEach((w, i) => { const a = ang + i / W.length * 6.283 - 14 + 6.283 * (W.length - 1 - i) / W.length * 0; w.visible = true;
        const A = (i - (W.length - 1)) / W.length * 6.283 + (14 - ang); w.position.set(Math.sin(A) * 150, top - 10, Math.cos(A) * 150); w.rotation.set(0, A, 0);
        if (i === W.length - 1 && tt > 3.5) { const k = ease((tt - 3.5) / .6); w.position.set(0, top - 10 + 40 * k, 150 * (1 - k)); w.rotation.set(0, spinY * k, 0); } });
    } else {
      if (tt < 3.3) W.forEach((w, i) => { w.visible = true; const a = tt * (5 + i) + i * 1.05, r = 70 + 20 * Math.sin(tt * 3 + i), y = top + 40 + ((tt * 60 + i * 40) % 160); w.position.set(Math.cos(a) * r, y, Math.sin(a) * r); w.rotation.set(tt * (3 + i), tt * 2, tt * (2 + i * .5)); });
      else { winner.visible = true; winner.position.set(0, top + 60 + 5 * Math.sin(t * 2), 0); winner.rotation.set(0, spinY, 0); }
    }
    // the winner wears its rarity's light once it shows (a ghost of light laid over it)
    if (tt >= 3.2 && winner.visible) { const g = G[G.length - 1]; g.visible = true; g.position.copy(winner.position); g.rotation.copy(winner.rotation); g.scale.copy(winner.scale).multiplyScalar(1.03);
      g.traverse(x => { if (x.isMesh) x.material.opacity = .35 + .25 * Math.sin(t * 6); }); }
    // the winner's rarity light round it once it shows
    v.scene.children.forEach(c => { if (c.isPointLight) c.color.set(R.c[1]); });
  };
  return v;
}
