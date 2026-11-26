// ================================================================ the sound engine: Skyrim's, the DLCs', Apocalypse's, Mysticism's and our own sounds,
// cut small from Noah's install (tools/soundopts.py), played the way a slot is set
const AUD = (function () {
  let ctx = null, master = null, cyc = 0; const bufs = {}, live = new Set();
  function ac() {
    if (!ctx) { const C = window.AudioContext || window.webkitAudioContext; if (!C) return null; ctx = new C();
      const comp = ctx.createDynamicsCompressor(); comp.threshold.value = -12; comp.ratio.value = 4;
      master = ctx.createGain(); master.gain.value = 0.9; master.connect(comp); comp.connect(ctx.destination); }
    if (ctx.state === 'suspended') ctx.resume();
    return ctx;
  }
  function load(f) { if (!bufs[f]) bufs[f] = fetch('snd/' + f).then(r => { if (!r.ok) throw new Error(r.status); return r.arrayBuffer(); }).then(b => new Promise((res, rej) => ac().decodeAudioData(b, res, rej))); return bufs[f]; }
  async function one(ref, gain, rate, delay, loopFor) {
    const S = SOUNDS.sounds[ref], c = ac(); if (!S || !c || !S.mp3 || !S.mp3.length || gain <= 0) return;
    let b; try { b = await load(S.mp3[Math.floor(Math.random() * S.mp3.length)]); } catch (e) { return; }
    const src = c.createBufferSource(); src.buffer = b; src.playbackRate.value = Math.max(0.3, rate);
    const g = c.createGain(); src.connect(g); g.connect(master);
    const t = c.currentTime + Math.max(0, delay || 0);
    if (loopFor) { src.loop = true; g.gain.setValueAtTime(0, t); g.gain.linearRampToValueAtTime(gain, t + 0.25); g.gain.setValueAtTime(gain, t + Math.max(0.3, loopFor - 0.8)); g.gain.linearRampToValueAtTime(0, t + loopFor); src.start(t); src.stop(t + loopFor + 0.05); }
    else { g.gain.value = gain; src.start(t); }
    live.add(src); src.onended = () => live.delete(src);
  }
  // a slot as set: its option's layers (or what plays now, cycling), the layer under it, its volume, pitch and variation
  function slot(sl, set, loopFor) {
    const o = sl.opts.find(x => x.id === set.opt) || sl.opts[0];
    const rate = (set.pitch / 100) * (1 + (Math.random() * 2 - 1) * set.varn / 100), vol = set.vol / 100;
    const lay = (opt, k) => {
      if (!opt) return;
      if (opt.cycle) { one(opt.cycle[cyc++ % opt.cycle.length], vol * k, rate, 0, 0); return; }
      for (const [r, g, d] of opt.snd) one(r, g * vol * k, rate, d, opt.loop ? (loopFor || 3) : 0);
    };
    lay(o, 1);
    if (set.layer && set.layer !== 'none' && set.layer !== o.id) lay(sl.opts.find(x => x.id === set.layer), set.layerVol / 100);
  }
  function stop() { live.forEach(s => { try { s.stop(); } catch (e) {} }); live.clear(); }
  return { slot, stop, ac, ok: !!(window.AudioContext || window.webkitAudioContext) };
})();

