// ================================================================ the Alchemist's flasks (Noah, Nov 27: "I dont see the flasks in the Workshop doc ... I would like to
// be able to edit their look like swirling glowing potion liquid and the element on the cork as well as their impact effects. I also
// think the explosion size for the flasks needs to be much less"): each flask is our own model (glass, liquid, cork, wrap) from the
// mod; its liquid is drawn as a glowing potion that moves inside the glass, the cork can carry its element, and its impact plays in
// action at the size you set.
const FLASKS = [
  { id: 'fire', name: 'Fire Flask', el: 'fire', what: 'Your first flask: bursts in flame.' },
  { id: 'frost', name: 'Frost Flask', el: 'frost', what: 'Bursts in frost that slows the dead (2 freed cities).' },
  { id: 'shock', name: 'Shock Flask', el: 'shock', what: 'Bursts in lightning (4 cities).' },
  { id: 'poison', name: 'Acid Flask', el: 'poison', what: 'Bursts in acid that eats the dead (6 cities).' },
  { id: 'sun', name: 'Holy Water', el: 'holy', what: 'Bursts in golden light; the dead burn in it (8 cities).' },
  { id: 'blood', name: 'Voidglass Flask', el: 'blood', what: 'What it drains comes back to you (9 cities).' },
  { id: 'philosopher', name: 'Philosopher\'s Flask', el: 'holy', what: 'Its gold mist turns the dead to statues.' },
];
const FLASK_LIQUID = [
  { id: 'calm', name: 'Calm glow', about: 'The potion glows softly from inside, slowly turning.' },
  { id: 'swirl', name: 'Swirling', about: 'Bright bands of the potion twisting round inside the glass, like a storm in a bottle.' },
  { id: 'boil', name: 'Boiling', about: 'The potion churning, bubbles of light rising through it to the neck.' },
];
const FLASK_CORK = [
  { id: 'plain', name: 'Plain cork', about: 'The cork and its cloth wrap as they are.' },
  { id: 'wisp', name: 'Element wisp', about: 'A thin wisp of its element curling up off the cork: flame, frost mist, sparks, fumes, light.' },
  { id: 'flame', name: 'Element flame', about: 'A small living flame of its element burning on the cork, like a lit fuse.' },
  { id: 'rune', name: 'Rune seal', about: 'A glowing rune of the Curse sealed on top of the cork.' },
];
const FLASK_IMPACT = [
  { id: 'shatter', name: 'Shatter and flare', role: 'impact', opt: 'core', about: 'The glass breaks and the element flares where it lands.' },
  { id: 'splash', name: 'Splash', role: 'impact', opt: 'spark', about: 'The potion splashes out of the broken glass in the way it flew.' },
  { id: 'nova', name: 'Ring', role: 'burst', opt: 'nova', about: 'A low ring of the element running out along the ground.' },
  { id: 'blast', name: 'Blast (today\'s)', role: 'burst', opt: 'big', about: 'A full explosion of the element: what the game draws now.' },
];
const LIQFS = NOISE + `uniform vec3 c0, c1, c2; uniform float time, mode, k; varying vec3 vP, vN, vV;
void main(){
  float a = atan(vP.x, vP.z), h = vP.y;
  float sp = mode < .5 ? .25 : mode < 1.5 ? 1.3 : 2.4, tw = mode < .5 ? .8 : mode < 1.5 ? 3.2 : 1.6;
  vec3 q = vec3(cos(a + time * sp + h * tw * .12), sin(a + time * sp + h * tw * .12), h * .09 - time * (mode > 1.5 ? .9 : .15));
  float n = fbm(q * (mode < 1.5 ? 2.2 : 3.4) + vec3(0., 0., time * .2)) * .5 + .5;
  float band = mode > .5 && mode < 1.5 ? smoothstep(.55, .75, n) * smoothstep(.95, .75, n) * 2. : 0.;
  float f = pow(1. - abs(dot(normalize(vN), vV)), 1.5);
  vec3 col = mix(c0 * 2.5, c1, n) + c2 * (band * .9 + f * .35);
  gl_FragColor = vec4(col * k, 1.); }`;
