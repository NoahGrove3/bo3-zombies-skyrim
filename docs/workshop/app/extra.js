// ================================================================ the master doc's other tabs: hit feel, the dead, the trials, big nights, issues
// (Noah, 2026-11-25: "Treat this document as the master document for our mod, where we can tinker, build, preview, test")
const playRefs = (refs, opt) => { AUD.ac(); const sl = { opts: [{ id: 'x', snd: refs.map(r => [r, 1, 0]), loop: !!(opt && opt.loop) }] }; AUD.slot(sl, Object.assign({ opt: 'x', vol: 100, pitch: 100, varn: 0, layer: 'none', layerVol: 0 }, opt || {}), opt && opt.loop ? 3 : 0); };
const fsl = (k, n, lo, hi, step, v, unit, help) => `<label class="sl" title="${esc(help || '')}"><span>${n}</span><input type="range" data-k="${k}" min="${lo}" max="${hi}" step="${step}" value="${v}" aria-label="${n}"><b class="v">${v}${unit}</b></label>`;
function optBtns(list, cur, rec, attr) { return list.map(([id, n, d]) => `<button class="opt" data-${attr}="${id}" aria-pressed="${cur === id}"><b>${esc(n)}${rec === id ? ' <span class="tag ok">my pick</span>' : ''}${id === 'a1' || id === 'keep' ? ' <span class="tag">now</span>' : ''}</b>${d ? `<span>${esc(d)}</span>` : ''}</button>`).join(''); }

