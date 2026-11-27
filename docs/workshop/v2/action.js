// ================================================================ abilities in action (Noah, Nov 26 night: "as well as the weapons abilities like what they
// do in game when attacking"): you - Skyrim's male Nord in the class's set, holding the model you picked - swing, slam or cast at the
// night's own dead (Mihail's zombies, read from your game), and the ability's effect, built from its element kit, plays where it lands
// while they react the way the game makes them (burn, freeze, shock, stagger, fall).
const SPOT2 = { wielder: new THREE.Vector3(-230, 0, 130), t0: new THREE.Vector3(40, 0, 0),
  others: [new THREE.Vector3(190, 0, -120), new THREE.Vector3(-40, 0, -190), new THREE.Vector3(200, 0, 120), new THREE.Vector3(-150, 0, -50)] };
const REACT_OF = { fire: 'burn', frost: 'freeze', shock: 'shock', shadowfire: 'burn', holy: 'burn', poison: 'poison', arcane: 'flinch', blood: 'bleed', spirit: 'lost', earth: 'knock', wind: 'knock' };
function actionView(cv) {
  const v = fxStage(cv, { az: 2.4, el: .3, r: 720, ty: 70, spin: false });
  const scene = v.scene;
  const spots = [SPOT2.t0, ...SPOT2.others];
  const zs = [], zinfo = spots.map(p => { const d = SPOT2.wielder.clone().sub(p).setY(0).normalize(); return { home: p.clone().setY(0), from: SPOT2.wielder.clone(), face: Math.atan2(-d.x, -d.z) }; });
  spots.forEach((p, i) => { const key = ZOM_KEYS[i % ZOM_KEYS.length];
    zomData(key).then(([sk, body]) => { const z = makeZombie(sk, body, (i * .37 + .11) % 1); zs[i] = z; scene.add(z.holder); zPose(z, [], 0, zinfo[i]); }).catch(e => console.warn('zombie', key, e)); });
  const pdir = SPOT2.t0.clone().sub(SPOT2.wielder).setY(0).normalize(), pinfo = { home: SPOT2.wielder.clone().setY(0), face: Math.atan2(-pdir.x, -pdir.z) };
  let plr = null, want = '', spec = null, rx = null, acts = [], proj = null;
  v.setPlayer = (cls, weaponKey, look) => {
    const w = cls + '|' + weaponKey + '|' + look; if (w === want) return; want = w;
    Promise.all([zomData(cls, 'plr'), weaponKey ? loadWeapon(weaponKey).catch(() => null) : null]).then(([[sk, body], wd]) => {
      if (want !== w) return;
      const z = makeZombie(sk, body, .5);
      if (wd && z.bones.WEAPON) {
        const wm = weaponModel(wd).g; wm.scale.setScalar((wd.scale || 1) * (SCYTHES.includes(weaponKey) ? 1.25 : 1));
        if (look && typeof weaponAura === 'function') { const art = weaponFx(wd, look); const a = weaponAura(wd, art.pts, Object.assign(lookStyle(wd, look), { k: 1, span: 120 })); wm.add(a); z.aura = a; z.auraHolder = wm; }
        z.bones.WEAPON.add(wm); z.weapon = wm;
      }
      if (plr) scene.remove(plr.holder); plr = z; scene.add(z.holder); plrPose(z, 0, [], pinfo);
    }).catch(e => console.warn('player', e));
  };
  // spec: { el, act: swing|slam|cast, role, opt, size, cast (a bolt flies from the hand first) }
  v.ability = sp => {
    spec = sp;
    const T = sp.cast ? .62 : sp.act === 'slam' ? .42 : .32;   // (when it lands)
    const at = sp.role === 'impact' ? [SPOT2.t0.x, 0, SPOT2.t0.z] : sp.role === 'burst' && sp.opt !== 'big' ? [SPOT2.t0.x - 140, 0, SPOT2.t0.z + 60] : [SPOT2.t0.x, 0, SPOT2.t0.z];
    const fx = makeEffect(sp.el, sp.role, sp.opt, { at, size: sp.size || (sp.role === 'impact' ? 1 : 1.15), seed: 11 });
    const full = { g: fx.g, dur: T + fx.dur + .3, lightAt: fx.lightAt, col: fx.col, f: t => fx.f(Math.max(0, t - T)) * 0, light: t => t < T ? 0 : fx.light ? fx.light(t - T) : 0 };
    full.f = t => { fx.f(t < T ? 0 : t - T); fx.g.visible = t >= T; };
    acts = [{ k: sp.cast ? 'cast' : sp.act, t: .2 }];
    // who reacts, how: the struck one for a light; everyone near for a heavy
    const R = REACT_OF[sp.el] || 'flinch', rows = sp.role === 'impact' ? [['t0', T + .02, R]] : [['near:' + (sp.role === 'above' && sp.opt === 'sky' ? 320 : 460), T + .04, sp.el === 'earth' || sp.el === 'wind' ? 'knock' : 'stagger'], ['near:460', T + .1, R]];
    if (R === 'burn') { const c = (ELEM[sp.el] || ELEM.fire).tint; rows.forEach(r => { if (r[2] === 'burn') r.push({ col: c }); }); }
    REACT.__ab = rows;
    rx = zReactions('__ab', new THREE.Vector3(at[0], 0, at[2]), spots, SPOT2.wielder, 1);
    // a bolt from the hand for a cast
    if (proj) { scene.remove(proj.g); proj = null; }
    if (sp.cast) {
      const g = new THREE.Group(), core = Volume({ el: sp.el, shape: 0, sp: [.9, 0, 0, 0], dens: 12, cut: .05 }), P = Particles({ el: sp.el, cap: 300, gain: 2.4 });
      core.scale.setScalar(16); g.add(core); g.add(P);
      for (let i = 0; i < 300; i++) { const k = i / 300, t0 = .28 + k * (T - .28); P.add1([0, 0, 0], [0, 0, 0], t0, .35, 5, 1, 0, 0); }
      proj = { g, core, P, T }; scene.add(g);
    }
    v.play(full);
  };
  const base = v.tick;
  const from = new THREE.Vector3(), to = new THREE.Vector3();
  v.tick = t => {
    base(t);
    const tt = v.fx && v.t0 != null ? (t - v.t0) % (v.fx.dur + .5) : 0;
    zs.forEach((z, i) => z && zPose(z, (rx && rx[i]) || [], tt, zinfo[i]));
    if (plr) { plrPose(plr, tt, acts, pinfo); if (plr.aura) plr.aura.tick(t, plr.auraHolder); }
    if (proj) {
      const k = (tt - .28) / (proj.T - .28); proj.g.visible = k > 0 && k < 1;
      from.copy(SPOT2.wielder).add(new THREE.Vector3(30, 120, -20)); to.set(SPOT2.t0.x, 80, SPOT2.t0.z);
      const p = from.clone().lerp(to, Math.max(0, Math.min(1, k)));
      proj.core.position.copy(p); proj.core.u.time.value = t; proj.core.u.life.value = 1; proj.P.u.time.value = tt;
      // (its trail: each particle left where the bolt was at its birth)
      const a = proj.P.geometry.attributes.aP; for (let i = 0; i < a.count; i++) { const kk = (i / a.count); const q = from.clone().lerp(to, kk); a.setXYZ(i, q.x, q.y, q.z); } a.needsUpdate = true;
    }
  };
  return v;
}
