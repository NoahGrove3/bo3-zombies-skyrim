// ================================================================ The Curse of Alduin's own effects (Noah, Nov 26 night: "make the previews as good or close
// to what they would look like in the actual game. These effects need to have 4k textures and hyper realistic VFX made in 3D, not flat or
// layered textures"). Nothing here is a flat card: fire, smoke, mist and light are volumes ray-marched through 3D noise (seen from any
// side they have depth), sparks and embers are thousands of GPU particles, lightning is a branching 3D bolt, ice and stone are real 3D
// meshes. The same shaders bake the game's 4K (and 2K) flipbooks, so the game plays what this shows. Needs three.js r128 + viewer.js.

function mulberry(a) { return () => { a |= 0; a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }
const NOISE = `
vec3 m289(vec3 x){return x-floor(x*(1./289.))*289.;} vec4 m289(vec4 x){return x-floor(x*(1./289.))*289.;}
vec4 perm(vec4 x){return m289(((x*34.)+1.)*x);} vec4 tis(vec4 r){return 1.79284291400159-0.85373472095314*r;}
float snoise(vec3 v){ const vec2 C=vec2(1./6.,1./3.); const vec4 D=vec4(0.,.5,1.,2.);
  vec3 i=floor(v+dot(v,C.yyy)); vec3 x0=v-i+dot(i,C.xxx); vec3 g=step(x0.yzx,x0.xyz); vec3 l=1.-g; vec3 i1=min(g.xyz,l.zxy); vec3 i2=max(g.xyz,l.zxy);
  vec3 x1=x0-i1+C.xxx; vec3 x2=x0-i2+C.yyy; vec3 x3=x0-D.yyy; i=m289(i);
  vec4 p=perm(perm(perm(i.z+vec4(0.,i1.z,i2.z,1.))+i.y+vec4(0.,i1.y,i2.y,1.))+i.x+vec4(0.,i1.x,i2.x,1.));
  float n_=.142857142857; vec3 ns=n_*D.wyz-D.xzx; vec4 j=p-49.*floor(p*ns.z*ns.z); vec4 x_=floor(j*ns.z); vec4 y_=floor(j-7.*x_);
  vec4 x=x_*ns.x+ns.yyyy; vec4 y=y_*ns.x+ns.yyyy; vec4 h=1.-abs(x)-abs(y); vec4 b0=vec4(x.xy,y.xy); vec4 b1=vec4(x.zw,y.zw);
  vec4 s0=floor(b0)*2.+1.; vec4 s1=floor(b1)*2.+1.; vec4 sh=-step(h,vec4(0.)); vec4 a0=b0.xzyw+s0.xzyw*sh.xxyy; vec4 a1=b1.xzyw+s1.xzyw*sh.zzww;
  vec3 p0=vec3(a0.xy,h.x); vec3 p1=vec3(a0.zw,h.y); vec3 p2=vec3(a1.xy,h.z); vec3 p3=vec3(a1.zw,h.w);
  vec4 nm=tis(vec4(dot(p0,p0),dot(p1,p1),dot(p2,p2),dot(p3,p3))); p0*=nm.x; p1*=nm.y; p2*=nm.z; p3*=nm.w;
  vec4 m=max(.6-vec4(dot(x0,x0),dot(x1,x1),dot(x2,x2),dot(x3,x3)),0.); m=m*m;
  return 42.*dot(m*m,vec4(dot(p0,x0),dot(p1,x1),dot(p2,x2),dot(p3,x3))); }
float fbm(vec3 p){ float a=.5, s=0.; for(int i=0;i<4;i++){ s+=a*snoise(p); p=p*2.03+vec3(1.7,9.2,3.1); a*=.5; } return s; }
float hash12(vec2 p){ vec3 p3=fract(vec3(p.xyx)*.1031); p3+=dot(p3,p3.yzx+33.33); return fract((p3.x+p3.y)*p3.z); }
`;

// ---------------------------------------------------------------- the elements: one palette and one way of moving each
// c: deep, main, hot core; smoke: drawn over (absorbs light) instead of glowing; rise: how its volumes climb; spark: its particles
const ELEM = {
  fire: { c: ['#3a0700', '#ff4d0a', '#fff1b8'], rise: 1.3, nsc: 2.1, spark: 'ember', light: '#ff7a2e', tint: [1, .42, .08] },
  frost: { c: ['#06213a', '#7cc8ff', '#ffffff'], smoke: true, rise: -0.25, nsc: 1.6, spark: 'snow', shards: true, light: '#9fd6ff', tint: [.55, .8, 1] },
  shock: { c: ['#120a40', '#7b6cff', '#f4f2ff'], rise: 0.4, nsc: 2.6, spark: 'spark', bolts: true, light: '#b9a8ff', tint: [.6, .75, 1] },
  shadowfire: { c: ['#050008', '#7a1cff', '#efc8ff'], rise: 1.0, nsc: 2.2, spark: 'ember', dark: true, light: '#9a3bff', tint: [.7, .3, 1] },
  holy: { c: ['#4a2c00', '#ffc23a', '#ffffff'], rise: 0.6, nsc: 1.5, spark: 'mote', rays: true, light: '#ffd56a', tint: [1, .85, .45] },
  poison: { c: ['#071400', '#5fc21c', '#e4ff8a'], smoke: true, rise: 0.15, nsc: 1.8, spark: 'drip', light: '#9be03a', tint: [.35, .9, .2] },
  arcane: { c: ['#070c3a', '#4f86ff', '#e8f0ff'], rise: 0.3, nsc: 1.9, spark: 'mote', runes: true, light: '#8fb4ff', tint: [.5, .65, 1] },
  blood: { c: ['#140000', '#9a0010', '#ff5a48'], smoke: true, rise: 0.2, nsc: 2.0, spark: 'drip', light: '#c0182a', tint: [.9, .08, .05] },
  spirit: { c: ['#0a1828', '#9fd2ff', '#ffffff'], rise: 0.7, nsc: 1.4, spark: 'wisp', light: '#dfe8ff', tint: [.75, .85, 1] },
  earth: { c: ['#1a1008', '#7a5a34', '#ffcf80'], smoke: true, rise: 0.25, nsc: 1.7, spark: 'grit', rocks: true, light: '#b08a5a', tint: [.7, .55, .35] },
  wind: { c: ['#1a242c', '#c8dcea', '#ffffff'], smoke: true, rise: 0.1, nsc: 1.3, spark: 'streak', swirl: true, light: '#cfe3f2', tint: [.85, .9, 1] },
};
// the colours a weapon look is drawn in (viewer.js COLS keys) -> the element it moves like
const LOOK_ELEM = { shadow: 'shadowfire', purple: 'shadowfire', gold: 'holy', white: 'holy', blue: 'arcane', carian: 'arcane', iceblue: 'frost', orange: 'fire', red: 'fire',
  green: 'poison', poison: 'poison', drain: 'arcane', moon: 'spirit', fear: 'blood', ss_none: 'fire', arcane: 'arcane', shockv: 'shock' };
// (additive: one + one, the colour's own brightness added - the shaders write alpha 0 for it)
const ADD = () => ({ blending: THREE.CustomBlending, blendEquation: THREE.AddEquation, blendSrc: THREE.OneFactor, blendDst: THREE.OneFactor });
const v3c = h => new THREE.Color(h).convertSRGBToLinear();

// ---------------------------------------------------------------- volumes: a cube the camera's rays march through
// shape 0 ball (r grows with life), 1 flame/column (radius r0 at the foot to r1 at the top), 2 ring (a torus lying flat, widening),
// 3 ground layer (thick at the foot, thinning upward), 4 beam (a thin column of light)
const VOLVS = `varying vec3 vO; void main(){ vO = position; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.); }`;
const VOLFS = NOISE + `
uniform vec3 camO, c0, c1, c2; uniform float time, life, dens, gain, smoke, dark, shape, rise, nsc, namp, cut, swirl, seed, steps;
uniform vec4 sp; varying vec3 vO;
float shapeD(vec3 p, out float h){
  h = p.y * .5 + .5;
  if (shape < .5) { return 1. - length(p) / max(sp.x, .001); }
  if (shape < 1.5) { float r = mix(sp.x, sp.y, h); return min(1. - length(p.xz) / max(r, .001), smoothstep(0., .15, h) * smoothstep(1., .7, h) * 4.); }
  if (shape < 2.5) { float q = length(vec2(length(p.xz) - sp.x, p.y * sp.z)); return 1. - q / max(sp.y, .001); }
  if (shape < 3.5) { float r = 1. - length(p.xz) / max(sp.x, .001); return min(r * 2., exp(-h * sp.y) - .05); }
  float r = 1. - length(p.xz) / max(mix(sp.x, sp.y, h), .001); return min(r, smoothstep(0., .05, h) * smoothstep(1., .9, h) * 4.);
}
float dens3(vec3 p, out float heat){
  float h; float b = shapeD(p, h);
  if (b < -.4) { heat = 0.; return 0.; }
  vec3 q = p; float a = swirl * (time * 1.3 + h * 2.);
  q.xz = mat2(cos(a), -sin(a), sin(a), cos(a)) * q.xz;
  float n = fbm(q * nsc + vec3(seed, -time * rise + seed * .7, seed * 1.3));
  float d = clamp(b + n * namp - cut, 0., 1.);
  heat = clamp(d * 1.7 * (1. - h * .55 * step(.5, shape) * step(shape, 1.5)) + .15 * b, 0., 1.);
  return d;
}
vec3 ramp(float x){ return x < .5 ? mix(c0, c1, x * 2.) : mix(c1, c2, (x - .5) * 2.); }
void main(){
  vec3 ro = camO, rd = normalize(vO - camO);
  vec3 t0 = (-1. - ro) / rd, t1 = (1. - ro) / rd, tn3 = min(t0, t1), tf3 = max(t0, t1);
  float tn = max(max(max(tn3.x, tn3.y), tn3.z), 0.), tf = min(min(tf3.x, tf3.y), tf3.z);
  if (tf <= tn) discard;
  const int N = 36; float st = (tf - tn) / float(N), t = tn + st * hash12(gl_FragCoord.xy + time);
  vec3 acc = vec3(0.); float T = 1.;
  for (int i = 0; i < N; i++) {
    if (float(i) >= steps) break;
    vec3 p = ro + rd * t; float heat; float d = dens3(p, heat);
    if (d > .002) {
      float a = 1. - exp(-d * st * dens * life);
      vec3 col;
      if (smoke > .5) col = mix(c0, c1, smoothstep(0., .8, d)) + c2 * pow(heat, 3.) * .6;
      else if (dark > .5) col = mix(vec3(0.), ramp(heat), smoothstep(.15, .9, 1. - d)) * 1.6 + c2 * pow(heat, 5.) * 1.2;
      else col = ramp(heat) * (.35 + heat * 2.6);
      acc += T * a * col; T *= 1. - a;
      if (T < .02) break;
    }
    t += st;
  }
  if (smoke > .5 || dark > .5) gl_FragColor = vec4(acc * gain, (1. - T) * min(1., life * 1.5));
  else gl_FragColor = vec4(acc * gain * life, 0.);
}`;
function Volume(o) {
  const E = ELEM[o.el] || ELEM.fire;
  const u = { camO: { value: new THREE.Vector3() }, c0: { value: v3c(E.c[0]) }, c1: { value: v3c(E.c[1]) }, c2: { value: v3c(E.c[2]) },
    time: { value: 0 }, life: { value: 1 }, dens: { value: o.dens || 9 }, gain: { value: o.gain || 1 }, smoke: { value: (o.smoke != null ? o.smoke : E.smoke) ? 1 : 0 },
    dark: { value: (o.dark != null ? o.dark : E.dark) ? 1 : 0 }, shape: { value: o.shape || 0 }, rise: { value: o.rise != null ? o.rise : E.rise }, nsc: { value: o.nsc || E.nsc },
    namp: { value: o.namp != null ? o.namp : .9 }, cut: { value: o.cut != null ? o.cut : .05 }, swirl: { value: o.swirl || 0 }, seed: { value: o.seed != null ? o.seed : Math.random() * 40 },
    steps: { value: o.steps || 36 }, sp: { value: new THREE.Vector4(...(o.sp || [.8, .6, 1, 0])) } };
  const alpha = u.smoke.value || u.dark.value;
  const m = new THREE.ShaderMaterial({ uniforms: u, vertexShader: VOLVS, fragmentShader: VOLFS, transparent: true, depthWrite: false, side: THREE.BackSide,
    blending: THREE.CustomBlending, blendEquation: THREE.AddEquation, blendSrc: THREE.OneFactor, blendDst: alpha ? THREE.OneMinusSrcAlphaFactor : THREE.OneFactor });
  const mesh = new THREE.Mesh(new THREE.BoxGeometry(2, 2, 2), m);
  mesh.frustumCulled = false; mesh.renderOrder = alpha ? 3 : 4;
  if (!alpha || o.glowLayer) mesh.layers.enable(1);
  mesh.onBeforeRender = (r, s, cam) => { u.camO.value.copy(cam.position); mesh.worldToLocal(u.camO.value); };
  mesh.u = u;
  return mesh;
}

// ---------------------------------------------------------------- GPU particles: each one's whole flight worked out from its start
const PVS2 = `attribute vec3 aP, aV; attribute vec4 aT, aR; uniform float time, loop, psz; uniform vec3 grav; uniform float turb;
varying vec2 vUv; varying float vK, vSeed, vKind;
void main(){
  float age = time - aT.x; if (loop > 0.) age = mod(age, aT.y); float life = aT.y; vK = age / life; vSeed = aR.x; vKind = aR.w;
  if (age < 0. || age > life) { gl_Position = vec4(2., 2., 2., 1.); return; }
  float drag = max(aR.z, .0001), dd = (1. - exp(-drag * age)) / drag;
  vec3 p = aP + aV * dd + .5 * grav * age * age;
  p += turb * vec3(sin(age * 2.3 + aR.x * 17.), sin(age * 1.7 + aR.x * 29.) * .6, cos(age * 2.1 + aR.x * 11.)) * age;
  vec3 vel = aV * exp(-drag * age) + grav * age;
  float size = mix(aT.z, aT.w, vK) * psz;
  vec4 mv = modelViewMatrix * vec4(p, 1.);
  vec2 c = position.xy;
  if (aR.w > .5 && aR.w < 1.5) {   // (a spark or a streak: drawn out along its flight)
    vec3 vv = (modelViewMatrix * vec4(vel, 0.)).xyz; vec2 d = vv.xy; float l = length(d);
    vec2 dir = l > .001 ? d / l : vec2(0., 1.); vec2 n = vec2(-dir.y, dir.x);
    mv.xy += dir * c.y * (size + l * .045) + n * c.x * size * .3;
  } else { float a = aR.y * age + aR.x * 6.28; mv.xy += vec2(c.x * cos(a) - c.y * sin(a), c.x * sin(a) + c.y * cos(a)) * size; }
  vUv = c + .5; gl_Position = projectionMatrix * mv;
}`;
const PFS2 = `uniform vec3 c0, c1, c2; uniform float gain, alpha; varying vec2 vUv; varying float vK, vSeed, vKind;
void main(){
  vec2 q = vUv - .5; float r = length(q) * 2.; if (r > 1.) discard;
  float core = pow(1. - r, 2.2), a;
  if (vKind > 2.5) a = smoothstep(1., .55, r) * .9;                    // (a soft mote / a drop: a round body)
  else a = core;
  float k = vK; vec3 col = k < .35 ? mix(c2, c1, k / .35) : mix(c1, c0, (k - .35) / .65);
  float flick = .75 + .25 * sin(vSeed * 91. + k * 40.);
  float fade = smoothstep(0., .08, k) * (1. - smoothstep(.65, 1., k));
  if (alpha > .5) gl_FragColor = vec4(col * gain, a * fade);
  else gl_FragColor = vec4(col * gain * a * fade * flick, 0.);
}`;
const PQUAD = (() => { const g = new THREE.InstancedBufferGeometry(); g.setAttribute('position', new THREE.Float32BufferAttribute([-.5, -.5, 0, .5, -.5, 0, .5, .5, 0, -.5, .5, 0], 3)); g.setIndex([0, 1, 2, 0, 2, 3]); return g; })();
// kind: 0 glow (embers, motes), 1 spark (stretched), 3 body (drops, grit, snow - drawn over)
function Particles(o) {
  const cap = o.cap || 600, E = ELEM[o.el] || ELEM.fire;
  const g = new THREE.InstancedBufferGeometry(); g.index = PQUAD.index; g.setAttribute('position', PQUAD.attributes.position);
  const A = { aP: new Float32Array(cap * 3), aV: new Float32Array(cap * 3), aT: new Float32Array(cap * 4), aR: new Float32Array(cap * 4) };
  for (let i = 0; i < cap; i++) A.aT[i * 4] = -1e9, A.aT[i * 4 + 1] = 1;
  for (const k in A) g.setAttribute(k, new THREE.InstancedBufferAttribute(A[k], k === 'aP' || k === 'aV' ? 3 : 4));
  g.instanceCount = 0;
  const cs = o.c || E.c, u = { time: { value: 0 }, psz: { value: o.psz || 1 }, loop: { value: o.loop ? 1 : 0 }, grav: { value: new THREE.Vector3(...(o.grav || [0, 0, 0])) }, turb: { value: o.turb || 0 },
    c0: { value: v3c(cs[0]) }, c1: { value: v3c(cs[1]) }, c2: { value: v3c(cs[2]) }, gain: { value: o.gain || 2.2 }, alpha: { value: o.alpha ? 1 : 0 } };
  const m = new THREE.ShaderMaterial(Object.assign({ uniforms: u, vertexShader: PVS2, fragmentShader: PFS2, transparent: true, depthWrite: false, side: THREE.DoubleSide }, o.alpha ? { blending: THREE.NormalBlending } : ADD()));
  const mesh = new THREE.Mesh(g, m); mesh.frustumCulled = false; mesh.renderOrder = 5; if (!o.alpha) mesh.layers.enable(1);
  let n = 0;
  mesh.add1 = (p, v, t0, life, s0, s1, drag, kind) => {
    if (n >= cap) return; const i = n++;
    A.aP.set(p, i * 3); A.aV.set(v, i * 3); A.aT.set([t0, life, s0, s1], i * 4); A.aR.set([Math.random(), (Math.random() - .5) * 4, drag || 0, kind || 0], i * 4);
    g.instanceCount = n; for (const k in A) g.attributes[k].needsUpdate = true;
  };
  mesh.u = u; return mesh;
}
// a burst of n particles from a point: dir (unit vector or null for every way), spread 0..1, speed range
function burstInto(P, n, at, o) {
  const dir = o.dir ? new THREE.Vector3(...o.dir).normalize() : null;
  for (let i = 0; i < n; i++) {
    let d = new THREE.Vector3(Math.random() * 2 - 1, Math.random() * 2 - 1, Math.random() * 2 - 1);
    if (d.lengthSq() > 1 || d.lengthSq() < .01) { i--; continue; }
    d.normalize(); if (dir) d.lerp(dir, 1 - (o.spread == null ? .5 : o.spread)).normalize();
    if (o.flat) d.y = Math.abs(d.y) * o.flat;
    const sp = (o.speed[0] + Math.random() * (o.speed[1] - o.speed[0]));
    const p = [at[0] + (Math.random() - .5) * (o.jit || 0), at[1] + (Math.random() - .5) * (o.jit || 0) * (o.flat ? .2 : 1), at[2] + (Math.random() - .5) * (o.jit || 0)];
    const life = o.life[0] + Math.random() * (o.life[1] - o.life[0]);
    P.add1(p, [d.x * sp, d.y * sp + (o.up || 0), d.z * sp], (o.t || 0) + Math.random() * (o.tw || 0), life, o.size[0] * (.6 + Math.random() * .8), o.size[1] * (.6 + Math.random() * .8), o.drag, o.kind);
  }
}

// ---------------------------------------------------------------- lightning: a branching 3D bolt, struck again every few frames
const BOLTVS = `attribute vec3 nxt; attribute float side, w; varying float vS, vW; uniform float width;
void main(){ vec4 a = modelViewMatrix * vec4(position, 1.), b = modelViewMatrix * vec4(nxt, 1.);
  vec2 d = normalize((b.xy - a.xy) + vec2(1e-5)); vec2 n = vec2(-d.y, d.x); a.xy += n * side * width * w; vS = side; vW = w; gl_Position = projectionMatrix * a; }`;
const BOLTFS = `uniform vec3 c1, c2; uniform float gain, flash; varying float vS, vW;
void main(){ float x = abs(vS); float core = pow(1. - x, 3.); vec3 col = mix(c1, c2, core); gl_FragColor = vec4(col * gain * flash * (core * 1.6 + (1. - x) * .5), 0.); }`;
function Bolts(o) {
  const E = ELEM[o.el] || ELEM.shock, u = { width: { value: o.width || 4 }, c1: { value: v3c(E.c[1]) }, c2: { value: v3c(E.c[2]) }, gain: { value: o.gain || 3 }, flash: { value: 1 } };
  const m = new THREE.ShaderMaterial(Object.assign({ uniforms: u, vertexShader: BOLTVS, fragmentShader: BOLTFS, transparent: true, depthWrite: false, side: THREE.DoubleSide }, ADD()));
  const geo = new THREE.BufferGeometry(), mesh = new THREE.Mesh(geo, m); mesh.frustumCulled = false; mesh.layers.enable(1); mesh.renderOrder = 6;
  let key = -1;
  function strike(rnd, list) {   // list: [[from, to, jag], ...] -> a polyline each, with branches
    const P = [], N = [], S = [], Wd = [], I = [];
    const seg = (pts, w0) => {
      for (let i = 0; i < pts.length - 1; i++) {
        const a = pts[i], b = pts[i + 1], w = w0 * (1 - i / pts.length * .6), base = P.length / 3;
        for (const [pp, nn, s] of [[a, b, -1], [a, b, 1], [b, a, 1], [b, a, -1]]) { P.push(...pp); N.push(...(nn === b ? b : [2 * b[0] - a[0], 2 * b[1] - a[1], 2 * b[2] - a[2]])); S.push(s); Wd.push(w); }
        I.push(base, base + 1, base + 2, base, base + 2, base + 3);
      }
    };
    const path = (a, b, jag, depth, w) => {
      let pts = [a, b];
      for (let it = 0; it < 6; it++) {
        const out = [pts[0]];
        for (let i = 0; i < pts.length - 1; i++) {
          const p = pts[i], q = pts[i + 1], L = Math.hypot(q[0] - p[0], q[1] - p[1], q[2] - p[2]);
          out.push([(p[0] + q[0]) / 2 + (rnd() - .5) * L * jag, (p[1] + q[1]) / 2 + (rnd() - .5) * L * jag, (p[2] + q[2]) / 2 + (rnd() - .5) * L * jag], q);
        }
        pts = out;
      }
      seg(pts, w);
      if (depth < 2) for (let k = 0; k < 3 - depth; k++) {
        const i = 4 + Math.floor(rnd() * (pts.length - 8)); const s = pts[i], L = Math.hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]) * (.18 + rnd() * .25);
        path(s, [s[0] + (rnd() - .5) * L, s[1] + (rnd() - .7) * L, s[2] + (rnd() - .5) * L], jag * 1.2, depth + 1, w * .45);
      }
    };
    for (const [a, b, jag] of list) path(a, b, jag || .35, 0, 1);
    geo.setAttribute('position', new THREE.Float32BufferAttribute(P, 3)); geo.setAttribute('nxt', new THREE.Float32BufferAttribute(N, 3));
    geo.setAttribute('side', new THREE.Float32BufferAttribute(S, 1)); geo.setAttribute('w', new THREE.Float32BufferAttribute(Wd, 1)); geo.setIndex(I);
  }
  mesh.at = (t, list, on) => {   // a new strike 14 times a second, a flicker between
    const k = Math.floor(t * 14); u.flash.value = on * (.55 + .45 * Math.abs(Math.sin(t * 61)));
    mesh.visible = on > .01;
    if (k !== key && mesh.visible) { key = k; strike(mulberry(k * 977 + (o.seed || 1)), list); }
  };
  mesh.u = u; return mesh;
}