// ---------------------------------------------------------------- hit feel
(function () {
  const grid = $('#feelgrid'), st = $('#feelstatus');
  $('#feelsub').innerHTML = 'How a hit <b>feels</b>: a <b>hit-stop</b> (the picture holds for a few milliseconds as the blade bites) and a <b>camera shake</b> on power attacks and kills - by weapon weight. Open one to see and hear it on the dead: the preview holds and shakes exactly as set (with your sound picks when you press Hear it play). Below, the hit markers and kill sounds. The game has none of this yet: every option but the first is new.';
  const K = { stopL: ['Hit-stop, a hit', 0, 150, 5, 'ms'], stopH: ['Hit-stop, a heavy', 0, 250, 5, 'ms'], shakeH: ['Shake, a heavy', 0, 100, 5, '%'], shakeK: ['Shake, a kill', 0, 60, 5, '%'] };
  const setOf = c => { const s = DB.get('feel', c.id) || {}; const o = c.opts.find(x => x[0] === (s.opt || 'a1')) || c.opts[0]; return Object.assign({ opt: o[0] }, o[3], s); };
  grid.innerHTML = FEEL.classes.map(c => `<article class="wcard" data-id="${c.id}">
<div class="stage"><canvas aria-label="${esc(c.name)}, live"></canvas><div class="ld">${GL.ok ? '' : 'Live previews need WebGL'}</div><div class="ov"><span class="ovl"></span><span class="ovr"></span></div><button class="hear" aria-pressed="false">Hear it play</button></div>
<div class="wbody"><div class="t"><h3>${esc(c.name)}</h3><span class="saved"></span></div><p class="now">${esc(c.who)}</p>
<div class="fbody"><p class="why"><b>My pick: ${esc(c.opts.find(o => o[0] === c.rec)[1])}.</b> ${esc(c.why)}</p><div class="opts fopts"></div><div class="sliders"></div>
<textarea rows="1" placeholder="A note for me (optional)" aria-label="Note on ${esc(c.name)}"></textarea></div></div></article>`).join('') +
    `<div class="card" style="grid-column:1/-1"><div class="k">Hit markers and kill feedback</div><div style="display:grid;gap:12px" id="marks"></div></div>`;
  const cards = {};
  grid.querySelectorAll('.wcard').forEach(el => {
    const cl = FEEL.classes.find(x => x.id === el.dataset.id), c = { el, cl, hear: false }; cards[cl.id] = c;
    whenNear(el, () => { if (!GL.ok) return; c.view = abilityView($('canvas', el)); let lastK = '';
      c.view.onBeat = b => { const s = setOf(cl);
        if (b.k === 'hit') { const heavy = lastK === 'heavy'; c.view.hitstop(heavy ? s.stopH : s.stopL); if (heavy) c.view.shake(s.shakeH); }
        if (cl.id === 'magic' && b.k === 'impact') { c.view.hitstop(s.stopL); c.view.shake(s.shakeH * 0.7, 0.35); }
        if (b.k !== 'impact') lastK = b.k;
        if (c.hear && window.sndBeat) sndBeat(cl.item, cl.key, b); };
      play(c); });
    paint(c);
  });
  function play(c) { if (!c.view) return; const a = abArgs(c.cl.key); if (!a) return; c.view.key = c.cl.key; c.view.play(...a); const s = setOf(c.cl); $('.ovl', c.el).textContent = `stop ${s.stopL} / ${s.stopH} ms · shake ${s.shakeH}%`; }
  function paint(c) {
    const s = setOf(c.cl), el = c.el, saved = DB.get('feel', c.cl.id);
    $('.fopts', el).innerHTML = optBtns(c.cl.opts.map(o => [o[0], o[1], o[2]]), s.opt, c.cl.rec, 'o');
    $('.sliders', el).innerHTML = Object.entries(K).map(([k, [n, lo, hi, step, u]]) => fsl(k, n, lo, hi, step, s[k], u)).join('');
    $('.saved', el).textContent = saved ? 'Saved: ' + (c.cl.opts.find(o => o[0] === s.opt) || [])[1] : '';
    el.classList.toggle('changed', !!saved && s.opt !== 'a1');
    const ta = $('textarea', el); if (document.activeElement !== ta) ta.value = saved && saved.note || '';
    if (c.view) $('.ovl', el).textContent = `stop ${s.stopL} / ${s.stopH} ms · shake ${s.shakeH}%`;
  }
  async function save(c, patch) { const row = Object.assign({ id: c.cl.id, name: c.cl.name }, setOf(c.cl), patch); try { await DB.set('feel', c.cl.id, row); saveMsg(st, null); } catch (e) { saveMsg(st, e); } paint(c); }
  grid.addEventListener('click', e => { const el = e.target.closest('.wcard'); if (!el) return; const c = cards[el.dataset.id], b = e.target.closest('button'); if (!b) return;
    if (b.classList.contains('hear')) { c.hear = !c.hear; b.setAttribute('aria-pressed', String(c.hear)); AUD.ac(); return; }
    if (b.dataset.o) { const o = c.cl.opts.find(x => x[0] === b.dataset.o); save(c, Object.assign({ opt: o[0] }, o[3])); } });
  grid.addEventListener('input', e => { if (e.target.type === 'range') { const k = K[e.target.dataset.k]; e.target.nextElementSibling.textContent = e.target.value + k[4]; } });
  grid.addEventListener('change', e => { const el = e.target.closest('.wcard'); if (!el) return; const c = cards[el.dataset.id];
    if (e.target.type === 'range') save(c, { [e.target.dataset.k]: +e.target.value }); else if (e.target.tagName === 'TEXTAREA') save(c, { note: e.target.value.slice(0, 2000) }); });
  window.addEventListener('detailclosed', e => { const c = Object.values(cards).find(x => x.el === e.detail); if (c && c.hear) { c.hear = false; $('.hear', c.el).setAttribute('aria-pressed', 'false'); } });
  // the hit markers
  const mk = $('#marks');
  function paintMarks() { mk.innerHTML = FEEL.marks.map(m => { const s = DB.get('feel', 'mark_' + m.id) || {}; const cur = s.choice || 'keep';
    return `<div class="mrow" data-m="${m.id}"><div class="row" style="justify-content:space-between"><b>${esc(m.name)}</b>${m.snd ? `<button class="play" data-p="${m.id}" aria-label="Hear ${esc(m.name)}">▶</button>` : ''}</div><p class="note" style="margin:2px 0 6px">Now: ${esc(m.now)}</p>
<div class="row">${m.opts.map(([id, n]) => `<button class="chip" data-c="${id}" aria-pressed="${cur === id}">${esc(n)}${m.rec === id ? ' · my pick' : ''}</button>`).join('')}</div></div>`; }).join(''); }
  paintMarks();
  mk.addEventListener('click', async e => { const r = e.target.closest('.mrow'); if (!r) return; const m = FEEL.marks.find(x => x.id === r.dataset.m), b = e.target.closest('button'); if (!b) return;
    if (b.dataset.p) { playRefs([m.snd], { pitch: m.pitch || 100 }); return; }
    if (b.dataset.c) { try { await DB.set('feel', 'mark_' + m.id, { id: 'mark_' + m.id, name: m.name, choice: b.dataset.c }); saveMsg(st, null); } catch (er) { saveMsg(st, er); } paintMarks(); } });
  DB.on('feel', (d, err) => { if (err) { st.textContent = err === 'offline' ? 'You can try every setting here; saving works when this page is opened in Claude.' : 'Your picks could not be loaded.'; return; } st.textContent = ''; Object.values(cards).forEach(paint); paintMarks(); });
})();

