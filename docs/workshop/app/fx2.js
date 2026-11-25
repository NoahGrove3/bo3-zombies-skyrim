// ================================================================ the ability effects' foundation (Noah, 2026-11-24: "hyper-realistic ... incredibly high detail,
// 30 FPS, smooth and satisfying"): GPU particles - every particle's whole life worked out on the graphics card from its start, so
// thousands of them cost nothing - flipbook fire, sparks stretched along their flight, lit smoke drawn over (not added), and a light
// that each effect casts on the ground and the dead round it
const PVS = `
attribute vec3 aP0; attribute vec3 aV; attribute vec4 aT; attribute vec4 aR;
uniform float time; uniform vec3 grav; uniform float stretch; uniform float turb; uniform vec2 cellN; uniform float fps;
varying vec2 vUv; varying float vK; varying float vSeed; varying vec2 vCell;
void main(){
  float age = time - aT.x, life = aT.y; vK = age / life; vSeed = aR.x;
  if (age < 0.0 || age > life) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
  float drag = max(aR.z, 0.0001);
  float dd = (1.0 - exp(-drag * age)) / drag;
  vec3 p = aP0 + aV * dd + 0.5 * grav * age * age;
  p += turb * vec3(sin(age * 2.3 + aR.x * 17.0), sin(age * 1.7 + aR.x * 29.0) * 0.6, cos(age * 2.1 + aR.x * 11.0)) * age;
  vec3 vel = aV * exp(-drag * age) + grav * age;
  float size = mix(aT.z, aT.w, vK);
  vec4 mv = modelViewMatrix * vec4(p, 1.0);
  vec2 corner = position.xy;
  float spin = aR.y * age + aR.x * 6.28;
  vec2 rc = vec2(corner.x * cos(spin) - corner.y * sin(spin), corner.x * sin(spin) + corner.y * cos(spin));
  if (stretch > 0.0) {   // (a spark: drawn out along its flight on screen)
    vec3 vv = (modelViewMatrix * vec4(vel, 0.0)).xyz;
    vec2 d = vv.xy; float l = length(d);
    vec2 dir = l > 0.001 ? d / l : vec2(0.0, 1.0); vec2 nrm = vec2(-dir.y, dir.x);
    mv.xy += dir * corner.y * (size + l * stretch) + nrm * corner.x * size * 0.35;
  } else mv.xy += rc * size;
  vUv = position.xy + 0.5;
  float c = floor(age * fps + aR.x * 16.0); vCell = vec2(mod(c, cellN.x), cellN.y - 1.0 - mod(floor(c / cellN.x), cellN.y));
  gl_Position = projectionMatrix * mv;
}`;
const PFS = `
uniform sampler2D tex, pal; uniform float gain; uniform vec2 cellN; uniform vec4 fade; uniform float heat0, heat1, smoke;
varying vec2 vUv; varying float vK; varying float vSeed; varying vec2 vCell;
void main(){
  vec2 uv = cellN.x > 1.0 ? (vUv + vCell) / cellN : vUv;
  vec4 t = texture2D(tex, uv);
  float a = smoothstep(0.0, fade.x, vK) * (1.0 - smoothstep(fade.y, 1.0, vK));
  float h = mix(heat0, heat1, vK) * t.r;
  vec4 p = texture2D(pal, vec2(clamp(h, 0.004, 0.996), 0.9));
  if (smoke > 0.5) { vec3 c = mix(vec3(0.05, 0.05, 0.06), vec3(0.34, 0.33, 0.32), t.r) + p.rgb * 0.35 * (1.0 - vK) * fade.z; gl_FragColor = vec4(c, t.a * a * fade.w); }
  else gl_FragColor = vec4(p.rgb * gain, t.a * a);
}`;
const QUAD = (() => { const g = new THREE.InstancedBufferGeometry(); g.setAttribute('position', new THREE.Float32BufferAttribute([-0.5, -0.5, 0, 0.5, -0.5, 0, 0.5, 0.5, 0, -0.5, 0.5, 0], 3)); g.setIndex([0, 1, 2, 0, 2, 3]); return g; })();
// a particle layer: up to `cap` particles of one kind; add() them at build time, set the time every frame
function Particles(fx, kind, cap, o = {}) {
  const g = new THREE.InstancedBufferGeometry(); g.index = QUAD.index; g.setAttribute('position', QUAD.attributes.position);
  const P0 = new Float32Array(cap * 3), V = new Float32Array(cap * 3), T = new Float32Array(cap * 4), R = new Float32Array(cap * 4);
  for (let i = 0; i < cap; i++) T[i * 4] = -1e9;
  g.setAttribute('aP0', new THREE.InstancedBufferAttribute(P0, 3)); g.setAttribute('aV', new THREE.InstancedBufferAttribute(V, 3));
  g.setAttribute('aT', new THREE.InstancedBufferAttribute(T, 4)); g.setAttribute('aR', new THREE.InstancedBufferAttribute(R, 4));
  g.instanceCount = cap;
  const K = { fire: ['t_spiritflame', [4, 4], 24], spark: ['t_glow', [1, 1], 0], glow: ['t_glow', [1, 1], 0], smoke: ['t_puff', [1, 1], 0], mote: ['t_glow', [1, 1], 0], shard: ['t_glow', [1, 1], 0] }[kind];
  const smoke = kind === 'smoke';
  const u = { time: { value: 0 }, grav: { value: new THREE.Vector3(...(o.grav || [0, 0, 0])) }, stretch: { value: o.stretch || 0 }, turb: { value: o.turb || 0 },
    cellN: { value: new THREE.Vector2(...K[1]) }, fps: { value: K[2] || 1 }, tex: { value: tex(K[0]) }, pal: { value: pal(fx.col) }, gain: { value: o.gain || 1.5 },
    fade: { value: new THREE.Vector4(...(o.fade || [0.08, 0.6, 1, 0.75])) }, heat0: { value: o.heat0 == null ? 1 : o.heat0 }, heat1: { value: o.heat1 == null ? 0.4 : o.heat1 }, smoke: { value: smoke ? 1 : 0 } };
  const m = new THREE.ShaderMaterial({ uniforms: u, vertexShader: PVS, fragmentShader: PFS, transparent: true, depthWrite: false,
    blending: smoke ? THREE.NormalBlending : THREE.AdditiveBlending });
  const mesh = new THREE.Mesh(g, m); mesh.frustumCulled = false; mesh.renderOrder = smoke ? 1 : 3;
  if (!smoke) mesh.layers.enable(1);
  fx.g.add(mesh);
  let n = 0;
  return {
    mesh, u,
    add(p, v, birth, life, s0, s1, o2 = {}) {
      if (n >= cap) return; const i = n++;
      P0.set([p.x, p.y, p.z], i * 3); V.set([v.x, v.y, v.z], i * 3); T.set([birth, life, s0, s1], i * 4); R.set([Math.random(), o2.spin || 0, o2.drag == null ? 1.5 : o2.drag, 0], i * 4);
      g.attributes.aP0.needsUpdate = g.attributes.aV.needsUpdate = g.attributes.aT.needsUpdate = g.attributes.aR.needsUpdate = true;
    },
    set(t) { u.time.value = t; },
  };
}
const V3 = (x, y, z) => new THREE.Vector3(x, y, z);
function sph(r) { const z = r() * 2 - 1, a = r() * Math.PI * 2, q = Math.sqrt(1 - z * z); return V3(Math.cos(a) * q, z, Math.sin(a) * q); }
function hemi(r, up = 0.3) { const v = sph(r); v.y = Math.abs(v.y) * (1 - up) + up; return v.normalize(); }
// the building blocks every effect is made of, each a few hundred particles
const P = {
  explosion(fx, r, at, s, t0 = 0, k = 1) {   // a fireball that blooms, rolls up and is left as smoke; sparks flung out; embers falling
    const fire = Particles(fx, 'fire', 70, { grav: [0, 160 * s, 0], turb: 12 * s, gain: 1.7, fade: [0.05, 0.45, 1, 1], heat0: 1.15, heat1: 0.45 });
    const sparks = Particles(fx, 'spark', 140, { grav: [0, -520, 0], stretch: 0.055, gain: 2.6, fade: [0.01, 0.55, 1, 1], heat0: 1.1, heat1: 0.6 });
    const smoke = Particles(fx, 'smoke', 40, { grav: [0, 90 * s, 0], turb: 14 * s, fade: [0.12, 0.55, 1, 0.75] });
    const flash = Particles(fx, 'glow', 4, { gain: 3.2, fade: [0.02, 0.15, 1, 1], heat0: 1.2, heat1: 1 });
    for (let i = 0; i < 70 * k; i++) { const d = sph(r); fire.add(at.clone().addScaledVector(d, 10 * s * r()), d.multiplyScalar((180 + 260 * r()) * s), t0 + r() * 0.08, 0.55 + 0.5 * r(), 26 * s, (70 + 50 * r()) * s, { drag: 4.5, spin: (r() - 0.5) * 2 }); }
    for (let i = 0; i < 140 * k; i++) { const d = hemi(r, 0.15); sparks.add(at.clone(), d.multiplyScalar((350 + 700 * r()) * s), t0 + r() * 0.12, 0.45 + 0.9 * r(), 2.2 * s, 1.0 * s, { drag: 1.6 }); }
    for (let i = 0; i < 40 * k; i++) { const d = sph(r); smoke.add(at.clone().addScaledVector(d, 25 * s), d.multiplyScalar(80 * s * r()), t0 + 0.15 + r() * 0.35, 1.4 + 1.2 * r(), 40 * s, (130 + 60 * r()) * s, { drag: 1.2, spin: (r() - 0.5) * 0.8 }); }
    flash.add(at.clone(), V3(0, 0, 0), t0, 0.35, 60 * s, 260 * s); flash.add(at.clone(), V3(0, 0, 0), t0, 0.18, 30 * s, 120 * s);
    return [fire, sparks, smoke, flash];
  },
  dust(fx, r, at, R, s, t0, n = 60) {   // a ring of dust kicked up off the ground
    const d = Particles(fx, 'smoke', n, { grav: [0, 20, 0], turb: 8, fade: [0.1, 0.4, 0.3, 0.45] });
    for (let i = 0; i < n; i++) { const a = r() * Math.PI * 2; const p = at.clone().add(V3(Math.cos(a) * R * 0.2, 8, Math.sin(a) * R * 0.2));
      d.add(p, V3(Math.cos(a), 0.15 + 0.2 * r(), Math.sin(a)).multiplyScalar(R * (1.6 + 0.8 * r())), t0 + r() * 0.1, 0.9 + 0.6 * r(), 30 * s, 110 * s, { drag: 3.2, spin: (r() - 0.5) }); }
    return d;
  },
  embers(fx, r, at, n, spread, s, t0, life = 1.6, up = 120) {   // slow motes of the element drifting up
    const e = Particles(fx, 'mote', n, { grav: [0, up, 0], turb: 30, gain: 2.4, fade: [0.15, 0.6, 1, 1], heat0: 1, heat1: 0.6 });
    for (let i = 0; i < n; i++) { const p = at.clone().add(V3((r() - 0.5) * spread, r() * spread * 0.4, (r() - 0.5) * spread)); e.add(p, sph(r).multiplyScalar(40 * s), t0 + r() * life * 0.8, life * (0.6 + 0.6 * r()), 4 * s, 1.5 * s, { drag: 1 }); }
    return e;
  },
};
function setAll(list, t) { for (const p of list) p.set(t); }