// ---------------------------------------------------------------- ice and stone: real 3D crystals and rocks
const SHARDVS = `varying vec3 vN, vV, vP; void main(){ vec4 w = modelMatrix * vec4(position, 1.); vP = position; vN = normalize(mat3(modelMatrix) * normal); vV = normalize(cameraPosition - w.xyz); gl_Position = projectionMatrix * viewMatrix * w; }`;
const SHARDFS = NOISE + `uniform vec3 c0, c1, c2; uniform float glow, op; varying vec3 vN, vV, vP;
void main(){ float f = pow(1. - abs(dot(normalize(vN), vV)), 2.); float n = snoise(vP * .08) * .5 + .5;
  vec3 col = mix(c0 * 1.5, c1, .35 + n * .4) + c2 * f * 1.1 + c1 * glow;
  gl_FragColor = vec4(col, op * (.55 + f * .45)); }`;
function crystalGeo(seed) {
  const r = mulberry(seed), g = new THREE.CylinderGeometry(0, 1, 1, 6, 1, false); g.translate(0, .5, 0);
  const p = g.attributes.position;
  for (let i = 0; i < p.count; i++) { const y = p.getY(i); if (y < .99) { p.setX(i, p.getX(i) * (.75 + r() * .5)); p.setZ(i, p.getZ(i) * (.75 + r() * .5)); } }
  g.computeVertexNormals(); return g;
}
function Shard(el, seed, o = {}) {
  const E = ELEM[el] || ELEM.frost, cs = o.c || E.c;
  const m = new THREE.ShaderMaterial({ uniforms: { c0: { value: v3c(cs[0]) }, c1: { value: v3c(cs[1]) }, c2: { value: v3c(cs[2]) }, glow: { value: o.glow || .15 }, op: { value: 1 } },
    vertexShader: SHARDVS, fragmentShader: SHARDFS, transparent: true, depthWrite: true, side: THREE.DoubleSide });
  const s = new THREE.Mesh(crystalGeo(seed), m); s.layers.enable(1); return s;
}
function rockGeo(seed) {
  const r = mulberry(seed), g = new THREE.IcosahedronGeometry(1, 1), p = g.attributes.position;
  const k = {}; for (let i = 0; i < p.count; i++) { const key = [p.getX(i), p.getY(i), p.getZ(i)].map(x => x.toFixed(3)).join(); if (!(key in k)) k[key] = .7 + r() * .55; const s = k[key]; p.setXYZ(i, p.getX(i) * s, p.getY(i) * s * .8, p.getZ(i) * s); }
  g.computeVertexNormals(); return g;
}
const ROCKMAT = () => new THREE.MeshStandardMaterial({ color: 0x5b4a3a, roughness: .95, metalness: 0, flatShading: true });