const LIQVS = `varying vec3 vP, vN, vV; void main(){ vP = position; vec4 w = modelMatrix * vec4(position, 1.); vN = normalize(mat3(modelMatrix) * normal); vV = normalize(cameraPosition - w.xyz); gl_Position = projectionMatrix * viewMatrix * w; }`;
function flaskModel(d, el, liquid) {   // the flask stood up (cork on top), its glass clear, its liquid our glowing potion
  const E = ELEM[el] || ELEM.fire, g = standUp(d, 60), parts = {}, u = { c0: { value: v3c(E.c[0]) }, c1: { value: v3c(E.c[1]) }, c2: { value: v3c(E.c[2]) }, time: { value: 0 }, mode: { value: FLASK_LIQUID.findIndex(x => x.id === liquid) }, k: { value: 1 } };
  g.meshes.forEach((m, i) => {
    const s = d.mesh[i].s, n = (s.name || '').split(':')[0].toLowerCase(); parts[n] = m;
    if (s.glow) m.visible = false;   // (the game's flat glow cards: the preview draws its own light)
    else if (n === 'glass') m.material = new THREE.MeshStandardMaterial({ color: 0xdfeaf0, roughness: .08, metalness: .1, transparent: true, opacity: .22, depthWrite: false, side: THREE.DoubleSide });
    else if (n === 'liquid') { m.material = new THREE.ShaderMaterial({ uniforms: u, vertexShader: LIQVS, fragmentShader: LIQFS }); m.layers.enable(1); }
  });
  g.updateMatrixWorld(true);
  const box = o => new THREE.Box3().setFromObject(o);
  if (parts.cork && parts.liquid && box(parts.cork).getCenter(new THREE.Vector3()).y < box(parts.liquid).getCenter(new THREE.Vector3()).y) g.rotation.x = Math.PI;
  g.updateMatrixWorld(true);
  return { g, u, parts, cork: parts.cork ? box(parts.cork) : null, liq: parts.liquid ? box(parts.liquid) : null };
}
function flaskView(cv, f) {
  const v = fxStage(cv, { az: .4, el: .18, r: 112, ty: 32 });
  const E = ELEM[f.el] || ELEM.fire, scene = v.scene, hold = new THREE.Group(); hold.position.y = 30; scene.add(hold);
  let fm = null, extra = [], want = { liquid: 'swirl', cork: 'wisp' };
  loadWeapon('flask_' + f.id).then(d => { fm = flaskModel(d, f.el, want.liquid); hold.add(fm.g); build(); }).catch(e => console.warn('flask', f.id, e));
  function build() {
    if (!fm) return;
    for (const x of extra) x.g.parent && x.g.parent.remove(x.g); extra = [];
    fm.u.mode.value = FLASK_LIQUID.findIndex(x => x.id === want.liquid);
    const top = fm.cork ? new THREE.Vector3((fm.cork.min.x + fm.cork.max.x) / 2, fm.cork.max.y, (fm.cork.min.z + fm.cork.max.z) / 2) : new THREE.Vector3(0, 30, 0);
    const c = [E.c[0], E.c[1], E.c[2]];
    if (want.liquid === 'boil' && fm.liq) {   // bubbles of light rising through the potion
      const L = fm.liq, P = Particles({ el: f.el, cap: 130, loop: true, c, gain: 2.4, turb: 1.2, psz: 2.2 });
      for (let i = 0; i < 120; i++) { const x = L.min.x + (L.max.x - L.min.x) * (.25 + .5 * Math.random()), z = L.min.z + (L.max.z - L.min.z) * (.25 + .5 * Math.random());
        P.add1([x, L.min.y + 2, z], [0, (L.max.y - L.min.y) * .55, 0], Math.random() * 3, 1.6, .9 + Math.random() * .8, .4, 0, 0); }
      P.renderOrder = 7; hold.add(P); extra.push({ g: P, f: t => P.u.time.value = t });
    }
    if (want.cork === 'wisp') {
      const P = Particles({ el: f.el, cap: 120, loop: true, c, gain: 1.3, turb: 3, psz: 2 });
      for (let i = 0; i < 110; i++) P.add1([top.x + (Math.random() - .5) * 1.5, top.y + .5, top.z + (Math.random() - .5) * 1.5], [(Math.random() - .5) * 2.5, 5 + Math.random() * 4, (Math.random() - .5) * 2.5], Math.random() * 3, 1.1 + Math.random() * .8, .9, 2.2, .6, 0);
      hold.add(P); extra.push({ g: P, f: t => P.u.time.value = t });
    } else if (want.cork === 'flame') {
      const x = Volume({ el: f.el, shape: 0, sp: [.9, 0, 0, 0], dens: 6, cut: .2, rise: 2.2, gain: .9 }); x.position.set(top.x, top.y + 2.5, top.z); x.scale.set(2.6, 4.5, 2.6);
      const P = Particles({ el: f.el, cap: 60, loop: true, c, gain: 2.4, turb: 3, psz: 2 });
      for (let i = 0; i < 50; i++) P.add1([top.x, top.y + 5, top.z], [(Math.random() - .5) * 6, 16 + Math.random() * 12, (Math.random() - .5) * 6], Math.random() * 2, .7, .9, .2, .5, 1);
      hold.add(x); hold.add(P); extra.push({ g: x, f: t => { x.u.time.value = t; x.u.life.value = .85 + .15 * Math.sin(t * 9); } }, { g: P, f: t => P.u.time.value = t });
    } else if (want.cork === 'rune') {
      const d = RuneDisc(f.el, 3.6); d.position.set(top.x, top.y + .3, top.z); hold.add(d); extra.push({ g: d, f: t => { d.u.time.value = t * .5; d.u.on.value = .9; } });
    }
  }
  v.set = (liquid, cork) => { want = { liquid, cork }; build(); };
  v.play({ g: new THREE.Group(), dur: 6, lightAt: new THREE.Vector3(0, 40, 30), col: E.light, f: () => {}, light: () => .6 });
  const base = v.tick;
  v.tick = t => { base(t); if (fm) { fm.u.time.value = t; fm.u.k.value = 1 + .12 * Math.sin(t * 2.3); } for (const x of extra) x.f(t); };
  return v;
}
// the throw and its impact (the Alchemist throws it underhand; it bursts where it lands) at the size you set
function flaskScript(f, impact, size) {
  const I = FLASK_IMPACT.find(x => x.id === impact) || FLASK_IMPACT[0];
  const hit = { role: I.role, opt: I.opt, size };
  return [{ k: 'act', a: 'throw', t: .2 }, { k: 'bolt', t: .3, to: 't0', d: .45, curve: true, hit, from: 'hand', size: .35, model: 'flask_' + f.id, len: 22, el: f.el },
    { k: 'react', t: .78, who: 'near:' + Math.round(80 + 200 * size) + '@t0', r: 'elem', el: f.el },
    { k: 'act', a: 'throw', t: 1.3 }, { k: 'bolt', t: 1.4, to: 't4', d: .45, curve: true, hit, from: 'hand', size: .35, model: 'flask_' + f.id, len: 22, el: f.el },
    { k: 'react', t: 1.88, who: 'near:' + Math.round(80 + 200 * size) + '@t4', r: 'elem', el: f.el }];
}