// ---------------------------------------------------------------- the dead: how they die, by what kills them
(function () {
  const grid = $('#engrid'), st = $('#enstatus');
  $('#ensub').innerHTML = 'How the Thriceborn die, by what kills them. Today fire, frost and lightning use the Frozen Electrocuted Combusted mod\'s bodies and everything else falls the same. Open one to watch it on the dead - each option its real parts from your game, its sound with <b>Hear it play</b>. Deaths that leave no body (ash, shards, melt) also lighten big nights.';
  const setOf = d => Object.assign({ opt: 'a1' }, DB.get('deaths', d.id) || {});
  grid.innerHTML = DEATHS.map(d => `<article class="wcard" data-id="${d.id}">
<div class="stage"><canvas aria-label="${esc(d.name)}, live"></canvas><div class="ld">${GL.ok ? '' : 'Live previews need WebGL'}</div><div class="ov"><span class="ovl"></span><span class="ovr"></span></div><button class="hear" aria-pressed="false">Hear it play</button></div>
<div class="wbody"><div class="t"><h3>${esc(d.name)}</h3><span class="saved"></span></div><p class="now">${esc(d.who)}</p>
<div class="fbody"><p class="why"><b>My pick: ${esc(d.opts.find(o => o.id === d.rec).name)}.</b> ${esc(d.why)}</p><div class="sopts dopts"></div>
<textarea rows="1" placeholder="A note for me (optional)" aria-label="Note on ${esc(d.name)}"></textarea></div></div></article>`).join('');
  const cards = {};
  grid.querySelectorAll('.wcard').forEach(el => {
    const d = DEATHS.find(x => x.id === el.dataset.id), c = { el, d, hear: false }; cards[d.id] = c;
    whenNear(el, () => { if (!GL.ok) return; c.view = abilityView($('canvas', el));
      c.view.onBeat = b => { if (c.hear && b.k === 'impact' && b.t < 0.2) { const o = c.d.opts.find(x => x.id === setOf(c.d).opt); playRefs(o.snd.slice(0, 1)); if (o.snd[1]) setTimeout(() => playRefs([o.snd[1]], { vol: 70 }), 380); } };
      play(c); });
    paint(c);
  });
  function play(c, optId) {
    if (!c.view) return; const o = c.d.opts.find(x => x.id === (optId || setOf(c.d).opt)) || c.d.opts[0], key = `death_${c.d.id}_${o.id}`;
    REACT[key] = o.rx.map(([t, k, col]) => ['t0', t, k, col ? ZBURN(col) : null]);
    const rows = o.parts.map(([th, role, t, s, gr]) => { const q = (REALFX.parts[th] || {})[role]; return q ? { t, at: gr ? 't0g' : 't0', parts: [{ model: q.model.toLowerCase(), s: q.s * s, delay: 0, ground: !!gr, pal: null }] } : null; }).filter(Boolean);
    if (!rows.length) rows.push({ t: 0.05, at: 't0', parts: [] });
    c.view.key = key; c.view.play('burst', '#ffffff', 100, 'hit', { show: rows, mod: {} });
    $('.ovl', c.el).textContent = o.name;
  }
  function paint(c) {
    const s = setOf(c.d), el = c.el, saved = DB.get('deaths', c.d.id);
    $('.dopts', el).innerHTML = c.d.opts.map(o => `<div class="sopt"><button class="play" data-p="${o.id}" aria-label="Hear ${esc(o.name)}">▶</button><button class="opt" data-o="${o.id}" aria-pressed="${s.opt === o.id}"><b>${esc(o.name)}${c.d.rec === o.id ? ' <span class="tag ok">my pick</span>' : ''}${o.id === 'a1' ? ' <span class="tag">in the game now</span>' : ''}</b><span>${esc(o.desc)}</span>${o.game ? `<span class="srcs"><i>${esc(o.game)}</i></span>` : ''}</button></div>`).join('');
    $('.saved', el).textContent = saved ? 'Saved: ' + (c.d.opts.find(o => o.id === s.opt) || {}).name : '';
    el.classList.toggle('changed', !!saved && s.opt !== 'a1');
    const ta = $('textarea', el); if (document.activeElement !== ta) ta.value = saved && saved.note || '';
  }
  async function save(c, patch) { const row = Object.assign({ id: c.d.id, name: c.d.name }, setOf(c.d), patch); row.optName = (c.d.opts.find(o => o.id === row.opt) || {}).name; try { await DB.set('deaths', c.d.id, row); saveMsg(st, null); } catch (e) { saveMsg(st, e); } paint(c); }
  grid.addEventListener('click', e => { const el = e.target.closest('.wcard'); if (!el) return; const c = cards[el.dataset.id], b = e.target.closest('button'); if (!b) return;
    if (b.classList.contains('hear')) { c.hear = !c.hear; b.setAttribute('aria-pressed', String(c.hear)); AUD.ac(); return; }
    if (b.dataset.p) { const o = c.d.opts.find(x => x.id === b.dataset.p); playRefs(o.snd.slice(0, 1)); if (o.snd[1]) setTimeout(() => playRefs([o.snd[1]], { vol: 70 }), 380); return; }
    if (b.dataset.o) { save(c, { opt: b.dataset.o }); play(c, b.dataset.o); } });
  grid.addEventListener('change', e => { if (e.target.tagName !== 'TEXTAREA') return; const c = cards[e.target.closest('.wcard').dataset.id]; save(c, { note: e.target.value.slice(0, 2000) }); });
  window.addEventListener('detailclosed', e => { const c = Object.values(cards).find(x => x.el === e.detail); if (c && c.hear) { c.hear = false; $('.hear', c.el).setAttribute('aria-pressed', 'false'); } });
  DB.on('deaths', (d, err) => { if (err) { st.textContent = err === 'offline' ? 'You can watch every death here; saving works when this page is opened in Claude.' : 'Your picks could not be loaded.'; return; } st.textContent = ''; Object.values(cards).forEach(c => { paint(c); play(c); }); });
})();