// ---------------------------------------------------------------- the rune circle on the ground (a disc in the world, lit from within)
const RUNEFS = NOISE + `uniform vec3 c1, c2; uniform float time, on, kind; varying vec2 vUv;
void main(){ vec2 q = vUv * 2. - 1.; float r = length(q), a = atan(q.y, q.x);
  float ring = smoothstep(.02, 0., abs(r - .92)) + smoothstep(.015, 0., abs(r - .78)) * .8 + smoothstep(.012, 0., abs(r - .42)) * .6;
  float seg = step(.5, fract((a + time * .25) / 6.2832 * 24.)) * step(.80, r) * step(r, .9);
  float g = floor((a - time * .4) / 6.2832 * 16.); float glyph = step(.55, hash12(vec2(g, floor(r * 9.)))) * step(.46, r) * step(r, .74) * step(.3, fract((a - time * .4) / 6.2832 * 16.)) * step(fract(r * 9.), .7);
  float spokes = smoothstep(.02, 0., abs(sin((a + time * .1) * 3.))) * step(r, .42) * step(.08, r);
  float m = (ring + seg * .7 + glyph * .9 + spokes * .5) * step(r, 1.);
  float burn = (1. - smoothstep(0., 1., r)) * .25 * (snoise(vec3(q * 3., time)) * .5 + .5);
  vec3 col = mix(c1, c2, clamp(m - .5, 0., 1.)) * (m * 1.6 + burn);
  gl_FragColor = vec4(col * on, 0.); }`;