// ================================================================ the sound workshop
(function () {
  const grid = $('#sndgrid'), st = $('#sndstatus'), cats = $('#sndcats');
  const SRCN = { Skyrim: 'Skyrim', Dawnguard: 'Dawnguard', Dragonborn: 'Dragonborn', Apocalypse: 'Apocalypse', Mysticism: 'Mysticism', ours: 'Ours', Update: 'Skyrim' };
  const items = [];
  ABIL.forEach(([cat, cid, list]) => list.forEach(([id, name, abs]) => {
    const keys = abs.map(a => id + '_' + a[0]).filter(k => SOUNDS.abilities[k]);
    if (!SOUNDS.weapons[id] && !keys.length) return;
    items.push({ cat, cid, id, name, abs: abs.filter(a => SOUNDS.abilities[id + '_' + a[0]]).map(a => ({ key: id + '_' + a[0], trig: a[1], does: a[2] })) });
  }));
  const groups = [...new Map(items.map(x => [x.cid, x.cat])).entries()];
  const SLD = [['vol', 'Volume', 0, 200, 5], ['pitch', 'Pitch', 50, 150, 1], ['varn', 'Variation', 0, 30, 1], ['layerVol', 'Layer volume', 0, 150, 5]];
  const SLH = { vol: 'how loud it plays', pitch: 'higher or lower: below 100 is deeper and slower', varn: 'how much each play differs, so a hundred hits don\'t sound the same', layerVol: 'how loud the layer under it plays' };
  const defSet = sl => ({ opt: 'a1', vol: 20, pitch: 100, varn: 5, layer: 'none', layerVol: 50 });
  const setOf = (id, sl) => Object.assign(defSet(sl), DB.get('sounds', id) || {});
  const srcOf = o => [...new Set((o.cycle || o.snd.map(x => x[0])).map(r => (SOUNDS.sounds[r] || {}).src).filter(Boolean))].map(s => SRCN[s] || s);
  const slotsOf = (c, i) => i === 0 ? (SOUNDS.weapons[c.it.id] || []).map(s => ['w__' + c.it.id + '__' + s.id, s]) : (SOUNDS.abilities[c.it.abs[i - 1].key] || []).map(s => ['a__' + c.it.abs[i - 1].key + '__' + s.id, s]);
  cats.innerHTML = `<button data-c="all" aria-pressed="true">All<span class="n">${items.length}</span></button>` + groups.map(([id, n]) => `<button data-c="${id}" aria-pressed="false">${esc(n)}<span class="n">${items.filter(x => x.cid === id).length}</span></button>`).join('');
  grid.innerHTML = groups.map(([cid, cat]) => `<div data-c="${cid}" style="grid-column:1/-1;display:grid;gap:2px;margin-top:6px"><h3 style="font-family:var(--display);font-weight:400;font-size:20px;color:var(--accent)">${esc(cat)}</h3></div>` +
    items.filter(x => x.cid === cid).map(it => `<article class="wcard scard" data-id="${it.id}" data-c="${cid}">
<div class="stage"><canvas aria-label="${esc(it.name)}, live"></canvas><div class="ld">${GL.ok ? '' : 'Live previews need WebGL'}</div><div class="ov"><span class="ovl"></span><span class="ovr"></span></div>
<button class="hear" aria-pressed="false">Hear it play</button></div>
<div class="abtabs" role="tablist" aria-label="${esc(it.name)}'s sounds">${SOUNDS.weapons[it.id] ? '<button role="tab" data-i="0" aria-selected="true">The weapon</button>' : ''}${it.abs.map((a, i) => `<button role="tab" data-i="${i + 1}" aria-selected="${!SOUNDS.weapons[it.id] && i === 0}">${esc(a.trig)}</button>`).join('')}</div>
<div class="wbody"><div class="t"><h3>${esc(it.name)}</h3><span class="saved"></span></div>
<p class="now sdoes"></p><div class="slots"></div>
<div class="row"><button class="btn sm" data-a="mine">Use all my picks for this weapon</button><button class="btn sm" data-a="now">Back to as it is now</button></div>
<textarea rows="1" placeholder="A note for me about its sounds (optional)" aria-label="Note on ${esc(it.name)}'s sounds"></textarea>
</div></article>`).join('')).join('');
  const cards = {}; let hearing = null; const openNow = {};
  grid.querySelectorAll('.scard').forEach(el => {
    const it = items.find(x => x.id === el.dataset.id), c = { el, it, i: SOUNDS.weapons[it.id] ? 0 : 1, open: {} }; cards[it.id] = c;
    whenNear(el, () => { if (GL.ok) { c.view = abilityView($('canvas', el)); c.view.onBeat = b => { if (hearing === c) beat(c, b); }; preview(c); } });
    paint(c);
  });
  function preview(c) {
    const key = c.i === 0 ? (c.it.abs[0] || {}).key : c.it.abs[c.i - 1].key;
    if (!c.view || !key || !window.abArgs) return;
    const args = abArgs(key); if (!args) return;
    c.view.key = key; c.view.play(...args);
    $('.ovl', c.el).textContent = c.i === 0 ? `the weapon, with its ${c.it.abs[0].trig.toLowerCase()}` : c.it.abs[c.i - 1].trig;
  }
  // a beat of the preview: the weapon's swing, heavy and hit; the ability's cast, impact and what lingers
  function beat(c, b) { playBeat(c.it.id, c.i === 0 ? (c.it.abs[0] || {}).key : c.it.abs[c.i - 1].key, b); }
  function playBeat(item, key, b) {
    const w = SOUNDS.weapons[item] || [], a = (key && SOUNDS.abilities[key]) || [];
    const find = (L, s, pre) => { const sl = L.find(x => x.id === s); return sl ? [pre + s, sl] : null; };
    let r = null;
    if (b.k === 'swing' || b.k === 'heavy' || b.k === 'hit') r = find(w, b.k, 'w__' + item + '__') || (b.k === 'heavy' ? find(w, 'swing', 'w__' + item + '__') : null);
    else r = find(a, b.k, 'a__' + key + '__');
    if (r) AUD.slot(r[1], setOf(r[0], r[1]), b.d);
  }
  window.sndBeat = playBeat;   // (the hit feel and the dead play each beat with your sound picks too)
  function optRow(id, sl, o, set) {
    const rec = sl.rec === o.id, src = srcOf(o);
    return `<div class="sopt"><button class="play" data-p="${o.id}" aria-label="Play ${esc(o.name)}">▶</button><button class="opt" data-o="${o.id}" aria-pressed="${set.opt === o.id}"><b>${esc(o.name)}${rec ? ' <span class="tag ok">my pick</span>' : ''}${o.now ? ' <span class="tag">in the game now</span>' : ''}</b><span>${esc(o.desc)}</span>${src.length ? `<span class="srcs">${src.map(s => `<i>${esc(s)}</i>`).join('')}</span>` : ''}</button></div>`;
  }
  function slotHTML(id, sl) {
    const set = setOf(id, sl), o = sl.opts.find(x => x.id === set.opt) || sl.opts[0], rec = sl.opts.find(x => x.id === sl.rec), saved = DB.get('sounds', id);
    const mod = set.vol !== 20 || set.pitch !== 100 || set.varn !== 5 || (set.layer && set.layer !== 'none');
    return `<div class="slot${saved && set.opt !== 'a1' ? ' changed' : ''}" data-s="${id}">
<div class="sh"><button class="play big" data-p="${o.id}" aria-label="Play ${esc(sl.label)}">▶</button><div class="sn"><b>${esc(sl.label)}</b> <span class="cur">${esc(o.name)}${mod ? ' · tuned' : ''}</span>${saved ? '<span class="dot" aria-label="saved"></span>' : ''}</div><button class="btn sm" data-a="open" aria-expanded="${!!this_open(id)}">${this_open(id) ? 'Hide' : 'Options'}</button></div>
<div class="sbody"${this_open(id) ? '' : ' hidden'}>
<p class="why"><b>My pick: ${esc(rec ? rec.name : '')}.</b> ${esc(sl.why || '')}</p>
<div class="sopts">${sl.opts.map(x => optRow(id, sl, x, set)).join('')}</div>
<div class="sliders">${SLD.map(([k, n, lo, hi, step]) => `<label class="sl" title="${SLH[k]}"><span>${n}</span><input type="range" data-k="${k}" min="${lo}" max="${hi}" step="${step}" value="${set[k]}" aria-label="${n}"><b class="v">${set[k]}%</b></label>`).join('')}
<label class="sl lay"><span>Layer under it</span><select data-k="layer" aria-label="Layer under it"><option value="none">None</option>${sl.opts.filter(x => x.snd.length || x.cycle).map(x => `<option value="${x.id}"${set.layer === x.id ? ' selected' : ''}>${esc(x.name)}</option>`).join('')}</select></label></div>
</div></div>`;
  }
  function this_open(id) { return openNow[id] !== false; }
  function paint(c) {
    const el = c.el;
    el.querySelectorAll('.abtabs button').forEach(b => b.setAttribute('aria-selected', String(+b.dataset.i === c.i)));
    $('.sdoes', el).innerHTML = c.i === 0 ? `<b>The weapon itself:</b> drawn, swung light and heavy, and its hit on the dead. Hear it play runs its ${esc((c.it.abs[0] || {}).trig || '')} with every sound on its beat.` : `<b>${esc(c.it.abs[c.i - 1].trig)}:</b> ${esc(c.it.abs[c.i - 1].does)}`;
    $('.slots', el).innerHTML = slotsOf(c, c.i).map(([id, sl]) => slotHTML(id, sl)).join('');
    const mine = Object.keys(DB.all('sounds')).filter(k => k.startsWith('w__' + c.it.id + '__') || c.it.abs.some(a => k.startsWith('a__' + a.key + '__'))).length;
    $('.saved', el).textContent = mine ? `${mine} sound${mine > 1 ? 's' : ''} set` : '';
    const note = DB.get('sounds', 'n__' + c.it.id), ta = $('textarea', el); if (document.activeElement !== ta) ta.value = note && note.note || '';
  }
  async function save(id, sl, patch, c) {
    const prev = setOf(id, sl), row = Object.assign({ id, item: c.it.name, slot: sl.label }, prev, patch);
    row.optName = (sl.opts.find(x => x.id === row.opt) || {}).name || '';
    try { await DB.set('sounds', id, row); saveMsg(st, null); count(); } catch (e) { saveMsg(st, e); }
    paint(c);
  }
  function slotFor(c, id) { return (slotsOf(c, 0).concat(...c.it.abs.map((a, i) => slotsOf(c, i + 1)))).find(([k]) => k === id); }
  grid.addEventListener('click', e => {
    const el = e.target.closest('.scard'); if (!el) return; const c = cards[el.dataset.id], b = e.target.closest('button'); if (!b) return;
    AUD.ac();
    if (b.classList.contains('hear')) {
      const on = hearing !== c; if (hearing) hearing.el.querySelector('.hear').setAttribute('aria-pressed', 'false');
      hearing = on ? c : null; b.setAttribute('aria-pressed', String(on)); if (!on) AUD.stop();
      if (on && c.view) preview(c); return;
    }
    if (b.dataset.i != null) { c.i = +b.dataset.i; paint(c); preview(c); return; }
    if (b.dataset.a === 'mine' || b.dataset.a === 'now') {
      const all = slotsOf(c, 0).concat(...c.it.abs.map((a, i) => slotsOf(c, i + 1)));
      (async () => { for (const [id, sl] of all) { const row = Object.assign({ id, item: c.it.name, slot: sl.label }, defSet(sl), { opt: b.dataset.a === 'mine' ? sl.rec : 'a1' }); row.optName = (sl.opts.find(x => x.id === row.opt) || {}).name; try { await DB.set('sounds', id, row); } catch (er) { saveMsg(st, er); break; } } count(); paint(c); saveMsg(st, null); })();
      return;
    }
    const sEl = b.closest('.slot'); if (!sEl) return; const [id, sl] = slotFor(c, sEl.dataset.s) || [];
    if (!sl) return;
    if (b.dataset.a === 'open') { openNow[id] = !this_open(id); paint(c); return; }
    if (b.dataset.p) { const set = setOf(id, sl); AUD.slot(sl, Object.assign({}, set, b.classList.contains('big') ? {} : { opt: b.dataset.p, layer: 'none' }), 3); return; }
    if (b.dataset.o) { save(id, sl, { opt: b.dataset.o }, c); AUD.slot(sl, Object.assign(setOf(id, sl), { opt: b.dataset.o }), 3); }
  });
  grid.addEventListener('input', e => { if (e.target.type !== 'range') return; e.target.nextElementSibling.textContent = e.target.value + '%'; });
  grid.addEventListener('change', e => {
    const el = e.target.closest('.scard'); if (!el) return; const c = cards[el.dataset.id];
    if (e.target.tagName === 'TEXTAREA') { DB.set('sounds', 'n__' + c.it.id, { id: 'n__' + c.it.id, item: c.it.name, note: e.target.value.slice(0, 2000) }).then(() => saveMsg(st, null), er => saveMsg(st, er)); return; }
    const sEl = e.target.closest('.slot'); if (!sEl) return; const [id, sl] = slotFor(c, sEl.dataset.s) || []; if (!sl) return;
    const k = e.target.dataset.k, v = e.target.type === 'range' ? +e.target.value : e.target.value;
    save(id, sl, { [k]: v }, c); AUD.ac(); AUD.slot(sl, Object.assign(setOf(id, sl), { [k]: v }), 3);
  });
  cats.addEventListener('click', e => { const b = e.target.closest('button'); if (!b) return; cats.querySelectorAll('button').forEach(x => x.setAttribute('aria-pressed', String(x === b)));
    grid.querySelectorAll('[data-c]').forEach(x => x.hidden = b.dataset.c !== 'all' && x.dataset.c !== b.dataset.c); });
  const nSlots = items.reduce((s, it) => s + (SOUNDS.weapons[it.id] || []).length + it.abs.reduce((q, a) => q + SOUNDS.abilities[a.key].length, 0), 0);
  function count() { const d = Object.values(DB.all('sounds')).filter(x => !String(x.id).startsWith('n__')); st.textContent = d.length ? `${d.length} of ${nSlots} sounds set (${d.filter(x => x.opt !== 'a1').length} changed). Saved here for me to build from.` : `None of the ${nSlots} sounds set yet. Press ▶ to hear any of them, or Hear it play to hear a weapon with all its sounds on their beats.`; }
  count();
  document.querySelectorAll('[data-count="snd"]').forEach(x => x.textContent = items.length);
  DB.on('sounds', (d, err) => { if (err) { st.textContent = err === 'offline' ? 'You can hear every sound here; saving works when this page is opened in Claude.' : 'Your sound picks could not be loaded.'; return; } count(); Object.values(cards).forEach(paint); });
  window.addEventListener('detailclosed', e => { if (hearing && hearing.el === e.detail) { hearing.el.querySelector('.hear').setAttribute('aria-pressed', 'false'); hearing = null; AUD.stop(); } });
  window.addEventListener('tabshown', () => { if ($('#sounds').hidden && hearing) { hearing.el.querySelector('.hear').setAttribute('aria-pressed', 'false'); hearing = null; AUD.stop(); } });
})();