// ---------------------------------------------------------------- the Echo Altar and its trials, in 3D
function trialView(cv) {
  const scene = new THREE.Scene(); scene.background = new THREE.Color(0x060810); scene.fog = new THREE.FogExp2(0x060810, 0.00018);
  const cam = new THREE.PerspectiveCamera(40, GL.W / GL.H, 10, 30000);
  const tgt = new THREE.Vector3(0, 60, 0); orbitCam(cv, cam, tgt, { az: 0.6, el: 0.36, r: 2500 });
  scene.add(new THREE.HemisphereLight(0x6f84b8, 0x14110c, 0.5));
  const moon = new THREE.DirectionalLight(0xbcd0ff, 0.7); moon.position.set(-1600, 2400, 1200); scene.add(moon);
  const gm = new THREE.Mesh(new THREE.CircleGeometry(9000, 64), new THREE.MeshStandardMaterial({ map: groundTex(), roughness: 0.95 })); gm.rotation.x = -Math.PI / 2; scene.add(gm);
  if (gm.material.map) gm.material.map.repeat.set(12, 12);
  const v = { cv, scene, cam, ready: true, visible: false, bloom: 1.25, trial: null, set: null, onEvent: null };
  // the altar itself (our shrine model)
  const altar = new THREE.Group(); scene.add(altar);
  loadWeapon('altar').then(d => { const m = weaponModel(d).g; m.scale.setScalar(d.scale || 1); m.rotation.x = -Math.PI / 2; altar.add(m);
    const b = new THREE.Box3().setFromObject(m); m.position.y -= b.min.y; }).catch(e => console.warn('altar', e));
  const stoneLight = new THREE.PointLight(0x9fd0ff, 1.2, 2600, 2); stoneLight.position.set(0, 220, 0); scene.add(stoneLight);
  // the ring of light on the ground, and its soft fill
  const ringMat = new THREE.ShaderMaterial({ transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, uniforms: { col: { value: new THREE.Color('#9fd0ff') }, t: { value: 0 }, a: { value: 1 } },
    vertexShader: 'varying vec2 vU; void main(){ vU = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.); }',
    fragmentShader: `uniform vec3 col; uniform float t, a; varying vec2 vU; void main(){ float ang = atan(vU.y-.5, vU.x-.5); float rr = length(vU-.5)*2.;
      float band = smoothstep(.955,.985,rr)*(1.-smoothstep(.985,1.,rr)); float rune = .55+.45*step(.5,fract(ang*24./6.2832 + t*.08))*step(.3,fract(ang*96./6.2832));
      float inner = smoothstep(.55,.985,rr)*(1.-smoothstep(.985,1.,rr))*.16; gl_FragColor = vec4(col*(band*rune*1.6+inner), (band*rune+inner)*a); }` });
  const ring = new THREE.Mesh(new THREE.PlaneGeometry(2, 2), ringMat); ring.rotation.x = -Math.PI / 2; ring.position.y = 3; ring.layers.enable(1); scene.add(ring);
  const rl = new THREE.PointLight(0x9fd0ff, 0, 2600, 2); rl.position.set(0, 150, 0); scene.add(rl);
  // souls: small bright flames rising
  const soulTex = (() => { const c = document.createElement('canvas'); c.width = c.height = 64; const g = c.getContext('2d'), gr = g.createRadialGradient(32, 32, 0, 32, 32, 32); gr.addColorStop(0, 'rgba(255,255,255,1)'); gr.addColorStop(0.3, 'rgba(255,255,255,.6)'); gr.addColorStop(1, 'rgba(255,255,255,0)'); g.fillStyle = gr; g.fillRect(0, 0, 64, 64); return new THREE.CanvasTexture(c); })();
  const souls = []; for (let i = 0; i < 8; i++) { const sp = new THREE.Sprite(new THREE.SpriteMaterial({ map: soulTex, color: 0xbfe6ff, blending: THREE.AdditiveBlending, depthWrite: false, transparent: true })); sp.scale.set(70, 110, 1); sp.visible = false; sp.layers.enable(1); scene.add(sp); souls.push(sp); }
  // seals: three rune discs standing up, for the seals trial
  const seals = [0, 1, 2].map(i => { const m = new THREE.Mesh(new THREE.CircleGeometry(70, 32), ringMat.clone()); m.material.uniforms.col.value = new THREE.Color('#ff8a6a'); m.position.set(Math.cos(i * 2.1 + 0.4) * 1500, 110, Math.sin(i * 2.1 + 0.4) * 1500); m.lookAt(0, 110, 0); m.layers.enable(1); scene.add(m); return m; });
  // the dead and you
  const zs = []; const N = 6;
  ZOM_KEYS.slice(0, N).forEach((key, i) => zomData(key).then(([sk, body]) => { const z = makeZombie(sk, body, (i * 0.37 + 0.11) % 1); zs[i] = z; scene.add(z.holder); }).catch(() => {}));
  let plr = null; zomData('summoner', 'plr').then(([sk, body]) => { plr = makeZombie(sk, body, 0.5); scene.add(plr.holder); }).catch(() => {});
  let lastT = null, fired = {};
  v.tick = t => {
    const tr = v.trial, s = v.set || {}; if (!tr) return;
    const R = s.radius || 1400, L = 9, tt = t % L; if (lastT != null && tt < lastT) fired = {}; lastT = tt;
    const ev = (id, at) => { if (tt >= at && !fired[id + at]) { fired[id + at] = 1; if (v.onEvent) v.onEvent(id); } };
    ringMat.uniforms.t.value = t; ringMat.uniforms.col.value.set(s.ring || '#9fd0ff'); rl.color.set(s.ring || '#9fd0ff'); stoneLight.color.set(s.ring || '#9fd0ff');
    souls.forEach(sp => { sp.material.color.set(s.soul || '#bfe6ff'); sp.visible = false; });
    seals.forEach(m => m.visible = tr === 'seals');
    let rr = R, ringAt = new THREE.Vector3(0, 3, 0), stone = 0.6;
    const pinfo = { home: new THREE.Vector3(-R * 0.35, 0, R * 0.2), face: 0.9 };
    if (tr === 'echo') {   // the ring closing on the stone; you step out at 4 s and it slips back
      const out = tt > 4 && tt < 5.2, k = Math.min(1, tt / L);
      rr = R * (1 - 0.85 * k) + (out ? R * 0.15 * (tt - 4) : 0) * 0 + (tt > 5.2 ? R * 0.12 : 0);
      pinfo.home.set(out ? rr + 160 : rr * 0.5, 0, 0); pinfo.face = -Math.PI / 2;
      ev('sing', 0.05); ev('inside', 0.3); ev('slip', 4.05); stone = 0.5 + k;
    } else if (tr === 'whisper') { ringAt.set(900, 3, -600); rr = R; ev('found', 0.3); }
    ring.position.set(ringAt.x, 3, ringAt.z); ring.scale.set(rr, rr, 1); rl.position.set(ringAt.x, 150, ringAt.z); rl.intensity = 1.2;
    // the dead: walking in from outside toward the ring's middle; in restless/whisper each falls in turn inside the light, its soul rising
    for (let i = 0; i < N; i++) {
      const z = zs[i]; if (!z) continue;
      const a = i * (6.2832 / N) + 0.3, d0 = R * 1.6, start = (i * 1.1) % 3, die = 2.2 + i * 1.0;
      const k = Math.max(0, Math.min(1, (tt - start) / 3.2)), dist = d0 - (d0 - R * 0.55) * k;
      const home = new THREE.Vector3(ringAt.x + Math.cos(a) * dist, 0, ringAt.z + Math.sin(a) * dist);
      const face = Math.atan2(-(ringAt.x - home.x), -(ringAt.z - home.z)) + Math.PI;
      let rx = [];
      if (tr === 'restless' || tr === 'whisper') { rx = [{ k: 'die', t: die, from: ringAt }];
        if (tt > die + 0.4 && tt < die + 2.2 && tr === 'restless') { const u = (tt - die - 0.4) / 1.8, sp = souls[i % souls.length]; sp.visible = true;
          sp.position.lerpVectors(home.clone().setY(90), new THREE.Vector3(0, 260, 0), Math.min(1, u * 1.2)); sp.position.y += Math.sin(Math.PI * Math.min(1, u)) * 260 + (u > 0.85 ? (u - 0.85) * 1600 : 0);
          sp.material.opacity = u > 0.85 ? Math.max(0, 1 - (u - 0.85) / 0.15) : 1; }
        if (tr === 'restless') { ev('rise', die + 0.4); ev('soul', die + 1.9); if (tt > die + 1.9 && tt < die + 2.3) stone = 1.6; }
      } else if (tr === 'seals') { const si = i % 3, sm = seals[si]; home.set(sm.position.x * 0.92, 0, sm.position.z * 0.92); rx = i < 3 ? [{ k: 'die', t: 1.2 + i * 2.2, from: new THREE.Vector3() }] : [];
        if (i < 3) { sm.material.uniforms.a.value = tt > 1.9 + i * 2.2 ? Math.max(0, 1 - (tt - 1.9 - i * 2.2) * 2) : 1; ev('break', 1.9 + i * 2.2); } }
      zPose(z, rx, tt, { home, from: ringAt, face });
      z.holder.visible = tr !== 'seals' || i < 3;
    }
    if (tr === 'seals') { ring.visible = false; rl.intensity = 0.3; } else ring.visible = true;
    stoneLight.intensity = stone;
    if (plr) plrPose(plr, tt, [], pinfo);
  };
  return GL.add(v);
}
(function () {
  const grid = $('#trgrid'), st = $('#trstatus');
  $('#trsub').innerHTML = 'The four trials every city\'s Echo Altar sets, in 3D: the altar, its ring of light at its real size, the dead coming, the souls rising, the ring closing. Edit what each says (title, goal and text as the journal shows them), its numbers for each difficulty, its colours and the sound of each moment - the preview follows at once. <b>Hear it play</b> plays each moment\'s sound on its beat.';
  const all = [...TRIALS.trials, { id: 'common', title: 'Every trial', events: TRIALS.common, common: true }];
  const setOf = tr => Object.assign({ title: tr.title, goal: tr.goal, text: tr.text, n: tr.n, seconds: tr.seconds, ring: (tr.look || {}).ring, soul: (tr.look || {}).soul, radius: TRIALS.radius[tr.radius], snd: {} }, DB.get('trials', tr.id) || {});
  const evRow = (tr, e, s) => { const cur = (s.snd || {})[e.id] || 'a1'; return `<div class="slot" data-e="${e.id}"><div class="sh"><button class="play big" data-p="${e.id}" aria-label="Hear ${esc(e.name)}">▶</button><div class="sn"><b>${esc(e.name)}</b> <span class="cur">${esc((e.opts.find(o => o.id === cur) || e.opts[0]).name)}</span></div></div>
<p class="note" style="margin:0">Now: ${esc(e.now)}</p><div class="sopts">${e.opts.map(o => `<div class="sopt"><button class="play" data-q="${e.id}:${o.id}" aria-label="Hear ${esc(o.name)}">▶</button><button class="opt" data-o="${e.id}:${o.id}" aria-pressed="${cur === o.id}"><b>${esc(o.name)}${e.rec === o.id && o.id !== 'a1' ? ' <span class="tag ok">my pick</span>' : ''}</b>${o.desc ? `<span>${esc(o.desc)}</span>` : ''}</button></div>`).join('')}</div></div>`; };
  grid.innerHTML = all.map(tr => `<article class="wcard" data-id="${tr.id}">
${tr.common ? '' : `<div class="stage"><canvas aria-label="${esc(tr.title)}, live"></canvas><div class="ld">${GL.ok ? '' : 'Live previews need WebGL'}</div><div class="ov"><span class="ovl"></span><span class="ovr">9 s loop</span></div><button class="hear" aria-pressed="false">Hear it play</button></div>`}
<div class="wbody"><div class="t"><h3>${esc(tr.title)}</h3><span class="saved"></span></div><p class="now">${esc(tr.common ? 'The sounds every trial shares: its start, each step, the city freed.' : tr.shows)}</p><div class="fbody tform"></div></div></article>`).join('');
  const cards = {};
  grid.querySelectorAll('.wcard').forEach(el => {
    const tr = all.find(x => x.id === el.dataset.id), c = { el, tr, hear: false }; cards[tr.id] = c;
    whenNear(el, () => { if (!GL.ok || tr.common) return; c.view = trialView($('canvas', el)); c.view.trial = tr.id; c.view.set = setOf(tr);
      c.view.onEvent = id => { if (!c.hear) return; const e = tr.events.find(x => x.id === id); if (!e) return; const o = e.opts.find(x => x.id === ((setOf(tr).snd || {})[id] || 'a1')) || e.opts[0]; playRefs(o.snd, { loop: /hum|inside/.test(id) }); }; });
    paint(c);
  });
  function paint(c) {
    const tr = c.tr, s = setOf(tr), el = c.el, saved = DB.get('trials', tr.id);
    $('.saved', el).textContent = saved ? 'Edited' : '';
    el.classList.toggle('changed', !!saved);
    if (c.view) c.view.set = s;
    if (el.contains(document.activeElement) && /INPUT|TEXTAREA/.test(document.activeElement.tagName) && document.activeElement.type !== 'range') return;
    const R = TRIALS.radius[tr.radius];
    $('.tform', el).innerHTML = tr.common ? tr.events.map(e => evRow(tr, e, s)).join('') : `
<label class="fld"><span>Title</span><input data-f="title" value="${esc(s.title)}"></label>
<label class="fld"><span>Goal (the journal's line; {city} is the city)</span><input data-f="goal" value="${esc(s.goal)}"></label>
<label class="fld"><span>Text ({n} is the number)</span><textarea data-f="text" rows="3">${esc(s.text)}</textarea></label>
<div class="lbl">${tr.id === 'echo' ? 'Seconds to hold, by difficulty' : tr.id === 'seals' ? 'Seals, by difficulty' : 'The dead to slay, by difficulty'}</div>
<div class="nums">${TRIALS.diffs.map((dn, i) => `<label><span>${dn}</span><input type="number" min="1" max="999" data-f="${tr.id === 'echo' ? 'seconds' : 'n'}" data-i="${i}" value="${(tr.id === 'echo' ? s.seconds : s.n)[i]}"></label>`).join('')}</div>
<div class="sliders">${fsl('radius', 'Size of the light', Math.round(R * 0.4), Math.round(R * 1.8), 50, s.radius, '', 'the ring\'s radius in game units (70 a metre)')}</div>
<div class="row"><label class="chip wheel"><input type="color" data-f="ring" value="${s.ring}" aria-label="Ring colour">The ring's colour</label><label class="chip wheel"><input type="color" data-f="soul" value="${s.soul}" aria-label="Soul colour">The souls' colour</label></div>
<div class="lbl">Its sounds</div>${tr.events.map(e => evRow(tr, e, s)).join('')}
<textarea data-f="note" rows="1" placeholder="A note for me (optional)">${esc(s.note || '')}</textarea>`;
    $('.ovr', el) && ($('.ovr', el).textContent = `radius ${s.radius} (${Math.round(s.radius / 70)} m)`);
  }
  async function save(c, patch) { const row = Object.assign({ id: c.tr.id, name: c.tr.title }, setOf(c.tr), patch); try { await DB.set('trials', c.tr.id, row); saveMsg(st, null); } catch (e) { saveMsg(st, e); } paint(c); }
  grid.addEventListener('click', e => { const el = e.target.closest('.wcard'); if (!el) return; const c = cards[el.dataset.id], b = e.target.closest('button'); if (!b) return; AUD.ac();
    if (b.classList.contains('hear')) { c.hear = !c.hear; b.setAttribute('aria-pressed', String(c.hear)); if (!c.hear) AUD.stop(); return; }
    const pick = (eid, oid) => c.tr.events.find(x => x.id === eid).opts.find(o => o.id === oid);
    if (b.dataset.p) { const cur = (setOf(c.tr).snd || {})[b.dataset.p] || 'a1'; playRefs(pick(b.dataset.p, cur).snd, { loop: /hum|inside/.test(b.dataset.p) }); return; }
    if (b.dataset.q) { const [eid, oid] = b.dataset.q.split(':'); playRefs(pick(eid, oid).snd, { loop: /hum|inside/.test(eid) }); return; }
    if (b.dataset.o) { const [eid, oid] = b.dataset.o.split(':'); save(c, { snd: Object.assign({}, setOf(c.tr).snd, { [eid]: oid }) }); playRefs(pick(eid, oid).snd, { loop: /hum|inside/.test(eid) }); } });
  grid.addEventListener('input', e => { const el = e.target.closest('.wcard'); if (!el) return; const c = cards[el.dataset.id];
    if (e.target.type === 'range') { e.target.nextElementSibling.textContent = e.target.value; if (c.view) c.view.set = Object.assign({}, setOf(c.tr), { radius: +e.target.value }); }
    if (e.target.type === 'color' && c.view) c.view.set = Object.assign({}, setOf(c.tr), { [e.target.dataset.f]: e.target.value }); });
  grid.addEventListener('change', e => { const el = e.target.closest('.wcard'); if (!el) return; const c = cards[el.dataset.id], f = e.target.dataset.f || e.target.dataset.k; if (!f) return;
    if (f === 'n' || f === 'seconds') { const a = setOf(c.tr)[f].slice(); a[+e.target.dataset.i] = Math.max(1, Math.min(999, +e.target.value || 1)); save(c, { [f]: a }); }
    else if (f === 'radius') save(c, { radius: +e.target.value });
    else save(c, { [f]: String(e.target.value).slice(0, 2000) }); });
  window.addEventListener('detailclosed', e => { const c = Object.values(cards).find(x => x.el === e.detail); if (c && c.hear) { c.hear = false; const h = $('.hear', c.el); if (h) h.setAttribute('aria-pressed', 'false'); AUD.stop(); } });
  DB.on('trials', (d, err) => { if (err) { st.textContent = err === 'offline' ? 'You can try every change here; saving works when this page is opened in Claude.' : 'Your edits could not be loaded.'; return; } st.textContent = ''; Object.values(cards).forEach(paint); });
})();