// the styles rebuilt on the particles (the rings, beams and bolts of the first pass kept, made finer)
const STYLE2 = {
  burst(fx, s, at, r) {
    const c = at.clone().setY(70), ps = P.explosion(fx, r, c, s, 0.1);
    return { dur: 2.4, light: t => 6 * Math.exp(-Math.max(0, t - 0.1) * 6) * (t > 0.1 ? 1 : 0), lightAt: c, f: t => setAll(ps, t) };
  },
  shockwave(fx, s, at, r) {
    const ring = ringPart(fx, s, 300), ps = [P.dust(fx, r, at, 300 * s, s, 0.05, 80), ...P.explosion(fx, r, at.clone().setY(25), s * 0.55, 0.0, 0.5)];
    const sparks = Particles(fx, 'spark', 160, { grav: [0, -300, 0], stretch: 0.05, gain: 2.4, fade: [0.01, 0.5, 1, 1] });
    for (let i = 0; i < 160; i++) { const a = r() * Math.PI * 2; sparks.add(at.clone().add(V3(0, 6, 0)), V3(Math.cos(a), 0.12 + 0.25 * r(), Math.sin(a)).multiplyScalar((420 + 260 * r()) * s), 0.04 + r() * 0.05, 0.5 + 0.4 * r(), 2 * s, 1 * s, { drag: 2.6 }); }
    ps.push(sparks);
    return { dur: 2.0, light: t => 5 * Math.exp(-t * 5), lightAt: at.clone().setY(40), f: t => { ring(t - 0.05, at); setAll(ps, t); } };
  },
  nova(fx, s, at, r) {
    const ring = ringPart(fx, s, 280, { gain: 2 }), ps = [P.dust(fx, r, at, 260 * s, s, 0.05, 60)];
    const shards = Particles(fx, 'shard', 120, { grav: [0, -380, 0], stretch: 0.09, gain: 2.4, fade: [0.02, 0.6, 1, 1], heat0: 1, heat1: 0.7 });
    for (let i = 0; i < 120; i++) { const a = r() * Math.PI * 2, rr = 280 * s * (0.3 + 0.7 * r()); shards.add(at.clone().add(V3(Math.cos(a) * rr, 4, Math.sin(a) * rr)), V3(Math.cos(a) * 60, 380 + 420 * r(), Math.sin(a) * 60).multiplyScalar(s), 0.1 + 0.5 * (rr / (280 * s)) * 0.6, 0.7 + 0.4 * r(), 3 * s, 1.5 * s, { drag: 1.8 }); }
    const mist = Particles(fx, 'smoke', 50, { grav: [0, 12, 0], turb: 10, fade: [0.12, 0.5, 1, 0.5] });
    for (let i = 0; i < 50; i++) { const a = r() * Math.PI * 2, rr = 260 * s * (0.4 + 0.6 * r()); mist.add(at.clone().add(V3(Math.cos(a) * rr, 20, Math.sin(a) * rr)), V3(Math.cos(a) * 40, 10, Math.sin(a) * 40), 0.2 + r() * 0.4, 1.5 + r(), 60 * s, 150 * s, { drag: 1, spin: r() - 0.5 }); }
    ps.push(shards, mist, P.embers(fx, r, at, 40, 500 * s, s, 0.2, 1.8, 60));
    return { dur: 2.6, light: t => 4 * Math.exp(-t * 3.5), lightAt: at.clone().setY(50), f: t => { ring(t, at); setAll(ps, t); } };
  },
  pillar(fx, s, at, r) {
    const base = STYLE_FN.pillar(fx, s, at, r);
    const sparks = Particles(fx, 'spark', 200, { grav: [0, -500, 0], stretch: 0.05, gain: 2.6, fade: [0.01, 0.5, 1, 1] });
    for (let i = 0; i < 200; i++) { const a = r() * Math.PI * 2; sparks.add(at.clone().add(V3(0, 10, 0)), V3(Math.cos(a), 0.4 + r(), Math.sin(a)).multiplyScalar((250 + 500 * r()) * s), 0.12 + r() * 0.5, 0.5 + 0.6 * r(), 2.2 * s, 1 * s, { drag: 1.8 }); }
    const motes = Particles(fx, 'mote', 90, { grav: [0, 260, 0], turb: 20, gain: 2.4, fade: [0.1, 0.6, 1, 1] });
    for (let i = 0; i < 90; i++) { const a = r() * Math.PI * 2, rr = 40 * s * r(); motes.add(at.clone().add(V3(Math.cos(a) * rr, 30 * r(), Math.sin(a) * rr)), V3(0, 200 + 300 * r(), 0), 0.12 + r() * 0.8, 1 + r(), 5 * s, 2 * s, { drag: 0.6 }); }
    const ps = [sparks, motes, P.dust(fx, r, at, 200 * s, s, 0.12, 50)];
    return { dur: 2.4, light: t => t < 0.12 ? 0 : 6 * Math.max(0, Math.min(1, (1.2 - t) / 0.5)), lightAt: at.clone().setY(80), f: t => { base.f(t); setAll(ps, t); } };
  },
  strike(fx, s, at, r) {
    const base = STYLE_FN.strike(fx, s, at, r), ps = P.explosion(fx, r, at.clone().setY(40), s * 0.7, 0.02, 0.6);
    return { dur: 1.8, light: t => ([[0, 0.07], [0.12, 0.19], [0.26, 0.36]].some(([a, b]) => t >= a && t < b) ? 9 : 2 * Math.exp(-t * 4)), lightAt: at.clone().setY(150), f: t => { base.f(t); setAll(ps, t); } };
  },
  chain(fx, s, at, r) {
    const base = STYLE_FN.chain(fx, s, at, r);
    const nodes = [at.clone().setY(80), ...fx.targets.slice(1, 3).map(p => p.clone().setY(80))];
    const ps = nodes.flatMap((n, i) => { const sp = Particles(fx, 'spark', 60, { grav: [0, -400, 0], stretch: 0.05, gain: 2.6, fade: [0.01, 0.5, 1, 1] });
      for (let j = 0; j < 60; j++) sp.add(n.clone(), sph(r).multiplyScalar((200 + 400 * r()) * s), i * 0.14 + r() * 0.3, 0.3 + 0.4 * r(), 1.8 * s, 0.8 * s, { drag: 2 }); return [sp]; });
    return { dur: 1.9, light: t => (((t * 20) | 0) % 3 !== 0 && t < 0.8) ? 5 : 0, lightAt: at.clone().setY(100), f: t => { base.f(t); setAll(ps, t); } };
  },
  bolt(fx, s, at, r) {
    const to = at.clone().setY(70), from = fx.wielderHand, dur = 0.55;
    const trail = Particles(fx, 'fire', 120, { grav: [0, 60, 0], turb: 10, gain: 1.5, fade: [0.05, 0.4, 1, 1], heat0: 1.1, heat1: 0.4 });
    const head = Particles(fx, 'glow', 40, { gain: 2.8, fade: [0.0, 0.7, 1, 1], heat0: 1.2, heat1: 1 });
    for (let i = 0; i < 120; i++) { const k = i / 120, p = from.clone().lerp(to, k); p.y += Math.sin(Math.PI * k) * 30; trail.add(p, sph(r).multiplyScalar(30 * s), k * dur, 0.35 + 0.25 * r(), 22 * s, 6 * s, { drag: 3, spin: r() - 0.5 }); }
    for (let i = 0; i < 40; i++) { const k = i / 40, p = from.clone().lerp(to, k); p.y += Math.sin(Math.PI * k) * 30; head.add(p, V3(0, 0, 0), k * dur, 0.06, 46 * s, 40 * s, {}); }
    const ps = [trail, head, ...P.explosion(fx, r, to, s * 0.75, dur, 0.7)];
    return { dur: 2.2, light: t => t < dur ? 2.5 : 5 * Math.exp(-(t - dur) * 6), lightAt: to, lightFollow: t => t < dur ? from.clone().lerp(to, t / dur) : to, f: t => setAll(ps, t) };
  },
  meteor(fx, s, at, r) {
    const ps = [], drops = [0, 1, 2].map(i => ({ to: i === 0 ? at.clone().setY(20) : fx.targets[i].clone().setY(20), t0: i * 0.45 }));
    const rings = drops.map(() => ringPart(fx, s * 0.8, 280, { dur: 0.6 }));
    for (const d of drops) {
      const from = d.to.clone().add(V3(-500, 1100, -260)), fd = 0.5;
      const trail = Particles(fx, 'fire', 110, { grav: [0, 80, 0], turb: 14, gain: 1.6, fade: [0.03, 0.35, 1, 1], heat0: 1.15, heat1: 0.45 });
      const smoke = Particles(fx, 'smoke', 60, { grav: [0, 30, 0], turb: 10, fade: [0.1, 0.5, 1, 0.6] });
      for (let i = 0; i < 110; i++) { const k = i / 110, p = from.clone().lerp(d.to, k); trail.add(p, sph(r).multiplyScalar(40 * s), d.t0 + k * fd, 0.35 + 0.3 * r(), 50 * s, 12 * s, { drag: 3, spin: r() - 0.5 }); }
      for (let i = 0; i < 60; i++) { const k = i / 60, p = from.clone().lerp(d.to, k); smoke.add(p, sph(r).multiplyScalar(20 * s), d.t0 + k * fd + 0.05, 1.2 + r(), 40 * s, 120 * s, { drag: 1.5, spin: r() - 0.5 }); }
      ps.push(trail, smoke, ...P.explosion(fx, r, d.to.clone().setY(30), s * 1.3, d.t0 + fd, 1), P.dust(fx, r, d.to, 260 * s, s, d.t0 + fd, 50));
    }
    return { dur: 3.4, light: t => drops.reduce((m, d) => Math.max(m, t > d.t0 + 0.5 ? 7 * Math.exp(-(t - d.t0 - 0.5) * 5) : (t > d.t0 ? 2 : 0)), 0), lightAt: at.clone().setY(60),
      f: t => { setAll(ps, t); drops.forEach((d, i) => rings[i](t - d.t0 - 0.5, d.to)); } };
  },
  cyclone(fx, s, at, r) {
    const base = STYLE_FN.cyclone(fx, s, at, r);
    const deb = Particles(fx, 'smoke', 90, { grav: [0, 70, 0], turb: 40, fade: [0.1, 0.6, 0.6, 0.55] });
    for (let i = 0; i < 90; i++) { const a = r() * Math.PI * 2, rr = 120 * s * (0.5 + r()); deb.add(at.clone().add(V3(Math.cos(a) * rr, 10, Math.sin(a) * rr)), V3(-Math.sin(a), 0.6, Math.cos(a)).multiplyScalar(300 * s), r() * 2.4, 1.2 + r(), 30 * s, 90 * s, { drag: 1.2, spin: 2 }); }
    const sparks = Particles(fx, 'spark', 160, { grav: [0, 200, 0], stretch: 0.04, turb: 60, gain: 2.4, fade: [0.05, 0.6, 1, 1] });
    for (let i = 0; i < 160; i++) { const a = r() * Math.PI * 2, rr = 80 * s * r(); sparks.add(at.clone().add(V3(Math.cos(a) * rr, 20 + 200 * r() * s, Math.sin(a) * rr)), V3(-Math.sin(a), 0.4, Math.cos(a)).multiplyScalar(420 * s), r() * 2.6, 0.5 + 0.5 * r(), 2 * s, 1 * s, { drag: 0.8 }); }
    const ps = [deb, sparks];
    return { dur: 3.4, light: t => 3.5 * fade(t, 0, 0.4, 2.5, 3.1) * (0.85 + 0.15 * Math.sin(t * 17)), lightAt: at.clone().setY(120), f: t => { base.f(t); setAll(ps, t); } };
  },
  cloud(fx, s, at, r) {
    const thick = Particles(fx, 'smoke', 70, { grav: [0, 8, 0], turb: 12, fade: [0.15, 0.6, 1.4, 0.8] });
    const glow = Particles(fx, 'glow', 40, { grav: [0, 5, 0], turb: 10, gain: 0.9, fade: [0.2, 0.6, 1, 1], heat0: 0.8, heat1: 0.5 });
    for (let i = 0; i < 70; i++) { const d = sph(r); d.y = Math.abs(d.y) * 0.5; thick.add(at.clone().addScaledVector(d, 130 * s * r()).setY(30 + 60 * r() * s), d.clone().multiplyScalar(40 * s), r() * 0.8, 2.0 + r(), 60 * s, 170 * s, { drag: 0.8, spin: (r() - 0.5) * 0.4 }); }
    for (let i = 0; i < 40; i++) { const d = sph(r); glow.add(at.clone().addScaledVector(d, 120 * s * r()).setY(40 + 50 * r()), d.multiplyScalar(20), r() * 1.2, 1.6 + r(), 60 * s, 140 * s, { drag: 1 }); }
    const ps = [thick, glow, P.embers(fx, r, at, 40, 280 * s, s, 0.2, 2.2, 40)];
    return { dur: 3.2, light: t => 2.2 * fade(t, 0, 0.4, 2.2, 3), lightAt: at.clone().setY(60), f: t => setAll(ps, t) };
  },
  pool(fx, s, at, r) {
    const fire = Particles(fx, 'fire', 260, { grav: [0, 140, 0], turb: 8, gain: 1.6, fade: [0.08, 0.5, 1, 1], heat0: 1.1, heat1: 0.45 });
    for (let i = 0; i < 260; i++) { const a = r() * Math.PI * 2, d = Math.sqrt(r()) * 160 * s; fire.add(at.clone().add(V3(Math.cos(a) * d, 4, Math.sin(a) * d)), V3(0, 40 + 60 * r(), 0).multiplyScalar(s), r() * 2.6, 0.5 + 0.4 * r(), 26 * s, 6 * s, { drag: 1, spin: r() - 0.5 }); }
    const smoke = Particles(fx, 'smoke', 50, { grav: [0, 60, 0], turb: 10, fade: [0.15, 0.5, 1, 0.5] });
    for (let i = 0; i < 50; i++) { const a = r() * Math.PI * 2, d = Math.sqrt(r()) * 150 * s; smoke.add(at.clone().add(V3(Math.cos(a) * d, 40, Math.sin(a) * d)), V3(0, 30, 0), r() * 2.4, 1.6, 40 * s, 120 * s, { drag: 1, spin: r() - 0.5 }); }
    const glowR = Ring(fx, 't_glow', 1.0, { rows: [[0, 0.5, 1], [1, 0.5, 0]], vrep: 1 });
    const ps = [fire, smoke, ...P.explosion(fx, r, at.clone().setY(30), s * 0.8, 0, 0.6)];
    return { dur: 3.2, light: t => 3.5 * fade(t, 0, 0.2, 2.5, 3.1) * (0.8 + 0.2 * Math.sin(t * 23)), lightAt: at.clone().setY(40),
      f: t => { setAll(ps, t); glowR.position.copy(at).setY(2); glowR.scale.set(200 * s, 1, 200 * s); glowR.material.uniforms.alpha.value = fade(t, 0.05, 0.3, 2.5, 3.1); } };
  },
  drain(fx, s, at, r) { const base = STYLE_FN.drain(fx, s, at, r); const e = P.embers(fx, r, at.clone().setY(60), 50, 80 * s, s, 0, 2.0, 30);
    return { dur: 2.6, light: t => 2.5 * fade(t, 0, 0.25, 1.9, 2.3), lightAt: at.clone().setY(80), f: t => { base.f(t); e.set(t); } }; },
  bind(fx, s, at, r) { const base = STYLE_FN.bind(fx, s, at, r); const e = P.embers(fx, r, at, 40, 180 * s, s, 0, 2.0, 50);
    return { dur: 2.8, light: t => 2.5 * fade(t, 0, 0.25, 2.0, 2.5), lightAt: at.clone().setY(30), f: t => { base.f(t); e.set(t); } }; },
  freeze(fx, s, at, r) { const base = STYLE_FN.freeze(fx, s, at, r);
    const glints = Particles(fx, 'spark', 90, { grav: [0, -200, 0], stretch: 0.04, gain: 2.4, fade: [0.01, 0.5, 1, 1] });
    for (let i = 0; i < 90; i++) glints.add(at.clone().setY(70), sph(r).multiplyScalar((150 + 300 * r()) * s), r() * 0.15, 0.4 + 0.4 * r(), 2 * s, 1 * s, { drag: 2.5 });
    const mist = Particles(fx, 'smoke', 40, { grav: [0, -10, 0], turb: 8, fade: [0.1, 0.5, 1, 0.4] });
    for (let i = 0; i < 40; i++) { const d = sph(r); mist.add(at.clone().addScaledVector(d, 60 * s).setY(40 + 60 * r()), d.multiplyScalar(30), 0.1 + r() * 1.4, 1.4, 40 * s, 110 * s, { drag: 1, spin: r() - 0.5 }); }
    return { dur: 2.8, light: t => 3 * Math.exp(-t * 2.5), lightAt: at.clone().setY(70), f: t => { base.f(t); glints.set(t); mist.set(t); } }; },
  toll(fx, s, at, r) { const base = STYLE_FN.toll(fx, s, at, r), d = P.dust(fx, r, at, 320 * s, s, 0.05, 70), d2 = P.dust(fx, r, at, 320 * s, s, 0.3, 40);
    return { dur: 2.2, light: t => 4 * Math.exp(-t * 4) + 2 * Math.exp(-Math.max(0, t - 0.22) * 5) * (t > 0.22), lightAt: at.clone().setY(70), f: t => { base.f(t); d.set(t); d2.set(t); } }; },
  wave(fx, s, at, r) { const base = STYLE_FN.wave(fx, s, at, r), from = fx.wielder.clone(), dir = at.clone().sub(from).setY(0).normalize();
    const dust = Particles(fx, 'smoke', 90, { grav: [0, 30, 0], turb: 10, fade: [0.1, 0.4, 0.4, 0.5] });
    for (let i = 0; i < 90; i++) { const k = r(), side = (r() - 0.5) * 2; const p = from.clone().addScaledVector(dir, 80 + 480 * k * s).add(V3(-dir.z * side * 160 * k * s, 6, dir.x * side * 160 * k * s));
      dust.add(p, dir.clone().multiplyScalar(160 * s).add(V3(0, 50, 0)), 0.08 + k * 0.6, 1 + r() * 0.6, 30 * s, 110 * s, { drag: 2, spin: r() - 0.5 }); }
    return { dur: 2.0, light: t => 3 * fade(t, 0, 0.1, 0.4, 0.9), lightAt: from.clone().addScaledVector(dir, 250).setY(70), f: t => { base.f(t); dust.set(t); } }; },
  summon(fx, s, at, r) { const base = STYLE_FN.summon(fx, s, at, r), e = P.embers(fx, r, at, 70, 160 * s, s, 0.2, 2.2, 160);
    const smoke = Particles(fx, 'smoke', 40, { grav: [0, 40, 0], turb: 12, fade: [0.1, 0.5, 1, 0.55] });
    for (let i = 0; i < 40; i++) { const a = r() * Math.PI * 2; smoke.add(at.clone().add(V3(Math.cos(a) * 90 * s, 10, Math.sin(a) * 90 * s)), V3(Math.cos(a) * 30, 40, Math.sin(a) * 30), 0.3 + r() * 1.2, 1.6, 40 * s, 120 * s, { drag: 1, spin: r() - 0.5 }); }
    return { dur: 3.2, light: t => 3 * fade(t, 0, 0.3, 2.3, 2.9), lightAt: at.clone().setY(60), f: t => { base.f(t); e.set(t); smoke.set(t); } }; },
  mark(fx, s, at, r) { const base = STYLE_FN.mark(fx, s, at, r), e = P.embers(fx, r, at.clone().setY(140), 30, 60 * s, s, 0, 2.2, -30);
    return { dur: 2.8, light: t => 1.5 * fade(t, 0, 0.3, 2.0, 2.5), lightAt: at.clone().setY(150), f: t => { base.f(t); e.set(t); } }; },
  slash(fx, s, at, r) { const base = STYLE_FN.slash(fx, s, at, r), ps = P.explosion(fx, r, at.clone().setY(70), s * 0.55, 0.22, 0.5);
    return { dur: 1.9, light: t => t > 0.22 ? 4 * Math.exp(-(t - 0.22) * 6) : 1.5 * (t > 0.05), lightAt: at.clone().setY(80), f: t => { base.f(t); setAll(ps, t); } }; },
  // (Noah, 2026-11-25: "This should look like the javelin bounces not lightning") the Wyrmspine Javelin itself, as the game flies it
  // (src/Classes.cpp JavFly): it strikes, then arcs slowly on to the next of the dead, point first, its element streaming behind it,
  // a flash and a burst at each it strikes
  bounce(fx, s, at, r) {
    const pts = [at.clone().setY(70), ...fx.targets.slice(1, 3).map(p => p.clone().setY(70))], hop = 0.6, arc = 120;
    const hops = pts.slice(0, -1).map((a, i) => ({ a, b: pts[i + 1], t0: 0.25 + i * (hop + 0.2) }));
    const trails = hops.map(() => boltPart(fx, s * 0.55, r));
    const ps = pts.flatMap((p, i) => P.explosion(fx, r, p, s * 0.45, i === 0 ? 0.05 : hops[i - 1].t0 + hop, 0.6));
    const flashes = pts.map(() => Card(fx, 't_glow', 2.6));
    const jav = new THREE.Group(); jav.visible = false; fx.g.add(jav);
    loadWeapon('javelin').then(d => { const m = weaponModel(d).g; m.scale.setScalar(d.scale || 1); jav.add(m); }).catch(() => {});
    const strikeAt = i => i === 0 ? 0.05 : hops[i - 1].t0 + hop, Y = V3(0, 1, 0);
    const end = hops[hops.length - 1].t0 + hop;
    return { dur: end + 1.4, light: t => pts.reduce((m, p, i) => Math.max(m, t >= strikeAt(i) ? 5 * Math.exp(-(t - strikeAt(i)) * 6) : 0), 0) + 1, lightAt: at.clone().setY(80),
      lightFollow: t => { const h = hops.find(h2 => t >= h2.t0 && t < h2.t0 + hop); if (!h) return at.clone().setY(80); const k = (t - h.t0) / hop, p = h.a.clone().lerp(h.b, k); p.y += Math.sin(Math.PI * k) * arc; return p; },
      f: t => {
        setAll(ps, t);
        hops.forEach((h, i) => trails[i](t - h.t0, h.a, h.b, hop, arc));
        pts.forEach((p, i) => { const k = (t - strikeAt(i)) / 0.3, c = flashes[i]; c.visible = k >= 0 && k < 1; if (!c.visible) return;
          c.position.copy(p); c.scale.setScalar(150 * s * (0.4 + 0.6 * k)); c.material.uniforms.alpha.value = 1 - k; });
        // the javelin: in flight on a hop, else stood in the one it struck last
        let pos, dir;
        const h = hops.find(h2 => t >= h2.t0 && t < h2.t0 + hop);
        if (h) { const k = (t - h.t0) / hop, at2 = k2 => { const p = h.a.clone().lerp(h.b, k2); p.y += Math.sin(Math.PI * k2) * arc; return p; };
          pos = at2(k); dir = at2(Math.min(1, k + 0.02)).sub(at2(Math.max(0, k - 0.02))).normalize(); }
        else { const i = t < hops[0].t0 ? 0 : t >= end ? pts.length - 1 : hops.findIndex(h2 => t < h2.t0); const last = Math.max(0, i);
          pos = pts[last].clone(); dir = (last > 0 ? pts[last].clone().sub(pts[last - 1]).setY(-60) : V3(0.5, -1, 0.2)).normalize(); }
        jav.visible = t >= 0 && t < end + 1.0; jav.position.copy(pos); jav.quaternion.setFromUnitVectors(Y, dir);
      } };
  },
};