function RuneDisc(el, R) {
  const E = ELEM[el], u = { c1: { value: v3c(E.c[1]) }, c2: { value: v3c(E.c[2]) }, time: { value: 0 }, on: { value: 1 }, kind: { value: 0 } };
  const m = new THREE.ShaderMaterial({ uniforms: u, vertexShader: 'varying vec2 vUv; void main(){ vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.); }',
    fragmentShader: RUNEFS, transparent: true, depthWrite: false, side: THREE.DoubleSide, ...ADD() });
  const d = new THREE.Mesh(new THREE.PlaneGeometry(R * 2, R * 2), m); d.rotation.x = -Math.PI / 2; d.position.y = 1.2; d.layers.enable(1); d.renderOrder = 2; d.u = u; return d;
}

// ---------------------------------------------------------------- an effect: everything one kit part plays, laid out on its own beats
// fx.f(t) sets every piece for time t in the loop (0..dur); fx.light(t) its light; all pieces in fx.g (world units: Skyrim's, ~70 to a metre)
const env = (t, a, b, c, d) => t < a || t > d ? 0 : t < b ? (t - a) / (b - a) : t < c ? 1 : 1 - (t - c) / (d - c);
const ease = x => 1 - Math.pow(1 - Math.max(0, Math.min(1, x)), 3);
function placeVol(v, at, sx, sy, sz) { v.position.set(at[0], Math.max(at[1], sy + 3), at[2]); v.scale.set(sx, sy, sz == null ? sx : sz); }   // (never sunk into the ground: its box would cut on it)
function makeEffect(el, role, opt, o = {}) {
  const E = ELEM[el] || ELEM.fire, g = new THREE.Group(), parts = [];
  const at = o.at || [0, 0, 0], S = o.size || 1, seed = o.seed || 7;
  const fx = { g, dur: 2.4, f: null, light: null, lightAt: new THREE.Vector3(at[0], at[1] + 60 * S, at[2]), el, react: o.react };
  const add = x => (g.add(x), x);
  const P = add(Particles({ el, cap: 1400, psz: 3.2, grav: [0, E.rise > .5 ? 40 : E.spark === 'drip' || E.spark === 'grit' ? -520 : E.spark === 'snow' ? -60 : 0, 0], turb: E.spark === 'wisp' ? 60 : 18, gain: 2.4 }));
  const Pa = E.smoke || E.spark === 'drip' || E.spark === 'grit' ? add(Particles({ el, cap: 600, psz: 2.6, grav: [0, -620, 0], alpha: true, gain: 1, c: [E.c[0], E.c[1], E.c[1]] })) : null;
  const vols = [];
  const vol = (oo, place, f) => { const v = add(Volume(Object.assign({ el, seed: seed + vols.length * 3.1 }, oo))); vols.push({ v, f }); place(v); return v; };
  let bolts = null, bl = null, shards = [], rocks = [], disc = null;
  const sparks = (n, p, oo) => burstInto(E.spark === 'drip' || E.spark === 'grit' ? (Pa || P) : P, n, p, Object.assign({ kind: E.spark === 'spark' || E.spark === 'streak' ? 1 : E.spark === 'drip' || E.spark === 'grit' || E.spark === 'snow' ? 3 : 0 }, oo));
  const y = at[1];
  const H = 40 * S;   // (the height an impact happens at: a chest)
  // ---- each part's designs
  const D = {
    // IMPACT: one hit landing
    'impact:core': () => {   // a compact flare of the element, its sparks thrown out, a lingering wisp
      fx.dur = 1.6;
      const c = [at[0], y + H, at[2]];
      vol({ shape: 0, sp: [.9, 0, 0, 0], dens: 11, namp: 1, cut: .1 }, v => placeVol(v, c, 34 * S, 34 * S), (v, t) => { const k = env(t, 0, .06, .18, .7); v.u.life.value = k; const s = (24 + 30 * ease(t / .5)) * S; v.scale.set(s, s, s); });
      vol({ shape: 0, sp: [.8, 0, 0, 0], dens: 5, namp: 1.2, cut: .2, smoke: E.smoke || el === 'fire', dark: false, gain: el === 'fire' ? .5 : 1 }, v => placeVol(v, c, 40 * S, 40 * S),
        (v, t) => { v.u.life.value = env(t, .1, .35, .6, 1.5); const s = (30 + 40 * ease(t / 1.2)) * S; v.scale.set(s, s, s); v.position.y = c[1] + 30 * t * E.rise; });
      sparks(90, c, { speed: [260 * S, 720 * S], life: [.25, .8], size: [3 * S, .5 * S], drag: 3.5, jit: 8 * S });
      if (el === 'frost') shardRing(c, 7, 14 * S, true, 0);
      if (E.bolts) bl = [[c, [c[0] + 70 * S, c[1] + 10, c[2] + 30], .5], [c, [c[0] - 60 * S, c[1] - 30, c[2] - 20], .5], [c, [c[0] + 10, c[1] + 70 * S, c[2]], .5]];
      fx.light = t => env(t, 0, .04, .15, .7) * 1.6;
    },
    'impact:spark': () => {   // it splashes: the element's own spray (flame tongues, ice, arcs, blood, grit)
      fx.dur = 1.6;
      const c = [at[0], y + H, at[2]];
      sparks(260, c, { dir: [0, .35, 1], spread: .55, speed: [380 * S, 1100 * S], life: [.3, 1], size: [3.5 * S, 1 * S], drag: 2.6, jit: 6 * S });
      for (let k = 0; k < 4; k++) vol({ shape: 1, sp: [.5, .15, 0, 0], dens: 10, rise: E.rise * 1.5, cut: .1 }, v => placeVol(v, c, 16 * S, 34 * S),
        (v, t) => { const tt = t - k * .05, ang = k * 1.6 + .5; v.u.life.value = env(tt, 0, .05, .2, .55); v.position.set(c[0] + Math.cos(ang) * 40 * S * ease(tt / .4), c[1] + 30 * S * ease(tt / .4), c[2] + Math.sin(ang) * 40 * S * ease(tt / .4) + 20 * S); v.rotation.set(Math.sin(ang) * .9, 0, Math.cos(ang) * .9); });
      if (el === 'frost') shardRing(c, 10, 18 * S, true, 0);
      if (E.bolts) bl = [0, 1, 2, 3, 4].map(i => [c, [c[0] + Math.cos(i * 1.3) * 120 * S, c[1] + (i - 2) * 25 * S, c[2] + Math.sin(i * 1.3) * 120 * S], .45]);
      if (E.rocks) rockBurst(c, 9, 7 * S, 0);
      fx.light = t => env(t, 0, .03, .12, .6) * 1.4;
    },
    'impact:drain': () => {   // pulled in: the element streams into the struck one, then pops
      fx.dur = 2;
      const c = [at[0], y + H, at[2]];
      for (let i = 0; i < 140; i++) { const d = new THREE.Vector3(Math.random() - .5, Math.random() - .3, Math.random() - .5).normalize().multiplyScalar(110 * S + Math.random() * 60 * S);
        P.add1([c[0] + d.x, c[1] + d.y, c[2] + d.z], [-d.x * 1.6, -d.y * 1.6, -d.z * 1.6], Math.random() * .6, .55, 2.6 * S, .6 * S, .01, E.spark === 'spark' ? 1 : 0); }
      vol({ shape: 0, sp: [.85, 0, 0, 0], dens: 9, swirl: 1.2, cut: .1 }, v => placeVol(v, c, 40 * S, 40 * S), (v, t) => { v.u.life.value = env(t, 0, .4, .7, .95); const s = (46 - 26 * ease(t / .9)) * S; v.scale.set(s, s, s); });
      vol({ shape: 0, sp: [.9, 0, 0, 0], dens: 12, cut: .05 }, v => placeVol(v, c, 20 * S, 20 * S), (v, t) => { v.u.life.value = env(t, .85, .9, 1, 1.4); const s = (20 + 50 * ease((t - .85) / .4)) * S; v.scale.set(s, s, s); });
      sparks(80, c, { speed: [300 * S, 700 * S], life: [.3, .7], size: [3 * S, .5 * S], drag: 3, t: .88 });
      fx.light = t => env(t, 0, .5, .85, 1.3) * 1.2;
    },
    // BURST: a heavy, an explosion
    'burst:big': () => {   // a full explosion: the fireball of the element rolling up into its smoke, debris, a scorch
      fx.dur = 2.8;
      const c = [at[0], y + 20 * S, at[2]];
      vol({ shape: 0, sp: [.92, 0, 0, 0], dens: 12, namp: 1.1, cut: .02, steps: 36 }, v => placeVol(v, c, 60, 60), (v, t) => { v.u.life.value = env(t, 0, .05, .35, 1.1); const s = (40 + 130 * ease(t / .7)) * S; v.scale.set(s, s * .85, s); v.position.y = c[1] + 40 * S * ease(t / 1); });
      vol({ shape: 0, sp: [.85, 0, 0, 0], dens: 6, namp: 1.3, cut: .25, smoke: true, gain: .8 }, v => placeVol(v, c, 80, 80),
        (v, t) => { v.u.life.value = env(t, .25, .6, 1.4, 2.6) * .9; const s = (90 + 120 * ease((t - .2) / 2)) * S; v.scale.set(s, s * .8, s); v.position.y = c[1] + (60 + 90 * ease((t - .2) / 2)) * S; });
      sparks(420, c, { speed: [500 * S, 1500 * S], life: [.5, 1.6], size: [4 * S, 1 * S], drag: 2, flat: .9, jit: 30 * S });
      if (E.rocks || el === 'fire' || el === 'earth') rockBurst(c, 14, 9 * S, 0);
      if (el === 'frost') shardRing(c, 14, 30 * S, true, 0);
      if (E.bolts) bl = [0, 1, 2, 3, 4, 5].map(i => [[c[0], c[1] + 40 * S, c[2]], [c[0] + Math.cos(i) * 220 * S, 0, c[2] + Math.sin(i) * 220 * S], .4]);
      scorch(c, 170 * S);
      fx.light = t => env(t, 0, .04, .3, 1.2) * 2.6;
    },
    'burst:nova': () => {   // a ring tearing outward along the ground from you, a flash at its heart
      fx.dur = 2.4;
      const c = [at[0], y + 8, at[2]];
      vol({ shape: 2, sp: [.3, .18, 2.2, 0], dens: 12, namp: .9, cut: .05, swirl: .4 }, v => placeVol(v, c, 100, 40, 100), (v, t) => {
        const k = ease(t / .9); v.u.life.value = env(t, 0, .06, .45, 1.1); v.u.sp.value.x = .15 + .7 * k; v.u.sp.value.y = .14 + .06 * k; const s = 280 * S; v.scale.set(s, 40 * S, s); });
      vol({ shape: 0, sp: [.9, 0, 0, 0], dens: 12, cut: .05 }, v => placeVol(v, [c[0], c[1] + 40 * S, c[2]], 50, 50), (v, t) => { v.u.life.value = env(t, 0, .03, .1, .4); const s = (30 + 60 * ease(t / .3)) * S; v.scale.set(s, s, s); });
      for (let i = 0; i < 260; i++) { const a = Math.random() * 6.283, sp = 700 * S + Math.random() * 500 * S; P.add1([c[0] + Math.cos(a) * 20, c[1] + 10 + Math.random() * 20, c[2] + Math.sin(a) * 20], [Math.cos(a) * sp, 40 + Math.random() * 120, Math.sin(a) * sp], Math.random() * .08, .5 + Math.random() * .4, 4 * S, 1 * S, 2.4, 1); }
      if (el === 'frost') { for (let k = 0; k < 3; k++) shardRing(c, 16, 22 * S, false, .12 + k * .14, (90 + k * 70) * S); }
      if (E.bolts) bl = 'nova';
      fx.light = t => env(t, 0, .03, .2, 1) * 2.2;
    },
    'burst:spikes': () => {   // it erupts: the element bursts up from the ground in a ring round you
      fx.dur = 2.8;
      const c = [at[0], y, at[2]], n = 7;
      for (let i = 0; i < n; i++) {
        const a = i / n * 6.283 + .3, r = (110 + (i % 2) * 50) * S, p = [c[0] + Math.cos(a) * r, c[1], c[2] + Math.sin(a) * r], t0 = .05 + i * .06;
        if (el === 'frost') { const s = add(Shard('frost', i * 13 + seed)); s.position.set(...p); s.rotation.set(Math.sin(a) * .35, a, -Math.cos(a) * .35); shards.push({ s, t0, h: (90 + (i % 3) * 35) * S, w: 16 * S, die: 1.8 }); }
        else if (E.rocks) { const s = add(new THREE.Mesh(crystalGeo(i * 7 + 3), ROCKMAT())); s.position.set(...p); s.rotation.set(Math.sin(a) * .3, a, -Math.cos(a) * .3); shards.push({ s, t0, h: (80 + (i % 3) * 30) * S, w: 22 * S, die: 2 }); }
        else vol({ shape: 1, sp: [.55, .12, 0, 0], dens: 11, rise: Math.max(1.4, E.rise * 2), cut: .05, swirl: E.swirl ? 1.5 : 0 }, v => placeVol(v, [p[0], p[1] + 70 * S, p[2]], 26 * S, 70 * S),
          (v, t) => { const k = env(t, t0, t0 + .1, t0 + .7, t0 + 1.3); v.u.life.value = k; const hh = (40 + 90 * ease((t - t0) / .35)) * S; v.scale.set(24 * S, hh, 24 * S); v.position.y = p[1] + hh + 3; });
        sparks(40, [p[0], p[1] + 10, p[2]], { dir: [0, 1, 0], spread: .3, speed: [300 * S, 800 * S], life: [.4, 1.1], size: [3 * S, 1 * S], drag: 2, t: t0, tw: .1 });
        if (E.bolts && i % 2 === 0) bl = bl || []; if (E.bolts) (bl = bl || []).push([[p[0], 0, p[2]], [p[0] + (Math.random() - .5) * 30, 160 * S, p[2]], .35]);
      }
      vol({ shape: 3, sp: [.95, 4, 0, 0], dens: 5, cut: .2, smoke: E.smoke || el === 'fire', gain: .7 }, v => placeVol(v, [c[0], c[1] + 30 * S, c[2]], 220 * S, 30 * S), (v, t) => { v.u.life.value = env(t, .1, .5, 1.6, 2.6) * .8; });
      fx.light = t => env(t, 0, .15, .9, 1.8) * 1.8;
    },
    // GROUND: what lingers where it struck
    'ground:mist': () => {   // a low blanket of the element rolling over the ground, lit from inside
      fx.dur = 3.6;
      const c = [at[0], y, at[2]];
      vol({ shape: 3, sp: [.95, 3.2, 0, 0], dens: 7, namp: 1.1, cut: .12, rise: .15, swirl: .15, smoke: E.smoke || el === 'shadowfire' ? true : undefined }, v => placeVol(v, [c[0], c[1] + 36 * S, c[2]], 200 * S, 36 * S),
        (v, t) => { v.u.life.value = env(t, 0, .6, 2.6, 3.5); const s = (120 + 80 * ease(t / 1.2)) * S; v.scale.set(s, 36 * S, s); });
      if (!E.smoke) vol({ shape: 3, sp: [.9, 4, 0, 0], dens: 6, cut: .25, rise: .4 }, v => placeVol(v, [c[0], c[1] + 24 * S, c[2]], 170 * S, 24 * S), (v, t) => { v.u.life.value = env(t, .1, .7, 2.5, 3.4) * .9; });
      for (let i = 0; i < 160; i++) { const a = Math.random() * 6.283, r = Math.random() * 170 * S; P.add1([c[0] + Math.cos(a) * r, c[1] + 4, c[2] + Math.sin(a) * r], [(Math.random() - .5) * 20, 30 + Math.random() * 70, (Math.random() - .5) * 20], Math.random() * 2.6, .9 + Math.random() * .8, 2.2 * S, .5 * S, .2, 0); }
      fx.light = t => env(t, 0, .6, 2.6, 3.5) * .9;
    },
    'ground:rune': () => {   // a circle of the element's runes burned into the ground, its motes rising
      fx.dur = 3.6;
      const c = [at[0], y, at[2]];
      disc = add(RuneDisc(el, 150 * S)); disc.position.set(c[0], c[1] + 1.2, c[2]);
      vol({ shape: 1, sp: [.95, .85, 0, 0], dens: 3, cut: .35, rise: .8 }, v => placeVol(v, [c[0], c[1] + 50 * S, c[2]], 140 * S, 50 * S), (v, t) => { v.u.life.value = env(t, .2, .8, 2.6, 3.4) * .7; });
      for (let i = 0; i < 220; i++) { const a = Math.random() * 6.283, r = (.4 + Math.random() * .55) * 140 * S; P.add1([c[0] + Math.cos(a) * r, c[1] + 4, c[2] + Math.sin(a) * r], [0, 60 + Math.random() * 90, 0], .2 + Math.random() * 2.6, .8 + Math.random(), 2 * S, .4 * S, .1, 0); }
      fx.light = t => env(t, 0, .5, 2.7, 3.5) * 1;
    },
    'ground:wave': () => {   // it spreads: a creeping ring of the element running outward across the ground, leaving patches
      fx.dur = 3.6;
      const c = [at[0], y, at[2]];
      vol({ shape: 2, sp: [.2, .12, 3.5, 0], dens: 9, namp: 1, cut: .08, rise: E.rise * .8 }, v => placeVol(v, [c[0], c[1] + 18 * S, c[2]], 240 * S, 20 * S),
        (v, t) => { const k = ease(t / 2.2); v.u.life.value = env(t, 0, .3, 2.2, 3.2); v.u.sp.value.x = .1 + .75 * k; v.u.sp.value.y = .1 + .05 * k; });
      for (let i = 0; i < 5; i++) { const a = i * 1.26 + .4, r = (60 + i * 25) * S;
        vol({ shape: 3, sp: [.9, 3, 0, 0], dens: 7, cut: .15 }, v => placeVol(v, [c[0] + Math.cos(a) * r, c[1] + 18 * S, c[2] + Math.sin(a) * r], 50 * S, 18 * S), (v, t) => { v.u.life.value = env(t, .3 + i * .25, .7 + i * .25, 2.6, 3.4) * .9; }); }
      if (el === 'frost') for (let k = 0; k < 2; k++) shardRing(c, 18, 12 * S, false, .3 + k * .5, (70 + k * 90) * S);
      fx.light = t => env(t, 0, .4, 2.4, 3.3) * 1;
    },
    // FROM ABOVE: a strike from the sky
    'above:sky': () => {   // it falls from the sky: lightning, a meteor of the element, a sunbeam - and lands
      fx.dur = 2.8;
      const c = [at[0], y, at[2]], top = [c[0] + 120 * S, 900, c[2] - 160 * S], T = .55;
      if (E.bolts) bl = 'sky';
      else if (E.rays || el === 'arcane' || el === 'spirit') vol({ shape: 4, sp: [.18, .32, 0, 0], dens: 14, cut: 0, rise: 3 }, v => placeVol(v, [c[0], 450, c[2]], 40 * S, 450), (v, t) => { v.u.life.value = env(t, .2, .35, .75, 1.3); v.u.sp.value.x = .1 + .2 * env(t, .2, .5, .6, 1.3); });
      else {
        const core = add(new THREE.Mesh(rockGeo(seed), el === 'frost' ? new THREE.MeshBasicMaterial({ color: v3c(E.c[1]) }) : ROCKMAT())); core.scale.setScalar(14 * S); rocks.push({ s: core, path: t => t < T ? [top[0] + (c[0] - top[0]) * t / T, top[1] + (c[1] + 10 - top[1]) * t / T, top[2] + (c[2] - top[2]) * t / T] : null });
        vol({ shape: 1, sp: [.25, .9, 0, 0], dens: 12, rise: 3, cut: .05 }, v => placeVol(v, top, 30 * S, 120 * S), (v, t) => {
          const k = Math.min(1, t / T); v.u.life.value = t < T ? 1 : 0; v.position.set(top[0] + (c[0] - top[0]) * k, top[1] + (c[1] - top[1]) * k + 100 * S, top[2] + (c[2] - top[2]) * k);
          v.rotation.z = Math.atan2(top[0] - c[0], top[1] - c[1]) * -1; v.rotation.x = Math.atan2(top[2] - c[2], top[1] - c[1]); });
      }
      vol({ shape: 0, sp: [.92, 0, 0, 0], dens: 12, cut: .03 }, v => placeVol(v, [c[0], c[1] + 30 * S, c[2]], 60, 60), (v, t) => { v.u.life.value = env(t, T, T + .04, T + .3, T + 1); const s = (30 + 110 * ease((t - T) / .6)) * S; v.scale.set(s, s * .8, s); });
      vol({ shape: 3, sp: [.95, 3, 0, 0], dens: 5, cut: .2, smoke: E.smoke || el === 'fire' || el === 'earth', gain: .7 }, v => placeVol(v, [c[0], c[1] + 40 * S, c[2]], 200 * S, 40 * S), (v, t) => { v.u.life.value = env(t, T + .1, T + .5, 1.8, 2.7) * .8; });
      sparks(320, [c[0], c[1] + 20, c[2]], { speed: [400 * S, 1300 * S], life: [.4, 1.3], size: [4 * S, 1 * S], drag: 2.2, flat: .8, t: T, jit: 20 * S });
      if (E.rocks || el === 'fire') rockBurst([c[0], c[1] + 10, c[2]], 12, 8 * S, T);
      if (el === 'frost') shardRing([c[0], c[1], c[2]], 12, 28 * S, true, T);
      scorch(c, 160 * S, T);
      fx.light = t => Math.max(env(t, .2, .4, .5, .6) * .6, env(t, T, T + .03, T + .3, T + 1.1) * 2.6);
    },
    'above:pillar': () => {   // a column of the element pours down on the spot and turns there
      fx.dur = 3;
      const c = [at[0], y, at[2]];
      vol({ shape: 1, sp: [.62, .9, 0, 0], dens: 9, namp: 1, cut: .06, swirl: 1, rise: -2.2 }, v => placeVol(v, [c[0], 380, c[2]], 70 * S, 380), (v, t) => {
        v.u.life.value = env(t, 0, .25, 1.6, 2.4); const w = (40 + 40 * env(t, 0, .3, 1.4, 2.4)) * S; v.scale.set(w, 380, w); });
      vol({ shape: 2, sp: [.4, .2, 2.5, 0], dens: 10, cut: .05, swirl: .8 }, v => placeVol(v, [c[0], c[1] + 14, c[2]], 160 * S, 20 * S), (v, t) => { v.u.life.value = env(t, .2, .4, 1.6, 2.4); v.u.sp.value.x = .3 + .4 * ease((t - .2) / 1.5); });
      for (let i = 0; i < 260; i++) { const a = Math.random() * 6.283, r = 30 * S + Math.random() * 50 * S, t0 = Math.random() * 1.6; P.add1([c[0] + Math.cos(a) * r, 40 + Math.random() * 500, c[2] + Math.sin(a) * r], [-Math.sin(a) * 160, -200 - Math.random() * 300, Math.cos(a) * 160], t0, .7, 3 * S, .8 * S, .3, E.spark === 'spark' ? 1 : 0); }
      if (E.bolts) bl = 'pillar';
      fx.light = t => env(t, 0, .25, 1.6, 2.4) * 1.8;
    },
  };
  function shardRing(c, n, sz, fly, t0, R) {
    for (let i = 0; i < n; i++) {
      const s = add(Shard('frost', seed * 31 + i)), a = i / n * 6.283 + (R ? 0 : Math.random());
      if (fly) { const d = new THREE.Vector3(Math.cos(a), .2 + Math.random() * .9, Math.sin(a)).normalize(); s.scale.set(sz * .25, sz, sz * .25); rocks.push({ s, fly: { p0: c, v: d.multiplyScalar(500 + Math.random() * 400), t0, spin: Math.random() * 8 } }); }
      else { s.position.set(c[0] + Math.cos(a) * R, c[1], c[2] + Math.sin(a) * R); s.rotation.set(Math.sin(a) * .5, a, -Math.cos(a) * .5); shards.push({ s, t0: t0 + Math.random() * .08, h: sz * (2.2 + Math.random() * 1.6), w: sz * .5, die: 2.2 }); }
    }
  }
  function rockBurst(c, n, sz, t0) {
    for (let i = 0; i < n; i++) { const s = add(new THREE.Mesh(rockGeo(seed + i * 5), ROCKMAT())); s.scale.setScalar(sz * (.5 + Math.random())); const a = Math.random() * 6.283;
      rocks.push({ s, fly: { p0: c, v: new THREE.Vector3(Math.cos(a) * (200 + Math.random() * 400), 300 + Math.random() * 500, Math.sin(a) * (200 + Math.random() * 400)), t0, spin: Math.random() * 10 } }); }
  }
  function scorch(c, R, t0) {   // (the ground burned/frozen/soaked where it struck: a dark stain fading)
    const m = new THREE.ShaderMaterial({ uniforms: { k: { value: 0 }, c1: { value: v3c(E.smoke ? E.c[1] : '#000000') } }, transparent: true, depthWrite: false,
      vertexShader: 'varying vec2 vUv; void main(){ vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.); }',
      fragmentShader: NOISE + 'uniform float k; uniform vec3 c1; varying vec2 vUv; void main(){ vec2 q = vUv * 2. - 1.; float r = length(q) + snoise(vec3(q * 4., 1.)) * .12; gl_FragColor = vec4(c1 * .4, smoothstep(1., .3, r) * .6 * k); }' });
    const d = add(new THREE.Mesh(new THREE.PlaneGeometry(R * 2, R * 2), m)); d.rotation.x = -Math.PI / 2; d.position.set(c[0], 1, c[2]);
    parts.push(t => { m.uniforms.k.value = env(t, (t0 || 0) + .05, (t0 || 0) + .2, fx.dur - .6, fx.dur); });
  }
  (D[role + ':' + opt] || D['impact:core'])();
  if (bl) { bolts = add(Bolts({ el, seed, width: 5 * S })); }
  const c0 = [at[0], y + H, at[2]];
  fx.f = t => {
    P.u.time.value = t; if (Pa) Pa.u.time.value = t;
    for (const { v, f } of vols) { v.u.time.value = t + v.u.seed.value; f(v, t); v.visible = v.u.life.value > .003; }
    for (const p of parts) p(t);
    for (const sh of shards) { const k = ease((t - sh.t0) / .18), die = env(t, sh.t0, sh.t0 + .01, sh.die, sh.die + .5); sh.s.visible = t > sh.t0 && die > 0; sh.s.scale.set(sh.w * (.6 + .4 * k), sh.h * k * die, sh.w * (.6 + .4 * k)); }
    for (const r of rocks) {
      if (r.path) { const p = r.path(t); r.s.visible = !!p; if (p) { r.s.position.set(...p); r.s.rotation.x = t * 6; } }
      else { const tt = t - r.fly.t0; r.s.visible = tt > 0 && tt < 1.6; if (tt > 0) { const p = r.fly.p0, v = r.fly.v; let yy = p[1] + v.y * tt - 490 * tt * tt; r.s.position.set(p[0] + v.x * tt, Math.max(4, yy), p[2] + v.z * tt); r.s.rotation.set(tt * r.fly.spin, tt * r.fly.spin * .7, 0); } }
    }
    if (disc) { disc.u.time.value = t; disc.u.on.value = env(t, 0, .4, fx.dur - .9, fx.dur - .2); }
    if (bolts) {
      let list = bl, on = 0;
      if (bl === 'sky') { list = [[[at[0] + 60, 900, at[2] - 90], [at[0], y + 2, at[2]], .3]]; on = env(t, .45, .5, .62, .75) + env(t, .8, .82, .9, .98); }
      else if (bl === 'nova') { const r = 40 + 300 * ease(t / .8) * S; list = [0, 1, 2, 3, 4, 5, 6, 7].map(i => [[at[0], y + 30, at[2]], [at[0] + Math.cos(i * .8) * r, y + 4, at[2] + Math.sin(i * .8) * r], .35]); on = env(t, 0, .03, .5, .9); }
      else if (bl === 'pillar') { list = [0, 1, 2].map(i => [[at[0] + Math.cos(t * 3 + i * 2) * 50, 700, at[2] + Math.sin(t * 3 + i * 2) * 50], [at[0] + Math.cos(t * 5 + i * 2) * 30, y + 2, at[2] + Math.sin(t * 5 + i * 2) * 30], .25]); on = env(t, .1, .3, 1.5, 2.2); }
      else { on = role === 'burst' ? env(t, 0, .03, .5, .9) : role === 'impact' ? env(t, 0, .02, .18, .35) : 0; }
      bolts.at(t, list, on);
    }
  };
  fx.lightAt = new THREE.Vector3(c0[0], c0[1] + 20, c0[2]);
  fx.col = E.light;
  return fx;
}

