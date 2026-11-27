// ================================================================ abilities in action (Noah, Nov 27: "I want the preview to show what the weapon description
// says it does"): every move and wonder is a short script of what it does - you swing, slam, cast, hold a beam or throw; bolts fly
// (curving, raining from the sky, forking), beams hold, lightning leaps, spirits rise and fight, runes burn, the dead burn, freeze,
// stagger, fall and die - each beat on its own time, so the preview plays the description. You are Skyrim's male Nord in the
// class's set holding the model you picked (in its tier look); the dead are Mihail's zombies from your game.
const SPOT2 = { wielder: new THREE.Vector3(-230, 0, 130), t0: new THREE.Vector3(40, 0, 0),
  others: [new THREE.Vector3(190, 0, -120), new THREE.Vector3(-40, 0, -190), new THREE.Vector3(200, 0, 120), new THREE.Vector3(-150, 0, -50)] };
const REACT_OF = { fire: 'burn', frost: 'freeze', shock: 'shock', shadowfire: 'burn', holy: 'burn', poison: 'poison', arcane: 'flinch', blood: 'bleed', spirit: 'lost', earth: 'knock', wind: 'knock' };
const SPOTS = [SPOT2.t0, ...SPOT2.others];
function actionView(cv) {
  const v = fxStage(cv, { az: -.95, el: .26, r: 600, ty: 70, spin: false });   // (over your shoulder, as the game's camera: you in front, the dead ahead)
  v.tgt.set(-40, 75, 30);
  const scene = v.scene;
  const zs = [], zinfo = SPOTS.map(p => { const d = SPOT2.wielder.clone().sub(p).setY(0).normalize(); return { home: p.clone().setY(0), from: SPOT2.wielder.clone(), face: Math.atan2(-d.x, -d.z) }; });
  SPOTS.forEach((p, i) => { const key = ZOM_KEYS[i % ZOM_KEYS.length];
    zomData(key).then(([sk, body]) => { const z = makeZombie(sk, body, (i * .37 + .11) % 1); zs[i] = z; scene.add(z.holder); zPose(z, [], 0, zinfo[i]); }).catch(e => console.warn('zombie', key, e)); });
  const pdir = SPOT2.t0.clone().sub(SPOT2.wielder).setY(0).normalize(), pinfo = { home: SPOT2.wielder.clone().setY(0), face: Math.atan2(-pdir.x, -pdir.z) };
  let plr = null, want = '', plrCls = 'summoner';
  v.setPlayer = (cls, weaponKey, look, lookEl) => {
    plrCls = cls;
    const w = cls + '|' + weaponKey + '|' + look + '|' + (lookEl || ''); if (w === want) return; want = w;
    Promise.all([zomData(cls, 'plr'), weaponKey ? loadWeapon(weaponKey).catch(() => null) : null]).then(([[sk, body], wd]) => {
      if (want !== w) return;
      const z = makeZombie(sk, body, .5);
      if (wd && z.bones.WEAPON) {
        const wm = weaponModel(wd).g; wm.scale.setScalar((wd.scale || 1) * (SCYTHES.includes(weaponKey) ? 1.25 : 1));
        if (look && typeof weaponAura === 'function' && wd.fx && wd.fx[look]) { const art = weaponFx(wd, look); const a = weaponAura(wd, art.pts, Object.assign(lookStyle(wd, look), elemStyle(lookEl), { k: 1, span: 120 })); wm.add(a); z.aura = a; z.auraHolder = wm; }
        z.bones.WEAPON.add(wm); z.weapon = wm;
      }
      if (plr) scene.remove(plr.holder); plr = z; scene.add(z.holder); plrPose(z, 0, [], pinfo);
    }).catch(e => console.warn('player', e));
  };
  // ---- where things are: a spot's chest, the hand's tip, the sky over a spot
  const chest = (p, h) => new THREE.Vector3(p.x, h == null ? 70 : h, p.z);
  const posOf = (who, h) => {
    if (Array.isArray(who)) return chest(new THREE.Vector3(who[0], 0, who[1]), h);
    if (who === 'me') return chest(SPOT2.wielder, h);
    if (who === 'front') return chest(SPOT2.t0.clone().lerp(SPOT2.wielder, .45), h);
    const i = /^t(\d)$/.test(who) ? +who[1] : 0; return chest(SPOTS[i], h);
  };
  const tipPos = () => {
    if (!plr || !plr.weapon) return chest(SPOT2.wielder, 120).add(new THREE.Vector3(30, 0, -20));
    plr.weapon.updateMatrixWorld(true);
    const box = new THREE.Box3().setFromObject(plr.weapon), sh = chest(SPOT2.wielder, 120);
    let best = null, bd = -1;
    for (const x of [box.min.x, box.max.x]) for (const y of [box.min.y, box.max.y]) for (const z of [box.min.z, box.max.z]) { const p = new THREE.Vector3(x, y, z), d = p.distanceTo(sh); if (d > bd) { bd = d; best = p; } }
    return best;
  };
  // ---- the pieces a script is made of, each with its own clock
  let parts = [], rx = null, acts = [], dur = 3, lights = [];
  const clearParts = () => { for (const p of parts) if (p.g && p.g.parent) p.g.parent.remove(p.g); parts = []; lights = []; };
  const E = el => ELEM[el] || ELEM.fire;
  function partFx(ev, el) {
    const at = posOf(ev.at || 't0', 0);
    const fx = makeEffect(ev.el || el, ev.role, ev.opt, { at: [at.x, 0, at.z], size: ev.size || 1, seed: 11 + parts.length });
    scene.add(fx.g);
    parts.push({ g: fx.g, f: t => { const u = t - ev.t; fx.g.visible = u >= 0 && u <= fx.dur; if (fx.g.visible) fx.f(u); } });
    lights.push(t => { const u = t - ev.t; return u >= 0 && fx.light ? [fx.light(u), fx.lightAt, fx.col] : null; });
    dur = Math.max(dur, ev.t + fx.dur);
  }
  function partBolt(ev, el) {   // a projectile: a ball of the element with its trail; straight, curving, or falling from the sky
    const g = new THREE.Group(), e = ev.el || el;
    const core = Volume({ el: e, shape: 0, sp: [.9, 0, 0, 0], dens: 12, cut: .05, gain: 1.2 }); core.scale.setScalar((ev.size ? 14 * ev.size : 14) * (ev.model ? .45 : 1)); g.add(core);
    let mdl = null;   // (a thrown weapon flies as itself - the javelin - its point along the way it flies)
    if (ev.model) loadWeapon(ev.model).then(d => { mdl = standUp(d, ev.len || 130); g.add(mdl); }).catch(() => {});
    const P = Particles({ el: e, cap: 260, gain: 2.4, psz: 2.4, turb: 10 }); g.add(P);
    scene.add(g);
    const d = ev.d || .35, bend = ev.curve ? new THREE.Vector3(0, 60, 90) : new THREE.Vector3();
    let from = null, to = null, seeded = false;
    const at = u => { const k = Math.min(1, Math.max(0, u / d)); const p = from.clone().lerp(to, k); p.addScaledVector(bend, Math.sin(k * Math.PI)); return p; };
    parts.push({ g, f: (t, tt) => {
      const u = t - ev.t; g.visible = u >= 0 && u <= d + .4;
      if (!g.visible) return;
      if (!from) { from = ev.from === 'sky' ? posOf(ev.to, 900).add(new THREE.Vector3(120, 0, -160)) : ev.from && ev.from !== 'hand' ? posOf(ev.from) : tipPos(); to = posOf(ev.to, ev.from === 'sky' ? 10 : 70); }
      if (!seeded) { seeded = true; for (let i = 0; i < 240; i++) { const k = i / 240, q = at(k * d); P.add1([q.x, q.y, q.z], [(Math.random() - .5) * 20, (Math.random() - .5) * 20, (Math.random() - .5) * 20], ev.t + k * d, .3, 5 * (ev.size || 1), 1, 0, 0); } }
      core.visible = u <= d; core.position.copy(at(u)); core.u.time.value = t; core.u.life.value = 1; P.u.time.value = t;
      if (mdl) { mdl.visible = u <= d; mdl.position.copy(core.position); const dir = at(Math.min(d, u + .02)).sub(at(Math.max(0, u - .02))); if (dir.lengthSq() > 1e-6) mdl.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.normalize()); }
    } });
    if (ev.hit) partFx(Object.assign({ t: ev.t + d, at: ev.to }, ev.hit), el);
    dur = Math.max(dur, ev.t + d + .5);
  }
  function partArc(ev, el) {   // lightning (or any element's crackle) leaping from one to another
    const b = Bolts({ el: ev.el || (el === 'shock' ? 'shock' : el), width: 4, gain: 2.6 }); scene.add(b);
    parts.push({ g: b, f: t => { const u = t - ev.t, on = u >= 0 && u <= (ev.d || .3) ? 1 : 0; const a = posOf(ev.from), c = posOf(ev.to); b.at(t, [[[a.x, a.y, a.z], [c.x, c.y, c.z], .3]], on); } });
    dur = Math.max(dur, ev.t + (ev.d || .3) + .3);
  }
  function partBeam(ev, el) {   // the held beam, from the weapon's tip to what it holds; wider and hotter the longer it's held
    const g = new THREE.Group(), e = ev.el || el;
    const core = Volume({ el: e, shape: 4, sp: [.2, .2, 0, 0], dens: 14, cut: 0, rise: 7, gain: 1.6 }), halo = Volume({ el: e, shape: 4, sp: [.55, .55, 0, 0], dens: 3, cut: .25, rise: 5, gain: .8 });
    g.add(core); g.add(halo); const bolts = Bolts({ el: e, width: 3, gain: 2.2 }); g.add(bolts);
    const forks = (ev.fork || []).map(() => { const f = Bolts({ el: e, width: 3.5, gain: 2.6 }); g.add(f); return f; });
    const spark = Particles({ el: e, cap: 400, gain: 2.6, psz: 3, grav: [0, 40, 0] }); g.add(spark);
    for (let i = 0; i < 400; i++) { const tt = ev.t + Math.random() * ev.d, a = Math.random() * 6.283; const p = posOf(ev.to); spark.add1([p.x, p.y, p.z], [Math.cos(a) * 260, Math.random() * 200, Math.sin(a) * 260], tt, .4, 4, 1, 3, 1); }
    scene.add(g);
    parts.push({ g, f: t => {
      const u = t - ev.t, k = env(u, 0, .15, ev.d - .2, ev.d), grow = Math.min(1, Math.max(0, u / ev.d));
      g.visible = k > .01; if (!g.visible) return;
      const from = tipPos(), to = posOf(ev.to), dir = to.clone().sub(from), L = dir.length(); dir.normalize();
      for (const [x, w] of [[core, 8 + 14 * grow], [halo, 22 + 30 * grow]]) { x.position.copy(from).addScaledVector(dir, L / 2); x.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir); x.scale.set(w, L / 2, w); x.u.life.value = k; x.u.time.value = t; }
      bolts.at(t, [[[from.x, from.y, from.z], [to.x, to.y, to.z], .1]], k);
      forks.forEach((f, i) => { const c = posOf(ev.fork[i]); f.at(t + i, [[[to.x, to.y, to.z], [c.x, c.y, c.z], .3]], k); });
      spark.u.time.value = t;
    } });
    lights.push(t => { const u = t - ev.t; return u >= 0 && u <= ev.d ? [1.6, posOf(ev.to), E(e).light] : null; });
    dur = Math.max(dur, ev.t + ev.d + .4);
  }
  function partGhost(ev, el) {   // a spectral figure (an ancestor, a scythe's shade): your own body in light, rising, striking
    const e = ev.el || el, col = v3c(E(e).c[1]), g = new THREE.Group(); scene.add(g);
    const info = { home: new THREE.Vector3(ev.at[0], 0, ev.at[1]), face: 0 };
    const tgt = posOf(ev.face || 't0'); info.face = Math.atan2(-(tgt.x - info.home.x), -(tgt.z - info.home.z)) + Math.PI;
    let z = null;
    zomData(ev.cls || plrCls, 'plr').then(([sk, body]) => {
      z = makeZombie(sk, body, .3);
      z.holder.traverse(o => { if (o.isMesh) { o.material = new THREE.MeshBasicMaterial(Object.assign({ color: col, transparent: true, opacity: .55, skinning: true, depthWrite: false }, ADD())); o.layers.enable(1); } });
      g.add(z.holder);
    }).catch(() => {});
    const P = Particles({ el: e, cap: 300, gain: 2, psz: 3, grav: [0, 120, 0], turb: 30 }); g.add(P);
    for (let i = 0; i < 300; i++) P.add1([info.home.x + (Math.random() - .5) * 60, Math.random() * 150, info.home.z + (Math.random() - .5) * 60], [0, 40, 0], ev.t + Math.random() * ev.d, .8, 4, 1, .2, 0);
    parts.push({ g, f: t => {
      const u = t - ev.t, k = env(u, 0, .35, ev.d - .4, ev.d); g.visible = k > .01; P.u.time.value = t;
      if (!z || !g.visible) return;
      z.holder.traverse(o => { if (o.isMesh) o.material.opacity = .55 * k; });
      plrPose(z, u, (ev.swings || [.6, 1.3]).map(s => ({ k: ev.act || 'swing', t: s })), info);
      z.holder.position.y = -30 * (1 - Math.min(1, u / .35));
    } });
    dur = Math.max(dur, ev.t + ev.d + .2);
  }
  function partOrbit(ev, el) {   // orbs of light circling you
    const g = new THREE.Group(), e = ev.el || el, orbs = [];
    for (let i = 0; i < (ev.n || 3); i++) { const o = Volume({ el: e, shape: 0, sp: [.9, 0, 0, 0], dens: 12, cut: .05, gain: 1.2 }); o.scale.setScalar(12); g.add(o); orbs.push(o); }
    scene.add(g);
    parts.push({ g, f: t => { const u = t - ev.t, k = env(u, 0, .3, ev.d - .3, ev.d); g.visible = k > .01;
      orbs.forEach((o, i) => { const a = u * 2.4 + i * 6.283 / orbs.length; o.position.set(SPOT2.wielder.x + Math.cos(a) * 80, 110 + Math.sin(u * 3 + i) * 10, SPOT2.wielder.z + Math.sin(a) * 80); o.u.life.value = k; o.u.time.value = t; }); } });
    dur = Math.max(dur, ev.t + ev.d);
  }
  function partMotes(ev, el) {   // a stream of motes from one place to another (souls, essence, magicka)
    const e = ev.el || el, P = Particles({ el: e, cap: 200, gain: 2.4, psz: 3, c: ev.c }); scene.add(P);
    const a = posOf(ev.from), b = posOf(ev.to);
    for (let i = 0; i < 200; i++) { const t0 = ev.t + Math.random() * (ev.d || .8), sp = b.clone().sub(a); P.add1([a.x + (Math.random() - .5) * 40, a.y + (Math.random() - .5) * 40, a.z + (Math.random() - .5) * 40], [sp.x / .6, sp.y / .6 + 40, sp.z / .6], t0, .6, 4, 1, .01, 0); }
    parts.push({ g: P, f: t => { P.u.time.value = t; } });
    dur = Math.max(dur, ev.t + (ev.d || .8) + .7);
  }
  function partMark(ev, el) {   // a small rune burned under one of the dead (marked, branded, catalysed)
    const d = RuneDisc(ev.el || el, 34); const p = posOf(ev.at, 0); d.position.set(p.x, 1.5, p.z); scene.add(d);
    parts.push({ g: d, f: t => { const u = t - ev.t; d.visible = u >= 0 && u <= ev.d; d.u.time.value = t; d.u.on.value = env(u, 0, .15, ev.d - .2, ev.d); } });
  }
  // ---- what the dead do: each event names who, when and how; the struck ones react as the game makes them
  function buildReactions(list, el) {
    const out = SPOTS.map(() => []);
    for (const ev of list) {
      const k = ev.r === 'elem' ? (REACT_OF[ev.el || el] || 'flinch') : ev.r;
      const o = k === 'burn' ? { col: E(ev.el || el).tint } : ev.r === 'pull' ? { to: SPOT2.wielder } : null;
      let hit = [];
      if (/^t\d$/.test(ev.who)) hit = [+ev.who[1]];
      else if (ev.who === 'all') hit = SPOTS.map((_, i) => i);
      else if (ev.who.startsWith('near:')) { const [R, c] = ev.who.slice(5).split('@'); const at = posOf(c || 't0', 0); hit = SPOTS.map((p, i) => [p, i]).filter(([p]) => p.distanceTo(new THREE.Vector3(at.x, 0, at.z)) <= +R).map(x => x[1]); }
      else if (ev.who === 'line') { const aim = SPOT2.t0.clone().sub(SPOT2.wielder).setY(0).normalize(); hit = SPOTS.map((p, i) => [p, i]).filter(([p]) => { const d = p.clone().sub(SPOT2.wielder).setY(0), al = d.dot(aim); return al > 0 && d.sub(aim.clone().multiplyScalar(al)).length() < 110; }).map(x => x[1]); }
      for (const i of hit) out[i].push({ k, t: ev.t, from: ev.from ? posOf(ev.from, 0) : SPOT2.wielder.clone(), o });
    }
    for (const r of out) r.sort((a, b) => a.t - b.t);
    return out;
  }
  // a script: [{k: act|fx|bolt|rain|arc|beam|ghost|orbit|motes|mark|react|dash, t, ...}]
  // the magicka bar, shown while a move's beam drains it (Nov 27: "make the beam cost magica")
  const host = cv.parentElement; if (host && getComputedStyle(host).position === 'static') host.style.position = 'relative';
  const mbar = document.createElement('div'); mbar.className = 'mbar'; mbar.hidden = true; mbar.innerHTML = '<i></i><span>Magicka</span>'; if (host) host.appendChild(mbar);
  let manaBeams = [];
  v.script = (sc, el) => {
    clearParts(); dur = 2.4; acts = [];
    manaBeams = sc.filter(e => e.k === 'beam' && e.mana); mbar.hidden = !manaBeams.length;
    const thrown = sc.filter(e => e.k === 'bolt' && e.model); v.away = thrown.length ? [Math.min(...thrown.map(e => e.t)), Math.max(...thrown.map(e => e.t + (e.d || .35)))] : null;
    const reacts = [];
    let dash = null;
    for (const ev of sc) {
      if (ev.k === 'act') { if (ev.a === 'hold') for (let s = ev.t; s < ev.t + ev.d; s += .35) acts.push({ k: 'cast', t: s }); else acts.push({ k: ev.a, t: ev.t }); }
      else if (ev.k === 'fx') partFx(ev, el);
      else if (ev.k === 'bolt') partBolt(ev, el);
      else if (ev.k === 'rain') { const r = mulberry(7); for (let i = 0; i < ev.n; i++) { const c = posOf(ev.at || 't0', 0), a = r() * 6.283, R = r() * (ev.R || 220); partBolt({ t: ev.t + i * (ev.d / ev.n), from: 'sky', to: [c.x + Math.cos(a) * R, c.z + Math.sin(a) * R], d: .35, size: ev.size || .8, hit: ev.hit, el: ev.el }, el); } }
      else if (ev.k === 'arc') partArc(ev, el);
      else if (ev.k === 'beam') partBeam(ev, el);
      else if (ev.k === 'ghost') partGhost(ev, el);
      else if (ev.k === 'orbit') partOrbit(ev, el);
      else if (ev.k === 'motes') partMotes(ev, el);
      else if (ev.k === 'mark') partMark(ev, el);
      else if (ev.k === 'react') reacts.push(ev);
      else if (ev.k === 'dash') dash = ev;
    }
    rx = buildReactions(reacts, el);
    v.dash = dash;
    v.play({ g: new THREE.Group(), dur, lightAt: chest(SPOT2.t0), col: E(el).light, f: () => {}, light: () => 0 });
  };
  const base = v.tick, L = new THREE.PointLight(0xffffff, 0, 1100, 2); scene.add(L);
  v.tick = t => {
    base(t);
    const tt = v.fx && v.t0 != null ? (t - v.t0) % (v.fx.dur + .5) : 0;
    for (const p of parts) p.f(tt, t);
    if (manaBeams.length) { let m = 100; for (const b of manaBeams) { const u = Math.min(b.d, Math.max(0, tt - b.t)); m -= 34 * u; m += 18 * Math.max(0, tt - b.t - b.d); } mbar.firstChild.style.width = Math.max(0, Math.min(100, m)) + '%'; mbar.dataset.low = String(m < 30); }
    let best = null; for (const l of lights) { const r = l(tt); if (r && (!best || r[0] > best[0])) best = r; }
    if (best) { L.intensity = best[0] * 2.4; L.position.copy(best[1]); L.color.set(best[2] || '#ffffff'); } else L.intensity = 0;
    zs.forEach((z, i) => z && zPose(z, (rx && rx[i]) || [], tt, zinfo[i]));
    if (plr) {
      const info = pinfo;
      if (v.dash) { const u = Math.min(1, Math.max(0, (tt - v.dash.t) / v.dash.d)), back = tt > v.dash.t + v.dash.d + .8 ? 1 : 0; const to = new THREE.Vector3(v.dash.to[0], 0, v.dash.to[1]); info.home = SPOT2.wielder.clone().lerp(to, back ? 0 : ease(u)); }
      else info.home = SPOT2.wielder.clone().setY(0);
      if (plr.weapon) plr.weapon.visible = !(v.away && tt >= v.away[0] && tt <= v.away[1]);
      plrPose(plr, tt, acts, info); if (plr.aura) plr.aura.tick(t, plr.auraHolder);
    }
  };
  // (the older call: one kit part as a light or a heavy - kept for the kit pages)
  v.ability = sp => v.script([{ k: 'act', a: sp.cast ? 'cast' : sp.act, t: .2 }, { k: 'fx', t: .45, role: sp.role, opt: sp.opt }, { k: 'react', t: .5, who: sp.role === 'impact' ? 't0' : 'near:420@t0', r: 'elem' }], sp.el);
  return v;
}