// ---------------------------------------------------------------- big nights
(function () {
  const box = $('#perfgrid'), st = $('#perfstatus'), M = PERF.measured;
  $('#perfsub').innerHTML = M ? `What your own game measured on the last night you played (build ${esc(M.build)}, ${esc(M.day)}, ${M.minutes} minutes): our code cost <b>${M.ms} ms a frame</b> on average at <b>${M.fps} fps</b> (lowest minute ${M.fpsMin}), with up to <b>${M.actMax}</b> of the dead near you. The average is small; the <b>worst single frames</b> are the problem. Below that, the knobs for a big night: the first option is what the game does now.` : '';
  const sl = M ? Object.entries(M.slots).filter(([, v]) => v.worst >= 1).sort((a, b) => b[1].worst - a[1].worst) : [];
  const max = Math.max(100, ...sl.map(([, v]) => v.worst));
  const NM = { director: 'Director (the night, the waves)', gun: 'Gun (hits, effects)', shops: 'Shops (stations, altars)', classes: 'Classes (class weapons and spells)', compass: 'Compass', movement: 'Movement', save: 'Saving', loot: 'Loot', perks: 'Perks', hud: 'HUD' };
  const chart = sl.length ? `<div class="card"><div class="k">The worst frame each system took (ms) · a frame at 60 fps is 16.7 ms</div><div class="bars">${sl.map(([k, v]) => `<div class="bar"><span>${esc(NM[k] || k)}</span><div class="track"><i style="width:${(100 * v.worst / max).toFixed(1)}%" class="${v.worst >= 16.7 ? 'hot' : ''}"></i><em style="left:${(100 * 16.7 / max).toFixed(1)}%"></em></div><b>${v.worst} ms</b><small>${v.spikes ? v.spikes + ' minute' + (v.spikes > 1 ? 's' : '') + ' with a stutter' : 'no stutter'} · ${v.avg} ms average</small></div>`).join('')}</div>
<p class="note">Memory: Skyrim used up to ${(M.memMax / 1024).toFixed(1)} GB; Windows had ${M.freeMin} MB free at the lowest.</p></div>` : '';
  function paint() {
    box.innerHTML = chart + PERF.knobs.map(k => { const s = DB.get('perf', k.id) || {}; const cur = s.choice || k.opts.find(o => /now/.test(o[1]) || o[0] === 'keep' || o[0] === 'full' || o[0] === 'all')?.[0] || k.opts[0][0];
      return `<div class="rec" data-id="${k.id}"><div class="h"><h3>${esc(k.name)}</h3><span class="saved">${s.choice ? 'Saved' : ''}</span></div><p>Now: ${esc(k.now)}</p><p><b style="color:var(--fg);font-weight:500">My pick: ${esc(k.opts.find(o => o[0] === k.rec)[1])}.</b> ${esc(k.why)}</p>
<div class="row">${k.opts.map(([id, n]) => `<button class="btn sm" data-c="${id}" aria-pressed="${cur === id && !!s.choice}">${esc(n)}</button>`).join('')}</div>
<textarea rows="1" placeholder="A note (optional)">${esc(s.note || '')}</textarea></div>`; }).join('');
  }
  paint();
  box.addEventListener('click', async e => { const b = e.target.closest('button[data-c]'); if (!b) return; const r = b.closest('.rec'), k = PERF.knobs.find(x => x.id === r.dataset.id);
    try { await DB.set('perf', k.id, Object.assign({}, DB.get('perf', k.id) || {}, { id: k.id, name: k.name, choice: b.dataset.c })); saveMsg(st, null); } catch (er) { saveMsg(st, er); } paint(); });
  box.addEventListener('change', async e => { if (e.target.tagName !== 'TEXTAREA') return; const r = e.target.closest('.rec'), k = PERF.knobs.find(x => x.id === r.dataset.id);
    try { await DB.set('perf', k.id, Object.assign({}, DB.get('perf', k.id) || {}, { id: k.id, name: k.name, note: e.target.value.slice(0, 2000) })); saveMsg(st, null); } catch (er) { saveMsg(st, er); } });
  DB.on('perf', (d, err) => { if (err) { st.textContent = err === 'offline' ? 'Saving works when this page is opened in Claude.' : ''; return; } st.textContent = ''; if (!box.contains(document.activeElement)) paint(); });
})();