// ================================================================ a scene to play them in: night ground, a dead one, a slow orbit
function groundTex2() {
  const c = document.createElement('canvas'); c.width = c.height = 512; const x = c.getContext('2d');
  x.fillStyle = '#1d1a16'; x.fillRect(0, 0, 512, 512);
  const r = mulberry(5);
  for (let i = 0; i < 2600; i++) { const g = 20 + r() * 26 | 0; x.fillStyle = `rgba(${g + 8},${g + 4},${g},${.25 + r() * .4})`; x.fillRect(r() * 512, r() * 512, 2 + r() * 7, 2 + r() * 7); }
  for (let i = 0; i < 90; i++) { x.strokeStyle = `rgba(60,70,40,${.15 + r() * .2})`; x.beginPath(); const a = r() * 512, b = r() * 512; x.moveTo(a, b); x.lineTo(a + (r() - .5) * 14, b - 6 - r() * 10); x.stroke(); }
  const t = new THREE.CanvasTexture(c); t.wrapS = t.wrapT = THREE.RepeatWrapping; t.repeat.set(10, 10); t.encoding = THREE.sRGBEncoding; t.anisotropy = 4; return t;
}
const GROUND2 = GL.ok ? groundTex2() : null;
function fxStage(cv, o = {}) {
  const scene = new THREE.Scene(); scene.background = new THREE.Color(0x06080d); scene.fog = new THREE.FogExp2(0x06080d, .00035);
  const cam = new THREE.PerspectiveCamera(36, GL.W / GL.H, 2, 9000);
  scene.add(new THREE.HemisphereLight(0x5a6c96, 0x120e0a, .5));
  const moon = new THREE.DirectionalLight(0xb8c8ff, .55); moon.position.set(-500, 900, 400); scene.add(moon);
  const gm = new THREE.Mesh(new THREE.CircleGeometry(4000, 48), new THREE.MeshStandardMaterial({ map: GROUND2, roughness: .96 })); gm.rotation.x = -Math.PI / 2; scene.add(gm);
  const light = new THREE.PointLight(0xffffff, 0, 1100, 2); scene.add(light);
  const v = { cv, scene, cam, ready: false, visible: false, bloom: 1.15, fx: null, az: o.az || .6, el: o.el || .32, r: o.r || 640, tgt: new THREE.Vector3(0, o.ty || 60, 0), spin: o.spin !== false };
  let drag = null; cv.style.cursor = 'grab'; cv.style.touchAction = 'pan-y';
  cv.addEventListener('pointerdown', e => { drag = { x: e.clientX, y: e.clientY, az: v.az, el: v.el }; cv.setPointerCapture(e.pointerId); });
  cv.addEventListener('pointermove', e => { if (!drag) return; v.az = drag.az - (e.clientX - drag.x) * .008; v.el = Math.max(.02, Math.min(1.3, drag.el + (e.clientY - drag.y) * .006)); });
  cv.addEventListener('pointerup', () => drag = null); cv.addEventListener('pointercancel', () => drag = null);
  cv.addEventListener('wheel', e => { e.preventDefault(); v.r = Math.max(200, Math.min(2200, v.r * Math.exp(e.deltaY * .001))); }, { passive: false });
  v.play = fx => { if (v.fx) scene.remove(v.fx.g); v.fx = fx; scene.add(fx.g); light.color.set(fx.col || '#ffffff'); light.position.copy(fx.lightAt); v.t0 = null; v.ready = true; };
  v.tick = t => {
    if (v.spin && !drag) v.az += .0035;
    cam.position.set(v.tgt.x + Math.sin(v.az) * Math.cos(v.el) * v.r, v.tgt.y + Math.sin(v.el) * v.r, v.tgt.z + Math.cos(v.az) * Math.cos(v.el) * v.r); cam.lookAt(v.tgt);
    if (!v.fx) return; if (v.t0 == null) v.t0 = t;
    const tt = (t - v.t0) % (v.fx.dur + .5);
    v.fx.f(tt); light.intensity = v.fx.light ? v.fx.light(tt) * 2.4 : 0;
    if (v.onTick) v.onTick(tt);
  };
  return GL.add(v);
}

