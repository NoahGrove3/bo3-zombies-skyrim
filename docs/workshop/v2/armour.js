// ================================================================ the armour sets' looks (Noah, Nov 27: "the armor effects should also be in the doc for me to
// choose from like the weapon looks"): you in your class's set, and the aura the set wraps you in as it climbs the tiers - made on
// the set itself as the weapon looks are on their models: the set's own surface glowing through a skin of light laid on it, every
// particle born on a real point of it, and the light round you a 3D volume. Five looks per class, in the class's colour; the
// tiers make the picked one stronger (I faint, II more, III full, Legendary brightest or with a second look over it).
const ARMOUR_LOOKS = [
  { id: 'sheen', name: 'Sheen', desc: 'The set itself glows from inside: light flowing slowly over the cloth and steel, brightest at its edges. Nothing leaves it.' },
  { id: 'wisps', name: 'Rising wisps', desc: 'Soft wisps of your colour lifting off the set\'s shoulders and back, like heat off a forge, fading as they climb.' },
  { id: 'embers', name: 'Embers', desc: 'Small bright embers shed off the set as you move, drifting up and winking out; a faint sheen under them.' },
  { id: 'runes', name: 'Rune circle', desc: 'A turning circle of the Curse\'s runes on the ground under your feet, motes rising from it round you.' },
  { id: 'mantle', name: 'Mantle', desc: 'A column of soft light standing round you, swirling slowly from your feet to over your head: the strongest, most visible look.' },
];
const ARMOUR_TIERS = [['t1', 'Tier I', .45], ['t2', 'Tier II', .7], ['t3', 'Tier III', 1], ['t4', 'Legendary', 1.35]];
function skinMat(c1, c2) {   // a skin of light laid just over the set, skinned with it: fresnel-bright at the edges, flowing
  const m = new THREE.MeshPhongMaterial({ skinning: true, transparent: true, depthWrite: false, side: THREE.FrontSide });
  Object.assign(m, ADD());
  m.userData.u = { uC1: { value: v3c(c1) }, uC2: { value: v3c(c2) }, uK: { value: 1 }, uT: { value: 0 }, uPush: { value: .6 } };
  m.onBeforeCompile = sh => {
    Object.assign(sh.uniforms, m.userData.u);
    sh.vertexShader = 'uniform float uPush; varying vec3 vOP;\n' + sh.vertexShader.replace('#include <begin_vertex>', '#include <begin_vertex>\n transformed += normalize(objectNormal) * uPush; vOP = position;');
    sh.fragmentShader = 'uniform vec3 uC1, uC2; uniform float uK, uT; varying vec3 vOP;\n' + sh.fragmentShader.replace('gl_FragColor = vec4( outgoingLight, diffuseColor.a );',
      'float fr = pow(1. - abs(dot(normalize(vNormal), normalize(vViewPosition))), 1.7); float n = .5 + .5 * sin(vOP.z * .22 - uT * 2.1 + sin(vOP.x * .3 + uT) * 1.4) * sin(vOP.y * .27 + uT * .7);' +
      ' vec3 col = mix(uC1, uC2, n * fr) * (fr * fr * 1.4 + n * fr * .35); gl_FragColor = vec4(col * uK, 0.);');
  };
  return m;
}
function armourView(cv, cls, el) {
  const v = fxStage(cv, { az: .5, el: .12, r: 270, ty: 92 });
  const E = ELEM[el] || ELEM.arcane, scene = v.scene;
  let z = null, skins = [], pts = [], layers = [], want = null;
  const pinfo = { home: new THREE.Vector3(0, 0, 0), face: 0 };
  zomData(cls, 'plr').then(([sk, body]) => {
    z = makeZombie(sk, body, .5); scene.add(z.holder); plrPose(z, 0, [], pinfo); z.holder.updateMatrixWorld(true);
    for (const mesh of z.inner.children.filter(o => o.isSkinnedMesh)) {   // the skin of light, on each piece, sharing its bones
      const m = skinMat(E.c[1], E.c[2]), s = new THREE.SkinnedMesh(mesh.geometry, m); s.frustumCulled = false; s.renderOrder = 3; s.layers.enable(1);
      s.bind(mesh.skeleton, mesh.bindMatrix); mesh.parent.add(s); skins.push(s);
      // points on the set itself (where it stands posed), each with the way out from the body
      const P = mesh.geometry.attributes.position, step = Math.max(1, Math.floor(P.count / 900)), q = new THREE.Vector3();
      for (let i = 0; i < P.count; i += step) { q.fromBufferAttribute(P, i); mesh.boneTransform(i, q); q.applyMatrix4(mesh.matrixWorld); pts.push(q.clone()); }
    }
    if (want) build(...want);
  }).catch(e => console.warn('armour', cls, e));
  const clear = () => { for (const l of layers) l.g.parent && l.g.parent.remove(l.g); layers = []; };
  function layer(look, k) {   // one look at strength k: what it adds over the skin
    const g = new THREE.Group(), ticks = [];
    const c = [E.c[0], E.c[1], E.c[2]];
    const outOf = p => new THREE.Vector3(p.x, 0, p.z).normalize();
    if (look === 'wisps' || look === 'embers') {
      const wisp = look === 'wisps', src = pts.filter(p => p.y > (wisp ? 95 : 30));
      const n = Math.round((wisp ? 260 : 340) * Math.min(1.5, k)), P = Particles({ el, cap: n + 10, loop: true, c, gain: 2.1, turb: wisp ? 14 : 6, psz: wisp ? 3.4 : 2.6 });
      for (let i = 0; i < n && src.length; i++) { const p = src[(i * 7919) % src.length], o = outOf(p);
        P.add1([p.x + o.x, p.y, p.z + o.z], wisp ? [o.x * 5, 22 + Math.random() * 18, o.z * 5] : [o.x * 14, 35 + Math.random() * 40, o.z * 14], Math.random() * 3, wisp ? 1.4 + Math.random() : .6 + Math.random() * .6,
          (wisp ? 5 : 1.6) * (.6 + Math.random() * .8) * (.7 + .3 * k), wisp ? 1.5 : .3, wisp ? 1.2 : .6, wisp ? 0 : 1); }
      P.renderOrder = 6; g.add(P); ticks.push(t => P.u.time.value = t);
    } else if (look === 'runes') {
      const d = RuneDisc(el, 62); d.position.set(0, 1.5, 0); g.add(d);
      const n = Math.round(200 * k), P = Particles({ el, cap: n + 10, loop: true, c, gain: 2.2, turb: 8, psz: 2.6 });
      for (let i = 0; i < n; i++) { const a = Math.random() * 6.283, r = 40 + Math.random() * 20; P.add1([Math.cos(a) * r, 3, Math.sin(a) * r], [-Math.sin(a) * 18, 40 + Math.random() * 40, Math.cos(a) * 18], Math.random() * 3, 1.2 + Math.random(), 2.2, .4, .8, 0); }
      g.add(P); ticks.push(t => { d.u.time.value = t * .6; d.u.on.value = Math.min(1, .35 + .5 * k); P.u.time.value = t; });
    } else if (look === 'mantle') {
      const x = Volume({ el, shape: 1, sp: [.42, .95, 0, 0], dens: 1.6 * k, cut: .35, swirl: 1.2, rise: 1.4, gain: .5 }); x.position.set(0, 95, 0); x.scale.set(62, 105, 62);
      x.u.c0.value = v3c(c[0]); x.u.c1.value = v3c(c[1]); x.u.c2.value = v3c(c[2]); g.add(x);
      ticks.push(t => { x.u.time.value = t; x.u.life.value = Math.min(1, .5 + .4 * k); });
    }
    scene.add(g); layers.push({ g, tick: ticks });
  }
  function build(look, k, extra) {
    if (!z) return; clear();
    const sk = look === 'sheen' ? 1 : look === 'mantle' || look === 'runes' ? .35 : .55;   // (the skin is the sheen; the others keep a faint one under them)
    skins.forEach(s => s.material.userData.u.uK.value = .3 * sk * k);
    layer(look, k);
    if (extra) { layer(extra, k * .8); if (extra === 'sheen') skins.forEach(s => s.material.userData.u.uK.value = .3 * k); }
  }
  v.show = (look, k, extra) => { want = [look, k, extra]; build(look, k, extra); };
  v.play({ g: new THREE.Group(), dur: 6, lightAt: new THREE.Vector3(0, 110, 60), col: E.light, f: () => {}, light: () => .5 });
  const base = v.tick;
  v.tick = t => { base(t); if (!z) return; plrPose(z, t, [], pinfo); skins.forEach(s => s.material.userData.u.uT.value = t); for (const l of layers) for (const f of l.tick) f(t); };
  return v;
}