// ---------------------------------------------------------------- issues
(function () {
  const box = $('#ilist'), st = $('#istatus'), add = $('#iadd'), prog = $('#iprog');
  const ST = { open: ['Open', 'bad'], doing: ['Being fixed', 'warn'], watch: ['Watching', 'cold'], check: ['Needs your eyes', 'cold'], fixed: ['Fixed', 'ok'] };
  const WHERE = { game: 'In game', doc: 'In this doc', test: 'Testing' };
  add.innerHTML = `<div class="k">Add an issue</div><div class="row" style="align-items:stretch"><input id="inew" placeholder="What's wrong, in a line" aria-label="New issue" style="flex:1;min-width:200px"><button class="btn" id="iaddb">Add</button></div><textarea id="inewd" rows="1" placeholder="More detail: where, which weapon, what you saw (optional)"></textarea>`;
  function list() { const d = DB.all('issues'); const mine = ISSUES.map(([id, t, n, s0, w]) => ({ id, t, n, s0, w })); const theirs = Object.values(d).filter(x => x.id && x.id.startsWith('u_')).map(x => ({ id: x.id, t: x.title, n: x.detail, s0: 'open', w: 'yours' }));
    return theirs.concat(mine); }
  function paint() {
    const d = DB.all('issues'), L = list(); const c = {}; Object.keys(ST).forEach(k => c[k] = 0);
    box.innerHTML = L.map(x => { const s = d[x.id] || {}, cur = s.status || x.s0; c[cur] = (c[cur] || 0) + 1;
      return `<div class="test" data-id="${x.id}"><div><h3>${esc(x.t)} <span class="tag ${ST[cur][1]}">${ST[cur][0]}</span> <span class="tag">${esc(WHERE[x.w] || 'Yours')}</span></h3>${x.n ? `<p class="how">${esc(x.n)}</p>` : ''}</div>
<div class="row">${Object.entries(ST).map(([k, [n]]) => `<button class="chip" data-s="${k}" aria-pressed="${cur === k}">${n}</button>`).join('')}</div>
<textarea rows="1" placeholder="A note (optional)">${esc(s.note || '')}</textarea></div>`; }).join('');
    prog.innerHTML = Object.entries(ST).map(([k, [n]]) => `<span><b>${c[k] || 0}</b> ${n.toLowerCase()}</span>`).join('');
    document.querySelectorAll('[data-count="issues"]').forEach(x => x.textContent = L.length - (c.fixed || 0));
  }
  async function save(id, patch) { const x = list().find(q => q.id === id); try { await DB.set('issues', id, Object.assign({ id, title: x.t, detail: x.n || '' }, DB.get('issues', id) || {}, patch)); saveMsg(st, null); } catch (e) { saveMsg(st, e); } paint(); }
  $('#iaddb').addEventListener('click', async () => { const t = $('#inew').value.trim(); if (!t) { $('#inew').focus(); return; } const id = 'u_' + Date.now().toString(36);
    try { await DB.set('issues', id, { id, title: t.slice(0, 200), detail: $('#inewd').value.slice(0, 2000), status: 'open' }); $('#inew').value = ''; $('#inewd').value = ''; saveMsg(st, null); } catch (e) { saveMsg(st, e); } paint(); });
  box.addEventListener('click', e => { const b = e.target.closest('button[data-s]'); if (b) save(b.closest('.test').dataset.id, { status: b.dataset.s }); });
  box.addEventListener('change', e => { if (e.target.tagName === 'TEXTAREA') save(e.target.closest('.test').dataset.id, { note: e.target.value.slice(0, 2000) }); });
  paint(); DB.on('issues', (d, err) => { if (err) { st.textContent = err === 'offline' ? 'Adding and marking issues works when this page is opened in Claude.' : ''; return; } st.textContent = ''; if (!box.contains(document.activeElement)) paint(); });
})();
