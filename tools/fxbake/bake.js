// (Noah, Nov 27 test of 3edd0b7: "I want the preview effects in this workshop to be exactly how it shows in game if not better") the
// workshop's own effects (docs/workshop/v2/vfx.js makeEffect - the very shaders the preview plays) rendered frame by frame on black,
// the preview's bloom laid over each, for the game's flipbooks (tools/fxbake.py). Loaded after three.js r128, viewer.js and vfx.js.
// window.bakeFrames(job) -> { frames: [png data URLs], dur }
(function () {
  let R = null, rt = null, quad = null, qcam = null, blurM = null, addM = null, W = 0, H = 0;
  function setup(w, h) {
    if (R && w === W && h === H) return;
    W = w, H = h;
    const c = document.createElement('canvas');
    R = new THREE.WebGLRenderer({ canvas: c, antialias: true, preserveDrawingBuffer: true, alpha: false });
    R.setPixelRatio(1); R.setSize(W, H, false); R.outputEncoding = THREE.sRGBEncoding;
    R.toneMapping = THREE.ACESFilmicToneMapping; R.toneMappingExposure = 1.05; R.autoClear = false;
    const mk = (a, b) => new THREE.WebGLRenderTarget(a, b, { minFilter: THREE.LinearFilter, magFilter: THREE.LinearFilter, type: THREE.HalfFloatType });
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
  // the preview's own picture (viewer.js draw): the scene, then its glow blurred at half and quarter size and added back
  function draw(scene, cam, bloom) {
    R.setRenderTarget(null); R.setClearColor(0x000000, 1); R.clear();
    cam.layers.mask = 3; R.render(scene, cam);
    R.setClearColor(0x000000, 1); cam.layers.mask = 2;
    R.setRenderTarget(rt.a); R.clear(); R.render(scene, cam);
    cam.layers.mask = 3;
    const s = Math.max(W, H) / 720;   // (the preview's blur widths are for its 720 px picture)
    blurM.uniforms.t.value = rt.a.texture; blurM.uniforms.dir.value.set(1.4 * s / (W / 2), 0); pass(blurM, rt.b);
    blurM.uniforms.t.value = rt.b.texture; blurM.uniforms.dir.value.set(0, 1.4 * s / (H / 2)); pass(blurM, rt.a);
    blurM.uniforms.t.value = rt.a.texture; blurM.uniforms.dir.value.set(2.2 * s / (W / 4), 0); pass(blurM, rt.c);
    blurM.uniforms.t.value = rt.c.texture; blurM.uniforms.dir.value.set(0, 2.2 * s / (H / 4)); pass(blurM, rt.d);
    blurM.uniforms.t.value = rt.d.texture; blurM.uniforms.dir.value.set(3.0 * s / (W / 4), 0); pass(blurM, rt.c);
    blurM.uniforms.t.value = rt.c.texture; blurM.uniforms.dir.value.set(0, 3.0 * s / (H / 4)); pass(blurM, rt.d);
    addM.uniforms.a.value = rt.a.texture; addM.uniforms.c.value = rt.d.texture; addM.uniforms.k.value = bloom;
    quad.material = addM; R.setRenderTarget(null); R.render(quad, qcam);
  }
  // job: { el, role, opt, size, frames, w, h, box: [cx, cy, bw, bh] (workshop units: the picture's middle and its width and height),
  //        elev (radians the camera looks down), bloom }
  window.bakeFrames = function (job) {
    setup(job.w, job.h);
    R.toneMappingExposure = job.exposure == null ? 1.05 : job.exposure;   // (the preview's own 1.05, unless the job dims it)
    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x000000);
    const fx = makeEffect(job.el, job.role, job.opt, { at: [0, 0, 0], size: job.size || 1, seed: job.seed || 11 });
    scene.add(fx.g);
    // (a near-orthographic view: far off along a narrow lens, so the volumes' rays come from a real eye and the card in the game
    // can be any size)
    const [cx, cy, bw, bh] = job.box, elev = job.elev == null ? .22 : job.elev;
    const fov = 8, dist = (bh / 2) / Math.tan(fov * Math.PI / 360);
    const cam = new THREE.PerspectiveCamera(fov, job.w / job.h, Math.max(2, dist - 4000), dist + 4000);
    cam.position.set(cx, cy + Math.sin(elev) * dist, Math.cos(elev) * dist);
    if (elev > 1.5) cam.up.set(0, 0, -1);   // (straight down: the picture's top is the far side)
    cam.lookAt(cx, cy, 0);
    const out = [], span = job.tmax || fx.dur;   // (tmax: where the last of it fades - the frames spread over what is seen)
    for (let i = 0; i < job.frames; i++) {
      const t = (i + .5) / job.frames * span;
      fx.f(t);
      draw(scene, cam, job.bloom == null ? 1.15 : job.bloom);
      out.push(R.domElement.toDataURL('image/png'));
    }
    return { frames: out, dur: span };
  };
  window.bakeReady = true;
})();