// ================================================================ a weapon's look, on its own model (Noah, Nov 26: "make sure the weapon look effects are
// made on the actual weapon models so it feels accurate and well made" / "360 so it doesn't look flat or like an outline at certain
// angles"): the effect is born on the model's own surface - every particle leaves a real point of the blade or the heads (where that
// look sits), the steel itself glows through a membrane laid on its own triangles, and the volume of light round it is drawn from
// every side. Nothing is a card, so no angle shows an edge.
const MEMVS = `uniform float thick; varying vec3 vN, vV, vP; void main(){ vec3 p = position + normal * thick; vP = position; vec4 w = modelMatrix * vec4(p, 1.);
  vN = normalize(mat3(modelMatrix) * normal); vV = normalize(cameraPosition - w.xyz); gl_Position = projectionMatrix * viewMatrix * w; }`;
const MEMFS = NOISE + `uniform vec3 c1, c2; uniform float time, k, flow, nsc; varying vec3 vN, vV, vP;
void main(){ float f = pow(1. - abs(dot(normalize(vN), vV)), 1.6);
  float n = fbm(vP * nsc + vec3(0., -time * flow, time * .3)) * .5 + .5;
  float m = smoothstep(.35, .9, n);
  vec3 col = mix(c1, c2, m * m) * (m * 1.3 + f * .9);
  gl_FragColor = vec4(col * k, 0.); }`;
function sampleSurface(d, region, rad, n) {   // n points on the model's own triangles, where the look sits (region: the look's own points)
  const tris = [];
  let total = 0;
  const R2 = rad * rad;
  const near = (x, y, z) => { for (let i = 0; i < region.length; i++) { const p = region[i], a = p[0] - x, b = p[1] - y, c = p[2] - z; if (a * a + b * b + c * c < R2) return true; } return false; };
  for (const { s, pos, idx } of d.mesh) {
    if (s.glow) continue;
    for (let i = 0; i < idx.length; i += 3) {
      const A = idx[i] * 3, B = idx[i + 1] * 3, C = idx[i + 2] * 3;
      const cx = (pos[A] + pos[B] + pos[C]) / 3, cy = (pos[A + 1] + pos[B + 1] + pos[C + 1]) / 3, cz = (pos[A + 2] + pos[B + 2] + pos[C + 2]) / 3;
      if (region.length && !near(cx, cy, cz)) continue;
      const ux = pos[B] - pos[A], uy = pos[B + 1] - pos[A + 1], uz = pos[B + 2] - pos[A + 2], vx = pos[C] - pos[A], vy = pos[C + 1] - pos[A + 1], vz = pos[C + 2] - pos[A + 2];
      const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx, ar = Math.hypot(nx, ny, nz) / 2;
      if (ar < 1e-6) continue;
      total += ar; tris.push({ pos, A, B, C, n: [nx / ar / 2, ny / ar / 2, nz / ar / 2], acc: total });
    }
  }
  const out = [], r = mulberry(91);
  if (!tris.length) return { pts: out, tris };
  for (let i = 0; i < n; i++) {
    const x = r() * total; let lo = 0, hi = tris.length - 1; while (lo < hi) { const m = (lo + hi) >> 1; if (tris[m].acc < x) lo = m + 1; else hi = m; }
    const t = tris[lo]; let a = r(), b = r(); if (a + b > 1) { a = 1 - a; b = 1 - b; }
    const p = t.pos; out.push({ p: [0, 1, 2].map(k => p[t.A + k] + (p[t.B + k] - p[t.A + k]) * a + (p[t.C + k] - p[t.A + k]) * b), n: t.n });
  }
  return { pts: out, tris };
}
function membraneGeo(tris) {
  const P = [], I = [];
  tris.forEach((t, i) => { for (const V of [t.A, t.B, t.C]) P.push(t.pos[V], t.pos[V + 1], t.pos[V + 2]); I.push(i * 3, i * 3 + 1, i * 3 + 2); });
  const g = new THREE.BufferGeometry(); g.setAttribute('position', new THREE.Float32BufferAttribute(P, 3)); g.setIndex(I);
  g.computeVertexNormals(); return g;
}
// a look: el (how it moves), cols (its two colours), strength 0..1.4, the model's span (for sizes)
function weaponAura(d, regionPts, o) {
  const E = ELEM[o.el] || ELEM.fire, g = new THREE.Group(), span = o.span || 100, k = o.k == null ? 1 : o.k;
  const rise = o.rise != null ? o.rise : E.rise, pk = o.pk || E.spark, pn = o.pn == null ? 1 : o.pn, memK = o.mem == null ? 1 : o.mem, volK = o.vol == null ? 1 : o.vol;
  const { pts, tris } = sampleSurface(d, regionPts, span * .045, 2400);
  const c1 = o.c1 || E.c[1], c2 = o.c2 || E.c[2], c0 = o.c0 || E.c[0];
  if (!pts.length) { g.tick = () => {}; return g; }
  // the steel's own glow: a skin of light on its own triangles, flowing through it (never a card: it IS the blade)
  const mu = { thick: { value: span * .0022 }, c1: { value: v3c(c1) }, c2: { value: v3c(c2) }, time: { value: 0 }, k: { value: .5 * k * memK }, flow: { value: rise > .5 ? 1.4 : .5 }, nsc: { value: (o.runes ? 22 : 12) / span } };
  const mem = new THREE.Mesh(membraneGeo(tris), new THREE.ShaderMaterial({ uniforms: mu, vertexShader: MEMVS, fragmentShader: MEMFS, transparent: true, depthWrite: false, side: THREE.DoubleSide, ...ADD() }));
  mem.layers.enable(1); mem.renderOrder = 3; g.add(mem);
  // what leaves it: particles born on the surface all round it, rising or drifting in the world (not the blade's) up
  const n = Math.round((160 + 900 * Math.min(1.4, k)) * pn), wisp = pk === 'wisp' || pk === 'smoke';
  const P = Particles({ el: o.el, cap: n + 10, loop: true, c: [c0, c1, c2], gain: o.dark ? 1.6 : 2.2, turb: span * (wisp ? .1 : .035) });
  const sz = span * (pk === 'mote' ? .011 : wisp ? .02 : .015) * (.7 + .5 * Math.min(1, k));
  for (let i = 0; i < n; i++) {
    const s = pts[(i * 7919) % pts.length];
    const out = span * (.03 + .12 * Math.random()) * (rise > .5 ? .4 : 1);
    P.add1([s.p[0] + s.n[0] * span * .004, s.p[1] + s.n[1] * span * .004, s.p[2] + s.n[2] * span * .004], [s.n[0] * out, s.n[1] * out, s.n[2] * out], Math.random() * 3,
      .45 + Math.random() * (wisp ? 1.5 : .9), sz * (.6 + Math.random() * .9), sz * .2, 1.2, pk === 'spark' || pk === 'streak' ? 1 : 0);
  }
  P.renderOrder = 6; g.add(P);
  // the volume of its light (or its smoke), round each part of the look on its own - both heads of a quarterstaff, never the
  // handle between them - the same from every side
  const vols = [];
  if (k * volK > .55) {
    let lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9]; for (const p of pts) for (let a = 0; a < 3; a++) { lo[a] = Math.min(lo[a], p.p[a]); hi[a] = Math.max(hi[a], p.p[a]); }
    const ax = [0, 1, 2].sort((x, y) => (hi[y] - lo[y]) - (hi[x] - lo[x]))[0], ext = hi[ax] - lo[ax];
    const sorted = pts.slice().sort((x, y) => x.p[ax] - y.p[ax]), groups = [[]];
    for (let i = 0; i < sorted.length; i++) { if (i && sorted[i].p[ax] - sorted[i - 1].p[ax] > ext * .08) groups.push([]); groups[groups.length - 1].push(sorted[i]); }
    for (const gp of groups) {
      if (gp.length < 30) continue;
      let l = [1e9, 1e9, 1e9], h = [-1e9, -1e9, -1e9]; for (const p of gp) for (let a = 0; a < 3; a++) { l[a] = Math.min(l[a], p.p[a]); h[a] = Math.max(h[a], p.p[a]); }
      const v = Volume({ el: o.el, shape: 0, sp: [.95, 0, 0, 0], dens: (o.dark ? 3.2 : 1.6) * Math.min(1.3, k) * volK, cut: .34, namp: 1.25, gain: .45, smoke: false, dark: !!o.dark, nsc: 3.2, rise: rise * .6 });
      v.u.c0.value = v3c(c0); v.u.c1.value = v3c(c1); v.u.c2.value = v3c(c2);
      const pad = span * .045;
      v.position.set((l[0] + h[0]) / 2, (l[1] + h[1]) / 2, (l[2] + h[2]) / 2);
      v.scale.set((h[0] - l[0]) * .6 + pad, (h[1] - l[1]) * .6 + pad, (h[2] - l[2]) * .6 + pad);
      g.add(v); vols.push(v);
    }
  }
  let bolts = null;
  if (E.bolts && pts.length > 20) { bolts = Bolts({ el: o.el, width: span * .006, gain: 2.4 * k }); g.add(bolts); }
  const worldUp = new THREE.Vector3(), q = new THREE.Quaternion();
  g.tick = (t, holder) => {
    mu.time.value = t; P.u.time.value = t;
    if (o.sweep) mu.k.value = .5 * k * memK * (.6 + .8 * Math.max(0, Math.sin(t * 1.4)));
    if (holder) { holder.getWorldQuaternion(q); worldUp.set(0, 1, 0).applyQuaternion(q.invert()); P.u.grav.value.copy(worldUp).multiplyScalar(span * (rise > .5 ? .9 * rise : pk === 'snow' || pk === 'drip' ? -.5 : .08 + rise * .3)); }
    for (const vol of vols) { vol.u.time.value = t; vol.u.life.value = .8 + .2 * Math.sin(t * 2.3); }
    if (bolts) { const r = mulberry(Math.floor(t * 9)); const a = pts[Math.floor(r() * pts.length)].p, b = pts[Math.floor(r() * pts.length)].p; bolts.at(t, [[a, b, .4]], .8); }
  };
  g.mem = mem; g.P = P;
  return g;
}
