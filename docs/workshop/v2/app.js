// The Curse of Alduin workshop: one permanent page (Noah, Nov 26 night). Its data is docs/workshop/v2/data.json, written by
// data.py; what Noah marks here is kept in the page's own database:
//   marks/<build>_<feature>   {r: 'pass'|'fail'|'skip', note}      one feature tested in a build
//   groups/<build>_<system>   {pass: true, note}                     a whole category passed at once
//   decisions/<id>            {pick, note}
//   cwpick/<class>            {model, note}                          the class weapon's one model
//   tablet/<weapon>           {choice: 'keep'|'cut'|'rework', note}  (and tablet/_ideas {note})
//   uidir/pick                {pick, note}                           the in-game UI direction
//   uiscreens/<screen>        {choice: 'ok'|'change', note}
const D = JSON.parse(document.getElementById('wsdata').textContent);
const $ = (s, el = document) => el.querySelector(s);
const $$ = (s, el = document) => [...el.querySelectorAll(s)];
const BUILD = D.installed.sha;
const ST_NAME = { idea: 'Idea', building: 'Building', ready: 'Ready to test', passed: 'Passed', failed: 'Failed', done: 'Done' };
const ST_ORDER = ['ready', 'failed', 'building', 'idea', 'passed', 'done'];
const SYS = Object.fromEntries(D.systems.map(s => [s.id, s]));
const pill = st => `<span class="pill st-${st}">${ST_NAME[st]}</span>`;

// ---------------------------------------------------------------- the database (the page's own: shared, kept)
const DB = (function () {
  let db = null; const data = {}, local = {}, subs = {};
  const ready = (async () => { try { db = window.claude && window.claude.use ? await window.claude.use('db') : null; } catch (e) { db = null; } return db; })();
  const merged = c => Object.assign({}, data[c] || {}, local[c] || {});
  return {
    ready,
    get: (c, id) => merged(c)[id],
    all: c => merged(c),
    watch(c, fn) {
      (subs[c] = subs[c] || []).push(fn);
      ready.then(d => {
        if (!d || subs[c].length > 1) return;
        try {
          d.collection(c).onSnapshot(snap => {
            data[c] = {}; snap.docs.forEach(x => { const v = x.data(); if (v) data[c][x.id] = v; });
            for (const id of Object.keys(local[c] || {})) if (data[c][id] && (data[c][id].savedAt || '') >= (local[c][id].savedAt || '')) delete local[c][id];
            subs[c].forEach(f => f());
          }, () => {});
        } catch (e) { /* (viewed outside Claude: what's marked here shows, nothing is kept) */ }
      });
    },
    async set(c, id, row) {
      row.savedAt = new Date().toISOString();
      (local[c] = local[c] || {})[id] = row;
      (subs[c] || []).forEach(f => f());
      const d = await ready; if (!d) throw { code: 'offline' };
      await d.collection(c).doc(id).set(row);
      return row;
    },
  };
})();
function savedMsg(el, e) {
  if (!el) return;
  el.textContent = !e ? 'Saved.' : e.code === 'not_granted' ? 'You can view this page but not save on it.' : e.code === 'offline' ? 'Saving works when this page is opened in Claude.' : 'That didn\'t save. Check your connection and try again.';
}

// ---------------------------------------------------------------- a feature's status now: its own, with this build's marks over it
function markOf(f) { return DB.get('marks', BUILD + '_' + f.id); }
function groupOf(sys) { return DB.get('groups', BUILD + '_' + sys); }
function statusOf(f) {
  if (f.st !== 'ready') return f.st;
  const m = markOf(f);
  if (m && m.r === 'pass') return 'passed';
  if (m && m.r === 'fail') return 'failed';
  if ((!m || m.r !== 'skip') && groupOf(f.sys) && groupOf(f.sys).pass) return 'passed';
  return 'ready';
}
const readyIn = sys => D.features.filter(f => f.sys === sys && f.st === 'ready');

function paintBadge() {   // (the Now tab's count: what's left to test, whichever section is open)
  const left = D.features.filter(f => f.st === 'ready' && statusOf(f) === 'ready').length, n = $('#n-now');
  n.hidden = !left; n.textContent = left;
}
// ---------------------------------------------------------------- the header and the sections
$('#installed').innerHTML = `Installed on your PC<br><b>Build ${esc(BUILD)}</b> &middot; ${esc(D.installed.label)} &middot; ${esc(D.installed.date)}`;
const SECTIONS = ['now', 'systems', 'weapons', 'ui', 'lore', 'release', 'history'];
const drawn = {};
function route() {
  let s = (location.hash || '').replace('#', '');
  if (!SECTIONS.includes(s)) s = 'now';
  $$('section[data-s]').forEach(el => el.classList.toggle('on', el.dataset.s === s));
  $$('#nav a').forEach(a => { if (a.dataset.s === s) a.setAttribute('aria-current', 'page'); else a.removeAttribute('aria-current'); });
  if (!drawn[s]) { drawn[s] = true; DRAW[s](); }
  window.scrollTo({ top: 0 });
}
window.addEventListener('hashchange', route);

// ================================================================ NOW: what to test, what to decide, what's being built
function drawNow() {
  const el = $('section[data-s="now"]');
  el.innerHTML = `<h2 class="sec">Now</h2>
<p class="lead">What's on your PC to test, sorted by category; the decisions waiting for you; and what's being built next. Mark each item, or pass a whole category at once. A fail goes straight back to the fix list with your note.</p>
<div class="stats" id="nowstats"></div>
<h3 class="sub">Test by category</h3><div id="cats"></div>
<h3 class="sub">Back to fix</h3><div id="fails"></div>
<h3 class="sub">Decisions waiting for you</h3><div id="decs"></div>
<h3 class="sub">Being built next</h3><div id="next"></div>`;
  // the categories with something to test
  const cats = D.systems.filter(s => readyIn(s.id).length);
  $('#cats').innerHTML = cats.map(s => `<details class="cat" data-sys="${s.id}"><summary><h4>${esc(s.name)}</h4><span class="prog"><span class="c"></span><i></i></span>
<span class="meta">${esc(s.about)}</span></summary><div class="body">${readyIn(s.id).map(itemHtml).join('')}
<div class="catfoot"><span class="saved gs"></span><button class="btn big pass" data-group="${s.id}" aria-pressed="false">Whole category passed</button></div></div></details>`).join('')
    || '<p class="empty">Nothing waiting to be tested.</p>';
  const first = $('#cats details'); if (first) first.open = true;
  // decisions
  $('#decs').innerHTML = D.decisions.map(d => `<div class="cat" data-dec="${d.id}" style="padding:14px 16px"><div class="item" style="padding:0">
<div class="ttl"><b>${esc(d.q)}</b><span class="pill st-idea">${esc(SYS[d.sys].name)}</span></div>
<div class="marks">${d.opts.map(o => `<button class="btn neutral" data-opt="${o.id}" aria-pressed="false" title="${esc(o.about)}">${esc(o.name)}</button>`).join('')}</div>
<div class="optabout muted" style="font-size:13px"></div>
<textarea rows="1" placeholder="A note (optional)" aria-label="Note on: ${esc(d.q)}"></textarea><span class="saved"></span></div></div>`).join('');
  // being built
  const nx = D.features.filter(f => f.st === 'building' || f.st === 'idea');
  $('#next').innerHTML = `<div class="flist">${nx.sort((a, b) => ST_ORDER.indexOf(a.st) - ST_ORDER.indexOf(b.st)).map(f => `<div class="frow"><div class="ttl"><b>${esc(f.t)}</b><span>${pill(f.st)} <span class="lbl">&nbsp;${esc(SYS[f.sys].name)}</span></span></div><p>${esc(f.what)}</p>${f.note ? `<p class="note">${esc(f.note)}</p>` : ''}</div>`).join('')}</div>`;
  el.addEventListener('click', async e => {
    const mb = e.target.closest('button[data-mark]');
    if (mb) {
      const it = mb.closest('.item'), id = it.dataset.f, prev = DB.get('marks', BUILD + '_' + id) || {};
      const r = prev.r === mb.dataset.mark ? '' : mb.dataset.mark;
      try { await DB.set('marks', BUILD + '_' + id, { r, note: $('textarea', it).value.slice(0, 2000), feature: id, build: BUILD }); savedMsg($('.saved', it)); } catch (er) { savedMsg($('.saved', it), er); }
      paintNow(); return;
    }
    const gb = e.target.closest('button[data-group]');
    if (gb) {
      const sys = gb.dataset.group, prev = groupOf(sys) || {};
      try { await DB.set('groups', BUILD + '_' + sys, { pass: !prev.pass, system: sys, build: BUILD }); savedMsg($('.gs', gb.parentElement)); } catch (er) { savedMsg($('.gs', gb.parentElement), er); }
      paintNow(); return;
    }
    const ob = e.target.closest('button[data-opt]');
    if (ob) {
      const box = ob.closest('[data-dec]'), id = box.dataset.dec, prev = DB.get('decisions', id) || {};
      try { await DB.set('decisions', id, { pick: prev.pick === ob.dataset.opt ? '' : ob.dataset.opt, note: $('textarea', box).value.slice(0, 2000) }); savedMsg($('.saved', box)); } catch (er) { savedMsg($('.saved', box), er); }
      paintNow();
    }
  });
  el.addEventListener('change', async e => {
    if (e.target.tagName !== 'TEXTAREA') return;
    const it = e.target.closest('.item[data-f]');
    if (it) { const id = it.dataset.f, prev = DB.get('marks', BUILD + '_' + id) || {};
      try { await DB.set('marks', BUILD + '_' + id, { r: prev.r || '', note: e.target.value.slice(0, 2000), feature: id, build: BUILD }); savedMsg($('.saved', it)); } catch (er) { savedMsg($('.saved', it), er); } return; }
    const box = e.target.closest('[data-dec]');
    if (box) { const prev = DB.get('decisions', box.dataset.dec) || {};
      try { await DB.set('decisions', box.dataset.dec, { pick: prev.pick || '', note: e.target.value.slice(0, 2000) }); savedMsg($('.saved', box)); } catch (er) { savedMsg($('.saved', box), er); } }
  });
  paintNow();
}
function itemHtml(f) {
  return `<div class="item" data-f="${f.id}"><div class="ttl"><b>${esc(f.t)}</b><span class="stp"></span></div>
<p class="muted">${esc(f.what)}</p>
<div class="howlook"><div><span class="lbl">How to test</span>${esc(f.how)}</div><div><span class="lbl">What you should see</span>${esc(f.look)}</div></div>
${f.note ? `<p class="note">Last time: ${esc(f.note.replace(/^Last time: /, ''))}</p>` : ''}
<div class="marks"><button class="btn pass" data-mark="pass" aria-pressed="false">Pass</button><button class="btn fail" data-mark="fail" aria-pressed="false">Fail</button><button class="btn skip" data-mark="skip" aria-pressed="false">Skip</button><span class="saved"></span></div>
<textarea rows="1" placeholder="What you saw (a fail needs a note)" aria-label="Note on ${esc(f.t)}"></textarea></div>`;
}
function paintNow() {
  if (!drawn.now) return;
  const ready = D.features.filter(f => f.st === 'ready');
  const sts = ready.map(statusOf);
  const left = sts.filter(s => s === 'ready').length, pass = sts.filter(s => s === 'passed').length, fail = sts.filter(s => s === 'failed').length;
  const decOpen = D.decisions.filter(d => !(DB.get('decisions', d.id) || {}).pick).length;
  $('#nowstats').innerHTML = `<div class="stat ember"><b>${left}</b><span>To test</span></div><div class="stat green"><b>${pass}</b><span>Passed</span></div><div class="stat red"><b>${fail}</b><span>Failed</span></div><div class="stat gold"><b>${decOpen}</b><span>Decisions waiting</span></div>`;
  $$('#cats details').forEach(c => {
    const its = readyIn(c.dataset.sys), done = its.filter(f => statusOf(f) !== 'ready').length;
    $('.prog .c', c).textContent = `${done} / ${its.length}`;
    $('.prog i', c).style.setProperty('--v', (100 * done / its.length) + '%');
    const g = groupOf(c.dataset.sys); $('button[data-group]', c).setAttribute('aria-pressed', String(!!(g && g.pass)));
  });
  $$('#cats .item').forEach(it => {
    const f = D.features.find(x => x.id === it.dataset.f), m = markOf(f) || {};
    $$('button[data-mark]', it).forEach(b => b.setAttribute('aria-pressed', String(m.r === b.dataset.mark)));
    $('.stp', it).innerHTML = pill(statusOf(f));
    const ta = $('textarea', it); if (document.activeElement !== ta) ta.value = m.note || '';
  });
  const fails = ready.filter(f => statusOf(f) === 'failed');
  $('#fails').innerHTML = fails.length ? `<div class="flist">${fails.map(f => `<div class="frow"><div class="ttl"><b>${esc(f.t)}</b><span>${pill('failed')}</span></div><p>${esc((markOf(f) || {}).note || 'No note yet: what went wrong?')}</p></div>`).join('')}</div>`
    : '<p class="empty">Nothing failed in this build so far.</p>';
  $$('#decs [data-dec]').forEach(box => {
    const d = D.decisions.find(x => x.id === box.dataset.dec), p = DB.get('decisions', d.id) || {};
    $$('button[data-opt]', box).forEach(b => b.setAttribute('aria-pressed', String(p.pick === b.dataset.opt)));
    const o = d.opts.find(x => x.id === p.pick); $('.optabout', box).textContent = o ? o.about : 'Hover or tap an option to choose it; tap it again to clear.';
    const ta = $('textarea', box); if (document.activeElement !== ta) ta.value = p.note || '';
  });
}

// ================================================================ SYSTEMS: every part of the mod and where it stands
let selSys = '', selSt = '';
function drawSystems() {
  const el = $('section[data-s="systems"]');
  el.innerHTML = `<h2 class="sec">Systems</h2>
<p class="lead">Every part of the mod, by category, with where each feature stands. This is the record of what the mod is: anything done stays here, so old ideas don't come back as suggestions.</p>
<div class="legend">${ST_ORDER.map(s => pill(s)).join('')}</div>
<div class="sgrid" id="sgrid"></div>
<h3 class="sub" id="fhead">Every feature</h3><div class="chips" id="stchips"></div><div class="flist" id="flist"></div>`;
  $('#sgrid').addEventListener('click', e => { const b = e.target.closest('.scard'); if (!b) return; selSys = selSys === b.dataset.sys ? '' : b.dataset.sys; paintSystems(); $('#fhead').scrollIntoView({ behavior: 'smooth', block: 'start' }); });
  $('#stchips').addEventListener('click', e => { const b = e.target.closest('.chip'); if (!b) return; selSt = b.dataset.st; paintSystems(); });
  paintSystems();
}
function paintSystems() {
  if (!drawn.systems) return;
  $('#sgrid').innerHTML = D.systems.map(s => {
    const fs = D.features.filter(f => f.sys === s.id), c = {}; fs.forEach(f => { const st = statusOf(f); c[st] = (c[st] || 0) + 1; });
    return `<button class="scard" data-sys="${s.id}" aria-pressed="${selSys === s.id}"><h4>${esc(s.name)}</h4><p>${esc(s.about)}</p>
<div class="stbar">${ST_ORDER.map(st => c[st] ? `<i class="${st}" style="width:${100 * c[st] / fs.length}%"></i>` : '').join('')}</div>
<span class="lbl">${fs.length} features${c.ready ? ` &middot; ${c.ready} to test` : ''}${c.failed ? ` &middot; ${c.failed} failed` : ''}</span></button>`;
  }).join('');
  const fs = D.features.filter(f => !selSys || f.sys === selSys);
  $('#fhead').textContent = selSys ? SYS[selSys].name : 'Every feature';
  $('#stchips').innerHTML = [['', 'All']].concat(ST_ORDER.map(s => [s, ST_NAME[s]])).map(([s, n]) => {
    const k = s ? fs.filter(f => statusOf(f) === s).length : fs.length;
    return k || !s ? `<button class="chip" data-st="${s}" aria-pressed="${selSt === s}">${esc(n)} (${k})</button>` : '';
  }).join('');
  const show = fs.filter(f => !selSt || statusOf(f) === selSt).sort((a, b) => ST_ORDER.indexOf(statusOf(a)) - ST_ORDER.indexOf(statusOf(b)));
  $('#flist').innerHTML = show.map(f => { const st = statusOf(f), m = markOf(f);
    return `<div class="frow"><div class="ttl"><b>${esc(f.t)}</b><span>${pill(st)}${selSys ? '' : ` <span class="lbl">&nbsp;${esc(SYS[f.sys].name)}</span>`}</span></div><p>${esc(f.what)}</p>
${m && m.note ? `<p class="note">You, build ${esc(BUILD)}: ${esc(m.note)}</p>` : f.note ? `<p class="note">${esc(f.note)}</p>` : ''}${f.since ? `<span class="lbl">Since build ${esc(f.since)}</span>` : ''}</div>`; }).join('') || '<p class="empty">Nothing here.</p>';
}

// ================================================================ WEAPONS & EFFECTS
let wTab = 'classes', kitEl = 'fire';
function drawWeapons() {
  const el = $('section[data-s="weapons"]');
  el.innerHTML = `<h2 class="sec">Weapons &amp; effects</h2>
<p class="lead">The foundation for every weapon and effect. <b>Element kits</b> set how each element looks and sounds, once; each <b>class weapon</b> then gets its four tier looks on the model you picked; the <b>Tablet</b> gets its new lineup. Abilities are built per weapon from its element's kit next.</p>
<div class="chips" id="wtabs">${[['classes', 'Class weapons'], ['kits', 'Element kits'], ['tablet', 'Tablet lineup'], ['clips', 'Test Range clips']].map(([k, n]) => `<button class="chip" data-w="${k}" aria-pressed="${k === wTab}">${n}</button>`).join('')}</div>
<div id="wbody"></div>`;
  $('#wtabs').addEventListener('click', e => { const b = e.target.closest('.chip'); if (!b) return; wTab = b.dataset.w; $$('#wtabs .chip').forEach(c => c.setAttribute('aria-pressed', String(c === b))); paintWeapons(true); });
  paintWeapons(true);
}
const VIEWS = [];
let COMPARE = false;   // (the old flat art shown instead of the 3D look, to compare)
function clearViews() { stopSound(); while (VIEWS.length) { const v = VIEWS.pop(); try { GL.remove(v); } catch (e) {} } }
function view3d(cv, key, o = {}) {
  if (!GL.ok || !key) return null;
  const v = weaponView(cv); VIEWS.push(v); v.spin = !!o.spin; v.gain = o.gain == null ? 1 : o.gain; v.vol = o.vol !== false; v.compare = COMPARE;
  v.show(key, o.look || 'o1', 'own').then(() => { if (v.fx) v.fx.g.visible = !o.bare; if (o.layer) v.layer(o.layer, o.layerGain || 0.7); });
  cv.__v = v; return v;
}
async function saveTo(c, id, row, msgEl) { try { await DB.set(c, id, row); savedMsg(msgEl); } catch (er) { savedMsg(msgEl, er); } }

// ---- the tier ladder: one look, stronger each tier (I faint, II glowing edge, III full, Legendary all of it plus another look)
const TIERS = [['t1', 'Tier I', 0.45], ['t2', 'Tier II', 0.7], ['t3', 'Tier III', 1.0], ['t4', 'Legendary', 1.25]];
function tierOptions(c, picks) {
  const ids = c.looks.map(l => l.id);
  const out = {};
  out.t1 = ids.slice(0, 3).map(id => ({ look: id }));
  for (const [t, prev] of [['t2', 't1'], ['t3', 't2']]) {
    const p = picks[prev], seen = new Set(), list = [];
    if (p && p.look) { list.push({ look: p.look, same: true }); seen.add(p.look); }
    for (const id of ids.slice().reverse()) if (list.length < 3 && !seen.has(id)) { list.push({ look: id }); seen.add(id); }
    out[t] = list;
  }
  const p3 = (picks.t3 && picks.t3.look) || ids[0];
  out.t4 = ids.filter(id => id !== p3).slice(0, 2).map(id => ({ look: p3, layer: id })).concat([{ look: p3, blaze: true }]);
  return out;
}
function lookName(c, o) {
  const n = id => (c.looks.find(l => l.id === id) || {}).name || id;
  return o.layer ? `${n(o.look)} + ${n(o.layer)}` : o.blaze ? `${n(o.look)}, ablaze` : o.same ? `${n(o.look)}, stronger` : n(o.look);
}
function optKey(o) { return o.look + (o.layer ? '+' + o.layer : '') + (o.blaze ? '!' : ''); }

function paintWeapons(full) {
  const box = $('#wbody');
  if (full) clearViews();
  if (wTab === 'classes') {
    if (full) {
      box.innerHTML = `<p class="hint">Each class keeps one model through every tier; the tiers make its look stronger (I faint, II a glowing edge, III full, Legendary all of it plus a second look). The looks are now made on the model itself: the steel glows through its own surface, every particle leaves a real point of the blade or the heads, and the light round it is a 3D volume, so no angle shows a flat card or an outline. The models wear Skyrim's own textures and normal maps. Drag to turn, scroll to zoom.</p>
<label class="cmp"><input type="checkbox" id="cmpold"> Compare with the old flat art (what the game draws today)</label>
<div class="chips" id="clsch">${D.classes.map((c, i) => `<button class="chip" data-cls="${c.id}" aria-pressed="${i === 0}">${esc(c.name)}</button>`).join('')}</div><div id="clsbody"></div>`;
      $('#cmpold').onchange = e => { COMPARE = e.target.checked; VIEWS.forEach(v => v.setCompare && v.setCompare(COMPARE)); };
      $('#clsch').onclick = e => { const b = e.target.closest('.chip'); if (!b) return; $$('#clsch .chip').forEach(x => x.setAttribute('aria-pressed', String(x === b))); drawClass(b.dataset.cls); };
      drawClass(D.classes[0].id);
    } else paintClass();
  } else if (wTab === 'kits') {
    if (full) drawKits(); else paintKits();
  } else if (wTab === 'tablet') {
    if (full) drawTablet(); else paintTablet();
  } else {
    box.innerHTML = `<p class="hint">The Test Range: a place in the game where each new effect plays on dummies, one labelled step at a time, so you can record it with NVIDIA. Your clips come from your Videos folder into this page, next to what they show. It comes once the element kits are picked.</p>
${(D.clips || []).length ? `<div class="wgrid">${D.clips.map(c => `<div class="clip"><video src="${esc(c.file)}" controls preload="metadata" playsinline></video><div class="wb"><b>${esc(c.title)}</b><p class="muted">${esc(c.when)}</p></div></div>`).join('')}</div>` : '<p class="empty">No clips yet.</p>'}`;
  }
}
let curCls = null;
function drawClass(id) {
  clearViews(); curCls = id; tierSig = '';
  const c = D.classes.find(x => x.id === id), box = $('#clsbody');
  box.innerHTML = `<div class="classblock" data-cls="${c.id}"><div class="classhead"><h4>${esc(c.name)}: ${esc(c.weapon)}</h4><span>Today: ${esc(c.now)}</span></div>
<h3 class="sub">1. The model</h3>
<div class="wgrid">${c.cands.map(m => `<article class="wcard" data-key="${m.key}"><canvas aria-label="${esc(m.name)}, turnable 3D model"></canvas><div class="wb"><h4>${esc(m.name)}</h4><p>${esc(m.src)}</p>
<div class="row"><button class="btn pass" data-pick="${m.key}" aria-pressed="false">Pick this model</button></div></div></article>`).join('')}</div>
<textarea rows="1" placeholder="A note on the ${esc(c.name)}'s model (optional)" aria-label="Note on the ${esc(c.name)} model" style="margin-top:10px" data-note="model"></textarea><span class="saved" data-sv="model"></span>
<div id="tiers"></div></div>`;
  $$('.wcard[data-key]', box).forEach(a => view3d($('canvas', a), a.dataset.key, { look: 'o1', gain: 0.8 }));
  box.onclick = async e => {
    const pb = e.target.closest('button[data-pick]');
    if (pb) { const prev = DB.get('cwpick', c.id) || {}; await saveTo('cwpick', c.id, { model: prev.model === pb.dataset.pick ? '' : pb.dataset.pick, note: $('[data-note="model"]', box).value.slice(0, 2000) }, $('[data-sv="model"]', box)); paintClass(true); return; }
    const tb = e.target.closest('button[data-tier]');
    if (tb) { const prev = DB.get('tierlook', c.id) || {}, t = tb.dataset.tier, ok = tb.dataset.opt, cur = prev[t] && optKey(prev[t]) === ok;
      const o = JSON.parse(tb.dataset.o); const row = Object.assign({}, prev, { [t]: cur ? null : o });
      await saveTo('tierlook', c.id, row, $('[data-sv="tiers"]', box)); paintClass(true); }
  };
  box.onchange = async e => {
    if (e.target.dataset.note === 'model') { const prev = DB.get('cwpick', c.id) || {}; await saveTo('cwpick', c.id, { model: prev.model || '', note: e.target.value.slice(0, 2000) }, $('[data-sv="model"]', box)); }
    if (e.target.dataset.note === 'tiers') { const prev = DB.get('tierlook', c.id) || {}; await saveTo('tierlook', c.id, Object.assign({}, prev, { note: e.target.value.slice(0, 2000) }), $('[data-sv="tiers"]', box)); }
  };
  paintClass(true);
}
let tierSig = '';
function paintClass(rebuildTiers) {
  if (!curCls) return;
  const c = D.classes.find(x => x.id === curCls), box = $('#clsbody'); if (!box) return;
  const p = DB.get('cwpick', c.id) || {};
  $$('.wcard[data-key]', box).forEach(a => { const on = p.model === a.dataset.key; a.dataset.picked = String(on); const b = $('button[data-pick]', a); b.setAttribute('aria-pressed', String(on)); b.textContent = on ? 'Your pick' : 'Pick this model'; });
  const ta = $('[data-note="model"]', box); if (document.activeElement !== ta) ta.value = p.note || '';
  const picks = DB.get('tierlook', c.id) || {}, key = p.model;
  const opts = tierOptions(c, picks), sig = key + '|' + JSON.stringify(opts);
  const tiers = $('#tiers', box);
  if (!key) { tiers.innerHTML = `<h3 class="sub">2. Its look at each tier</h3><p class="empty">Pick the model first: its tier looks show here, on it.</p>`; tierSig = ''; return; }
  if (sig !== tierSig) {
    tierSig = sig;
    VIEWS.filter(v => v.cv.closest('#tiers')).forEach(v => { GL.remove(v); VIEWS.splice(VIEWS.indexOf(v), 1); });
    tiers.innerHTML = `<h3 class="sub">2. Its look at each tier</h3><p class="hint">On the ${esc((c.cands.find(m => m.key === key) || {}).name || 'model')}. Each tier's options build on your pick for the tier before it.</p>`
      + TIERS.map(([t, tn, g]) => `<div class="tierrow" data-t="${t}"><div class="classhead"><h4 style="font-size:18px">${tn}</h4><span>${t === 't1' ? 'Faint: the look just waking' : t === 't2' ? 'A glowing edge' : t === 't3' ? 'Full: the whole look' : 'All of it, plus a second look'}</span></div>
<div class="wgrid">${opts[t].map(o => `<article class="wcard" data-opt="${optKey(o)}"><canvas aria-label="${esc(lookName(c, o))} at ${tn}, turning model"></canvas><div class="wb"><h4>${esc(lookName(c, o))}</h4>
<p>${esc(o.layer ? 'Your Tier III look with this one layered over it.' : o.blaze ? 'Your Tier III look at its brightest.' : (c.looks.find(l => l.id === o.look) || {}).desc || '')}</p>
<div class="row"><button class="btn pass" data-tier="${t}" data-opt="${optKey(o)}" data-o='${esc(JSON.stringify(o))}' aria-pressed="false">Pick for ${tn}</button></div></div></article>`).join('')}</div></div>`).join('')
      + `<textarea rows="2" data-note="tiers" placeholder="Anything about the tier looks (optional)" aria-label="Note on the tier looks" style="margin-top:12px"></textarea><span class="saved" data-sv="tiers"></span><div id="abil"></div>`;
    for (const [t, , g] of TIERS) $$(`.tierrow[data-t="${t}"] .wcard`, tiers).forEach((a, i) => { const o = opts[t][i];
      view3d($('canvas', a), key, { look: o.look, gain: o.blaze ? 1.6 : g, layer: o.layer || null, layerGain: 0.75, spin: true }); });
    drawAbil(c, key, (picks.t3 && picks.t3.look) || 'o1');
  }
  paintAbil(c);
  for (const [t] of TIERS) $$(`.tierrow[data-t="${t}"] .wcard`, tiers).forEach(a => { const on = !!(picks[t] && optKey(picks[t]) === a.dataset.opt); a.dataset.picked = String(on); const b = $('button', a); b.setAttribute('aria-pressed', String(on)); });
  const tn = $('[data-note="tiers"]', tiers); if (tn && document.activeElement !== tn) tn.value = picks.note || '';
}

// ---- the class weapon's abilities, built from its element's kit, shown in action (you swinging at the dead)
const ROLE_N = { impact: 'Impact', burst: 'Burst', ground: 'Ground', above: 'From above' };
const ABVIEWS = {};
function abilEl(c) { const a = D.abil[c.id], p = DB.get('abil', c.id) || {}; return a.els.includes(p.el) ? p.el : a.els[0]; }
function drawAbil(c, key, look) {
  const a = D.abil[c.id], box = $('#abil'); if (!box || !a) return;
  const el = abilEl(c), kit = D.kits.find(k => k.id === el);
  const optsFor = roles => roles.flatMap(r => kit.parts.find(p => p.id === r).opts.map(o => ({ v: r + ':' + o.id, n: (r === 'impact' ? '' : ROLE_N[r] + ': ') + o.label, about: o.about })));
  box.innerHTML = `<h3 class="sub">3. Its abilities</h3><p class="hint">Made here, from the ${esc(kit.name)} kit: you (a Nord in the ${esc(c.name)}'s set, holding this model in your Tier III look) strike the night's own dead, and they react the way the game makes them. Click an option to watch it, then Use it. Your kit's own pick for that part is marked.</p>
${a.els.length > 1 ? `<div class="chips" id="abel">${a.els.map(e => { const k = D.kits.find(x => x.id === e); return `<button class="chip" data-abel="${e}" aria-pressed="${e === el}"><span class="dot" style="background:${k.col}"></span>${esc(k.name)}</button>`; }).join('')}<span class="muted" style="align-self:center">The rapier follows the spell in your other hand: one ability per element.</span></div>` : ''}
<div class="abgrid">${['light', 'heavy'].map(w => { const ab = a[w]; return `<div class="abcol" data-ab="${w}"><div class="classhead"><h4 style="font-size:18px">${w === 'light' ? 'Light attack' : 'Heavy attack'}</h4><span>${esc(ab.what)}</span></div>
<canvas aria-label="${w} attack, playing"></canvas>
<div class="chips abopts">${optsFor(ab.roles).map(o => `<button class="chip" data-abo="${o.v}" title="${esc(o.about)}">${esc(o.n)}<span class="kitmark" data-km="${o.v}"></span></button>`).join('')}</div>
<p class="muted abnow"></p><div class="row"><button class="btn pass" data-abuse="${w}" aria-pressed="false">Use this</button><span class="saved" data-sv="ab_${w}"></span></div></div>`; }).join('')}</div>
<textarea rows="2" data-note="abil" placeholder="Anything about the ${esc(c.name)}'s abilities (optional)" aria-label="Note on the ${esc(c.name)} abilities" style="margin-top:12px"></textarea><span class="saved" data-sv="abil"></span>`;
  for (const w of ['light', 'heavy']) {
    const col = $(`.abcol[data-ab="${w}"]`, box), cv = $('canvas', col);
    if (!GL.ok) continue;
    const v = actionView(cv); VIEWS.push(v); ABVIEWS[w] = v; v.setPlayer(c.id, key, look);
    const p = DB.get('abil', c.id) || {}, saved = (p[el] || {})[w], first = saved || (a[w].roles[0] + ':' + kit.parts.find(x => x.id === a[w].roles[0]).opts[0].id);
    playAbil(c, w, first);
  }
  box.onclick = async e => {
    const eb = e.target.closest('button[data-abel]');
    if (eb) { const prev = DB.get('abil', c.id) || {}; await saveTo('abil', c.id, Object.assign({}, prev, { el: eb.dataset.abel }), $('[data-sv="abil"]', box)); VIEWS.filter(v => v.cv.closest('#abil')).forEach(v => { GL.remove(v); VIEWS.splice(VIEWS.indexOf(v), 1); }); drawAbil(c, key, look); return; }
    const ob = e.target.closest('button[data-abo]');
    if (ob) { playAbil(c, ob.closest('.abcol').dataset.ab, ob.dataset.abo); return; }
    const ub = e.target.closest('button[data-abuse]');
    if (ub) { const w = ub.dataset.abuse, colEl = ub.closest('.abcol'), cur = colEl.dataset.playing, prev = DB.get('abil', c.id) || {}, elp = Object.assign({}, prev[el] || {});
      elp[w] = elp[w] === cur ? '' : cur; await saveTo('abil', c.id, Object.assign({}, prev, { [el]: elp }), $(`[data-sv="ab_${w}"]`, box)); paintAbil(c); }
  };
  box.onchange = async e => { if (e.target.dataset.note !== 'abil') return; const prev = DB.get('abil', c.id) || {}; await saveTo('abil', c.id, Object.assign({}, prev, { note: e.target.value.slice(0, 2000) }), $('[data-sv="abil"]', box)); };
  paintAbil(c);
}
function playAbil(c, w, val) {
  const a = D.abil[c.id], el = abilEl(c), [role, opt] = val.split(':'), box = $('#abil'); if (!box) return;
  const col = $(`.abcol[data-ab="${w}"]`, box); col.dataset.playing = val;
  $$('button[data-abo]', col).forEach(b => b.dataset.on = String(b.dataset.abo === val));
  const kit = D.kits.find(k => k.id === el), o = kit.parts.find(p => p.id === role).opts.find(x => x.id === opt);
  $('.abnow', col).textContent = (ROLE_N[role] + ': ' + o.label + ' - ' + o.about);
  const v = ABVIEWS[w]; if (v) v.ability({ el, act: a[w].act, cast: !!a[w].cast, role, opt });
  paintAbil(c);
}
function paintAbil(c) {
  const box = $('#abil'); if (!box || !D.abil[c.id]) return;
  const el = abilEl(c), p = DB.get('abil', c.id) || {}, kp = DB.get('kits', el) || {};
  for (const w of ['light', 'heavy']) {
    const col = $(`.abcol[data-ab="${w}"]`, box); if (!col) continue;
    const saved = (p[el] || {})[w];
    $$('button[data-abo]', col).forEach(b => b.setAttribute('aria-pressed', String(b.dataset.abo === saved)));
    $$('[data-km]', col).forEach(m => { const [r, o] = m.dataset.km.split(':'); m.textContent = kp['p_' + r] === o ? ' (kit pick)' : ''; });
    const ub = $('button[data-abuse]', col), on = saved && saved === col.dataset.playing; ub.setAttribute('aria-pressed', String(!!on)); ub.textContent = on ? 'In use' : 'Use this';
  }
  const ta = $('[data-note="abil"]', box); if (ta && document.activeElement !== ta) ta.value = p.note || '';
}

// ---- element kits: Skyrim's own effects played live, Skyrim's own sounds played on a click
let AUD = null;
function stopSound() { if (AUD) { AUD.pause(); AUD = null; } }
function playSound(mp3) { stopSound(); AUD = new Audio('snd/' + mp3); AUD.volume = 0.8; AUD.play().catch(() => {}); }
const prettySnd = n => n.replace(/^coa:/, 'Curse: ').replace(/^WB_/, '').replace(/_Sound_|_Sound$|Sound_/g, ' ').replace(/SFX$|SD$|LPM?S?D?$|2D/g, '').replace(/_/g, ' ').replace(/([a-z])([A-Z])/g, '$1 $2').replace(/\s+/g, ' ').trim();
function drawKits() {
  const box = $('#wbody');
  box.innerHTML = `<p class="hint">Each element has one kit, and everything of that element (weapon abilities, tier looks, spells, flasks) is built from it. These are the Curse's own effects, made fully in 3D: fire, smoke, mist and light are real volumes you can see into from any side, sparks and embers are thousands of particles, lightning is a branching bolt struck again every few frames, ice and stone are real 3D crystals and rocks. In the game they play from 4K textures baked from these same shaders, with a 2K option in the MCM for weaker PCs (your pick). Drag to turn round each one, scroll to zoom. Pick one design for each part, and the sounds.</p>
<div class="chips" id="kitch">${D.kits.map(k => `<button class="chip" data-k="${k.id}" aria-pressed="${k.id === kitEl}" style="--kc:${k.col}"><span class="dot" style="background:${k.col}"></span>${esc(k.name)}<span class="kn" data-kn="${k.id}"></span></button>`).join('')}</div><div id="kitbody"></div>`;
  $('#kitch').onclick = e => { const b = e.target.closest('.chip'); if (!b) return; kitEl = b.dataset.k; $$('#kitch .chip').forEach(x => x.setAttribute('aria-pressed', String(x === b))); drawKit(); };
  drawKit();
}
function drawKit() {
  clearViews();
  const k = D.kits.find(x => x.id === kitEl), box = $('#kitbody');
  box.innerHTML = `<div class="kithead" style="--c:${k.col}"><h4>${esc(k.name)}</h4><p>${esc(k.about)}</p></div>
${k.parts.map(p => `<h3 class="sub">${esc(p.name)} <span class="muted" style="text-transform:none;letter-spacing:0;font:400 13px Inter,sans-serif">${esc(p.about)}</span></h3>
<div class="wgrid">${p.opts.map(o => `<article class="wcard" data-part="${p.id}" data-o="${o.id}"><canvas aria-label="${esc(o.label)}, playing"></canvas><div class="wb"><h4>${esc(o.label)}</h4><p>${esc(o.about)}</p>
<div class="row"><button class="btn pass" data-kpick="${p.id}" data-o="${o.id}" aria-pressed="false">Pick</button></div></div></article>`).join('')}</div>`).join('')}
<h3 class="sub">Sounds</h3>
<div class="sndgrid">${k.sounds.map(s => `<div class="sndcol"><b>${esc(s.name)}</b>${s.opts.map(o => `<div class="snd" data-sp="${s.id}" data-o="${o.id}"><button class="play" data-mp3="${esc(o.mp3)}" aria-label="Play ${esc(prettySnd(o.id))}">&#9654;</button><span>${esc(prettySnd(o.id))}<small>${esc(o.src)}</small></span><button class="btn pass" data-spick="${s.id}" data-o="${o.id}" aria-pressed="false">Pick</button></div>`).join('')}</div>`).join('')}</div>
<textarea rows="2" data-note="kit" placeholder="Anything about the ${esc(k.name)} kit (optional)" aria-label="Note on the ${esc(k.name)} kit" style="margin-top:14px"></textarea><span class="saved" data-sv="kit"></span>`;
  $$('.wcard[data-part]', box).forEach(a => kitView3d($('canvas', a), k.id, a.dataset.part, a.dataset.o));
  box.onclick = async e => {
    const pl = e.target.closest('button.play'); if (pl) { playSound(pl.dataset.mp3); return; }
    const b = e.target.closest('button[data-kpick],button[data-spick]'); if (!b) return;
    const prev = DB.get('kits', k.id) || {}, f = b.dataset.kpick ? 'p_' + b.dataset.kpick : 's_' + b.dataset.spick;
    await saveTo('kits', k.id, Object.assign({}, prev, { [f]: prev[f] === b.dataset.o ? '' : b.dataset.o }), $('[data-sv="kit"]', box)); paintKits();
  };
  box.onchange = async e => { if (e.target.dataset.note !== 'kit') return; const prev = DB.get('kits', k.id) || {}; await saveTo('kits', k.id, Object.assign({}, prev, { note: e.target.value.slice(0, 2000) }), $('[data-sv="kit"]', box)); };
  paintKits();
}
function paintKits() {
  const box = $('#kitbody'); if (!box) return;
  const k = D.kits.find(x => x.id === kitEl), p = DB.get('kits', k.id) || {};
  $$('button[data-kpick]', box).forEach(b => { const on = p['p_' + b.dataset.kpick] === b.dataset.o; b.setAttribute('aria-pressed', String(on)); b.textContent = on ? 'Your pick' : 'Pick'; b.closest('.wcard').dataset.picked = String(on); });
  $$('button[data-spick]', box).forEach(b => { const on = p['s_' + b.dataset.spick] === b.dataset.o; b.setAttribute('aria-pressed', String(on)); b.textContent = on ? 'Your pick' : 'Pick'; });
  const ta = $('[data-note="kit"]', box); if (ta && document.activeElement !== ta) ta.value = p.note || '';
  D.kits.forEach(x => { const q = DB.get('kits', x.id) || {}, n = x.parts.filter(pp => q['p_' + pp.id]).length + x.sounds.filter(s => q['s_' + s.id]).length, tot = x.parts.length + x.sounds.length, el = $(`[data-kn="${x.id}"]`); if (el) el.textContent = n ? ` ${n}/${tot}` : ''; });
}
// (one kit part, played on the night ground among three of the dead for scale, turning slowly)
function kitView3d(cv, el, role, opt) {
  if (!GL.ok) return;
  const v = fxStage(cv, { az: .6 + Math.random() * .5, el: role === 'above' ? .28 : .34, r: role === 'impact' ? 430 : role === 'above' ? 900 : 700, ty: role === 'above' ? 150 : role === 'impact' ? 50 : 40 });
  for (const [x, z] of [[-170, -110], [150, -140], [60, 190]]) {
    const i = VIEWS.length + x;
    zomData(ZOM_KEYS[Math.abs(i) % ZOM_KEYS.length]).then(([sk, body]) => { const zz = makeZombie(sk, body, Math.random()); v.scene.add(zz.holder); const info = { home: new THREE.Vector3(x, 0, z), from: new THREE.Vector3(0, 0, 0), face: Math.atan2(x, z) }; zPose(zz, [], 0, info); v.zs = (v.zs || []).concat([[zz, info]]); }).catch(() => {});
  }
  v.play(makeEffect(el, role, opt, { at: [0, 0, 0], seed: 3 + role.length }));
  const base = v.tick; v.tick = t => { base(t); for (const [zz, info] of (v.zs || [])) zPose(zz, [], t, info); };
  VIEWS.push(v);
}

// ---- the Tablet: its lineup by rarity, its roll, new weapons to choose from, today's marks
const RCOL = { rare: '#3d8bff', epic: '#a34bff', legendary: '#ffb52e' }, RNAME = { rare: 'Rare', epic: 'Epic', legendary: 'Legendary' };
let rollRar = 'epic';
function tabLineup() {
  const kept = D.tablet.filter(w => (DB.get('tablet', w.id) || {}).choice === 'keep').map(w => ({ id: w.id, name: w.name, kind: w.kind, does: w.does, src: 'kept' }));
  const added = D.tabnew.concat(D.tabnew2).filter(w => (DB.get('tabnew', w.id) || {}).choice === 'add').map(w => ({ id: w.id, name: w.name, kind: w.kind, does: [w.light, w.heavy !== '-' ? 'Heavy: ' + w.heavy : '', w.special].filter(Boolean).join(' '), src: 'added', el: w.el }));
  return kept.concat(added);
}
const rarOf = id => ((DB.get('tabrar', 'all') || {})[id]) || D.rarity[id] || 'rare';
function drawTablet() {
  clearViews();
  const box = $('#wbody');
  const newCard = w => { const k = D.kits.find(x => x.id === w.el), r = D.rarity[w.id] || 'rare'; return `<article class="wcard tnew" data-tn="${w.id}" style="--c:${k ? k.col : '#888'}"><div class="tnhead"><span class="dot" style="background:${k ? k.col : '#888'}"></span><span class="lbl">${esc(k ? k.name : w.el)} &middot; ${esc(w.kind)}</span><span class="rar" style="--r:${RCOL[r]}">${RNAME[r]}</span></div><div class="wb"><h4>${esc(w.name)}</h4>
<p><b>Light:</b> ${esc(w.light)}</p>${w.heavy && w.heavy !== '-' ? `<p><b>Heavy:</b> ${esc(w.heavy)}</p>` : ''}<p><b>Special:</b> ${esc(w.special)}</p>
<p class="muted" style="font-size:12.5px">Made from: ${esc(w.base)}. BO3: ${esc(w.bo3)}.</p>
<div class="row"><button class="btn pass" data-tnc="add" aria-pressed="false">Add it</button><button class="btn skip" data-tnc="maybe" aria-pressed="false">Maybe</button><button class="btn fail" data-tnc="no" aria-pressed="false">No</button></div>
<textarea rows="1" placeholder="What to change (optional)" aria-label="Note on ${esc(w.name)}"></textarea><span class="saved"></span></div></article>`; };
  box.innerHTML = `<p class="hint">The Tablet is the Curse's Mystery Box. Its weapons now come in three rarities by how strong their abilities are: <b style="color:${RCOL.rare}">Rare</b>, <b style="color:${RCOL.epic}">Epic</b> and <b style="color:${RCOL.legendary}">Legendary</b>, and rarer ones roll less often (your pick). Their uses get rebalanced once you finalize the lineup.</p>
<h3 class="sub">1. The lineup, by rarity</h3>
<p class="hint">Your kept weapons and the ones you added. I sorted them by strength; move any with its R / E / L buttons.</p>
<div class="rargrid" id="rargrid"></div>
<div class="odds" id="odds"><b>How often each rolls</b>${['rare', 'epic', 'legendary'].map(r => `<label style="--r:${RCOL[r]}">${RNAME[r]} <input type="range" min="0" max="100" step="1" data-odds="${r}"><output data-oo="${r}"></output></label>`).join('')}<span class="muted" id="oddsum"></span><span class="saved" data-sv="odds"></span></div>
<h3 class="sub">2. The roll</h3>
<p class="hint">Four ways for the Tablet to roll, on the Tablet itself with its weapons. See each in every rarity's colour, then pick one.</p>
<div class="chips" id="rollrar">${['rare', 'epic', 'legendary'].map(r => `<button class="chip" data-rr="${r}" aria-pressed="${r === rollRar}"><span class="dot" style="background:${RCOL[r]}"></span>${RNAME[r]} win</button>`).join('')}</div>
<div class="wgrid">${D.rolls.map(r => `<article class="wcard" data-roll="${r.id}"><canvas aria-label="${esc(r.name)}, playing"></canvas><div class="wb"><h4>${esc(r.name)}</h4><p>${esc(r.about)}</p><div class="row"><button class="btn pass" data-rpick="${r.id}" aria-pressed="false">Pick this roll</button></div></div></article>`).join('')}</div>
<textarea rows="1" data-note="roll" placeholder="Anything about the roll (optional)" aria-label="Note on the roll" style="margin-top:10px"></textarea><span class="saved" data-sv="roll"></span>
<h3 class="sub">3. More new weapons to choose from</h3>
<p class="hint">12 more, made from Skyrim's own weapons and lore with a BO3 Zombies power each; none like the five you said no to. My suggested rarity is on each.</p>
<div class="wgrid">${D.tabnew2.map(newCard).join('')}</div>
<div class="status-line" id="tnsum"></div>
<h3 class="sub">4. The ones you already added</h3>
<div class="wgrid">${D.tabnew.filter(w => (DB.get('tabnew', w.id) || {}).choice === 'add').map(newCard).join('')}</div>
<details class="more"><summary>The ones you said no to, and today's weapons you cut</summary><div class="wgrid">${D.tabnew.filter(w => (DB.get('tabnew', w.id) || {}).choice !== 'add').map(newCard).join('')}</div></details>
<h3 class="sub">5. Today's weapons: your marks</h3>
<div class="wgrid">${D.tablet.map(w => `<article class="wcard" data-tb="${w.id}">${w.key ? '<canvas aria-label="' + esc(w.name) + ', turnable 3D model"></canvas>' : '<div class="noview">No preview</div>'}<div class="wb"><h4>${esc(w.name)}</h4><span class="lbl">${esc(w.kind)}</span><p>${esc(w.does)}</p>
<div class="row"><button class="btn pass" data-tc="keep" aria-pressed="false">Keep</button><button class="btn skip" data-tc="rework" aria-pressed="false">Rework</button><button class="btn fail" data-tc="cut" aria-pressed="false">Cut</button></div>
<textarea rows="1" placeholder="Why, or what to change (optional)" aria-label="Note on ${esc(w.name)}"></textarea><span class="saved"></span></div></article>`).join('')}</div>
<h3 class="sub">Your own ideas</h3><div data-tb="_ideas"><textarea rows="3" placeholder="Any weapon you'd like in the Tablet: what it is and what it does" aria-label="Ideas for new Tablet weapons"></textarea><span class="saved"></span></div>`;
  $$('.wcard[data-tb]', box).forEach(a => { const w = D.tablet.find(x => x.id === a.dataset.tb); if (w.key) view3d($('canvas', a), w.key, { gain: 0.8 }); });
  if (GL.ok) $$('.wcard[data-roll]', box).forEach(a => { const v = rollView($('canvas', a), a.dataset.roll); v.setRarity(rollRar); VIEWS.push(v); a.__v = v; });
  box.onclick = async e => {
    const b = e.target.closest('button[data-tc]');
    if (b) { const card = b.closest('[data-tb]'), prev = DB.get('tablet', card.dataset.tb) || {}; await saveTo('tablet', card.dataset.tb, { choice: prev.choice === b.dataset.tc ? '' : b.dataset.tc, note: $('textarea', card).value.slice(0, 2000) }, $('.saved', card)); paintTablet(); return; }
    const n = e.target.closest('button[data-tnc]');
    if (n) { const card = n.closest('[data-tn]'), prev = DB.get('tabnew', card.dataset.tn) || {}; await saveTo('tabnew', card.dataset.tn, { choice: prev.choice === n.dataset.tnc ? '' : n.dataset.tnc, note: $('textarea', card).value.slice(0, 2000) }, $('.saved', card)); paintTablet(); return; }
    const rr = e.target.closest('button[data-rr]');
    if (rr) { rollRar = rr.dataset.rr; $$('#rollrar .chip').forEach(c => c.setAttribute('aria-pressed', String(c === rr))); $$('.wcard[data-roll]', box).forEach(a => a.__v && a.__v.setRarity(rollRar)); return; }
    const rp = e.target.closest('button[data-rpick]');
    if (rp) { const prev = DB.get('tabroll', 'pick') || {}; await saveTo('tabroll', 'pick', Object.assign({}, prev, { pick: prev.pick === rp.dataset.rpick ? '' : rp.dataset.rpick }), $('[data-sv="roll"]', box)); paintTablet(); return; }
    const mv = e.target.closest('button[data-mv]');
    if (mv) { const prev = DB.get('tabrar', 'all') || {}; await saveTo('tabrar', 'all', Object.assign({}, prev, { [mv.dataset.id]: mv.dataset.mv }), $('[data-sv="odds"]', box)); paintTablet(); }
  };
  box.oninput = e => { const r = e.target.dataset.odds; if (!r) return; $(`[data-oo="${r}"]`, box).textContent = e.target.value + '%'; };
  box.onchange = async e => {
    const r = e.target.dataset.odds;
    if (r) { const prev = DB.get('tabrar', 'odds') || Object.assign({}, D.odds); await saveTo('tabrar', 'odds', Object.assign({}, prev, { [r]: +e.target.value }), $('[data-sv="odds"]', box)); paintTablet(); return; }
    if (e.target.dataset.note === 'roll') { const prev = DB.get('tabroll', 'pick') || {}; await saveTo('tabroll', 'pick', Object.assign({}, prev, { note: e.target.value.slice(0, 2000) }), $('[data-sv="roll"]', box)); return; }
    if (e.target.tagName !== 'TEXTAREA') return;
    const tb = e.target.closest('[data-tb]'), tn = e.target.closest('[data-tn]');
    if (tb) { const prev = DB.get('tablet', tb.dataset.tb) || {}; await saveTo('tablet', tb.dataset.tb, { choice: prev.choice || '', note: e.target.value.slice(0, 2000) }, $('.saved', tb)); }
    if (tn) { const prev = DB.get('tabnew', tn.dataset.tn) || {}; await saveTo('tabnew', tn.dataset.tn, { choice: prev.choice || '', note: e.target.value.slice(0, 2000) }, $('.saved', tn)); }
  };
  paintTablet();
}
function paintTablet() {
  const box = $('#wbody'); if (!box || wTab !== 'tablet') return;
  $$('[data-tb]', box).forEach(card => { const p = DB.get('tablet', card.dataset.tb) || {}; $$('button[data-tc]', card).forEach(b => b.setAttribute('aria-pressed', String(p.choice === b.dataset.tc))); const ta = $('textarea', card); if (document.activeElement !== ta) ta.value = p.note || ''; });
  let add = 0, maybe = 0;
  $$('[data-tn]', box).forEach(card => { const p = DB.get('tabnew', card.dataset.tn) || {}; $$('button[data-tnc]', card).forEach(b => b.setAttribute('aria-pressed', String(p.choice === b.dataset.tnc))); card.dataset.picked = String(p.choice === 'add'); const ta = $('textarea', card); if (document.activeElement !== ta) ta.value = p.note || ''; });
  D.tabnew2.forEach(w => { const c = (DB.get('tabnew', w.id) || {}).choice; if (c === 'add') add++; if (c === 'maybe') maybe++; });
  const line = tabLineup();
  const s = $('#tnsum'); if (s) s.textContent = `${add} of these added, ${maybe} maybe. The Tablet holds ${line.length} weapons now.`;
  const rg = $('#rargrid');
  if (rg) rg.innerHTML = ['rare', 'epic', 'legendary'].map(r => `<div class="rarcol" style="--r:${RCOL[r]}"><h4>${RNAME[r]} <span>${line.filter(w => rarOf(w.id) === r).length}</span></h4>${line.filter(w => rarOf(w.id) === r).map(w => `<div class="rarw"><b>${esc(w.name)}</b><span class="muted">${esc(w.kind)}${w.src === 'added' ? ' &middot; added' : ''}</span>
<div class="rarmv">${['rare', 'epic', 'legendary'].map(x => `<button class="rb" data-mv="${x}" data-id="${w.id}" aria-pressed="${x === r}" style="--r:${RCOL[x]}" title="Make it ${RNAME[x]}">${RNAME[x][0]}</button>`).join('')}</div></div>`).join('') || '<p class="empty">None</p>'}</div>`).join('');
  const odds = DB.get('tabrar', 'odds') || D.odds;
  $$('[data-odds]', box).forEach(i => { if (document.activeElement !== i) i.value = odds[i.dataset.odds]; $(`[data-oo="${i.dataset.odds}"]`, box).textContent = odds[i.dataset.odds] + '%'; });
  const tot = odds.rare + odds.epic + odds.legendary, os = $('#oddsum'); if (os) os.textContent = tot === 100 ? '' : `(they add up to ${tot}%; I'll scale them to 100%)`;
  const rp = (DB.get('tabroll', 'pick') || {}); $$('button[data-rpick]', box).forEach(b => { const on = rp.pick === b.dataset.rpick; b.setAttribute('aria-pressed', String(on)); b.textContent = on ? 'Your pick' : 'Pick this roll'; b.closest('.wcard').dataset.picked = String(on); });
  const rn = $('[data-note="roll"]', box); if (rn && document.activeElement !== rn) rn.value = rp.note || '';
}

// ================================================================ IN-GAME UI
let uTab = 'direction', uFilter = 'all';
const DIRS = [
  { id: 'tally', L: 'A', name: 'Ember Tally', tag: 'BO3\'s layout and punch, in Skyrim\'s fire',
    pts: ['The night as glowing ember tally marks (BO3\'s chalk round counter, scratched like runes).', 'Cursed Essence big and gold bottom right, with +10s that pop up like BO3\'s points.', 'Your brews as a row of round glowing icons bottom left, like perk-a-colas.', 'Skyrim\'s thin compass and its own bars; everything else hidden until it matters.'] },
  { id: 'bone', L: 'D', name: 'Dragonbone', tag: 'The dragon cult\'s carved bone and the blood moon\'s crimson; BO3\'s ornament',
    pts: ['Every readout on a small plaque of carved bone, its corners cut like a Dragon Priest\'s mask, a fine inner line.', 'The night\'s tally scratched in blood-red light; numbers and titles in Cinzel, the old Nordic inscription type.', 'Brews as bone-rimmed diamonds glowing their colour; Felldir\'s words in a calligraphic serif.', 'The most Skyrim of the three, and the most ornate: Shadows of Evil\'s art-deco ornament made Nordic.'] },
  { id: 'glass', L: 'E', name: 'Echo Glass', tag: 'The Echo Altar\'s soul crystal: frosted dark glass with light running in its edges',
    pts: ['Every panel is dark frosted glass that blurs the world behind it, a thin line of soul-light round its edge.', 'The night\'s tally drawn in cold blue-white light; clean modern type (Jost, the mod\'s own font).', 'Pale blue and violet - the colours of the altar crystal and BO3\'s Apothicon - with soft glows, nothing heavy.', 'The most modern and the cleanest; the most like a new game\'s HUD.'] },
];
const dirNow = () => DIRS.find(d => d.id === ((DB.get('uidir', 'pick') || {}).pick || 'tally')) || DIRS[0];
const UI_STATE_T = { in: 'In the game now (old look)', next: 'To be built in your direction', own: 'Skyrim\'s own: stays', removed: 'Removed', keep: 'Kept as it is in your game (your note)', menu: 'Pick one of the two for this menu' };
const RES = [['2560x1600', '16 / 10', '2560 x 1600 (16:10)'], ['1920x1080', '16 / 9', '1920 x 1080 (16:9)'], ['2560x1440', '16 / 9', '2560 x 1440 (16:9)'], ['3440x1440', '43 / 18', '3440 x 1440 (21:9)']];
let uRes = '2560x1600', uPlay = true;
function drawUI() {
  const el = $('section[data-s="ui"]');
  el.innerHTML = `<h2 class="sec">In-game UI</h2>
<p class="lead">Every screen of the mod, aiming for the perfect blend of Skyrim and Black Ops 3 Zombies, drawn over real stills from your own game. The direction you pick restyles every screen at once. Every box fades in and out with a soft gradient (playing here on a loop), and the whole UI fits any screen: pick a resolution to see it.</p>
<div class="uctl"><div class="chips" id="ures">${RES.map(([k, , n]) => `<button class="chip" data-res="${k}" aria-pressed="${k === uRes}">${n}</button>`).join('')}</div><label class="cmp"><input type="checkbox" id="uplay" checked> Play the fades</label></div>
<div class="chips" id="utabs">${[['direction', 'The direction'], ['screens', 'Every screen']].map(([k, n]) => `<button class="chip" data-u="${k}" aria-pressed="${k === uTab}">${n}</button>`).join('')}</div>
<div id="ubody"></div><div class="zoom" id="uzoom" hidden><div class="zin"></div><button class="btn" id="uzclose">Close</button></div>`;
  const applyRes = () => { const r = RES.find(x => x[0] === uRes); el.style.setProperty('--ar', r[1]); };
  applyRes();
  $('#ures').addEventListener('click', e => { const b = e.target.closest('.chip'); if (!b) return; uRes = b.dataset.res; $$('#ures .chip').forEach(c => c.setAttribute('aria-pressed', String(c === b))); applyRes(); });
  $('#uplay').addEventListener('change', e => { uPlay = e.target.checked; $$('.stage', el).forEach(st => st.classList.toggle('play', uPlay)); });
  $('#utabs').addEventListener('click', e => { const b = e.target.closest('.chip'); if (!b) return; uTab = b.dataset.u; $$('#utabs .chip').forEach(c => c.setAttribute('aria-pressed', String(c === b))); paintUI(true); });
  el.addEventListener('click', e => {   // (a still at full width, to read it as you would on your screen)
    const s = e.target.closest('.stage'); if (s && !e.target.closest('#uzoom')) { $('#uzoom .zin').innerHTML = s.outerHTML; $('#uzoom').hidden = false; return; }
    if (e.target.id === 'uzclose' || e.target.id === 'uzoom') $('#uzoom').hidden = true;
  });
  document.addEventListener('keydown', e => { if (e.key === 'Escape' && $('#uzoom')) $('#uzoom').hidden = true; });
  paintUI(true);
}
function paintUI(full) {
  const box = $('#ubody');
  if (uTab === 'direction') {
    if (full) {
      box.innerHTML = `<p class="hint">The same moment of a night (night 12, a trial running, 12,480 essence) in each direction, on a real still of your game. Click a still to see it full width. Your pick restyles every screen in Every screen.</p>
<div class="dgrid">${DIRS.map(d => `<article class="dcard" data-dir="${d.id}">${uiStage('night', d.L, NIGHT_HUD())}<div class="wb"><h4>${esc(d.name)}</h4><span class="lbl">${esc(d.tag)}</span><ul>${d.pts.map(p => `<li>${esc(p)}</li>`).join('')}</ul>
<div class="row"><button class="btn pass big" data-dpick="${d.id}" aria-pressed="false">Pick this direction</button></div></div></article>`).join('')}</div>
<div style="margin-top:14px;max-width:980px"><textarea rows="2" id="dnote" placeholder="What to take from the others, or anything to change (optional)" aria-label="Note on the UI direction"></textarea><span class="saved" id="dsaved"></span></div>`;
      box.onclick = async e => { const b = e.target.closest('button[data-dpick]'); if (!b) return; const prev = DB.get('uidir', 'pick') || {};
        await saveTo('uidir', 'pick', { pick: prev.pick === b.dataset.dpick ? '' : b.dataset.dpick, note: $('#dnote').value.slice(0, 2000) }, $('#dsaved')); paintUI(false); };
      box.onchange = async e => { if (e.target.id !== 'dnote') return; const prev = DB.get('uidir', 'pick') || {}; await saveTo('uidir', 'pick', { pick: prev.pick || '', note: e.target.value.slice(0, 2000) }, $('#dsaved')); };
    }
    const p = DB.get('uidir', 'pick') || {};
    $$('.dcard', box).forEach(c => { const on = p.pick === c.dataset.dir; c.dataset.picked = String(on); const b = $('button', c); b.setAttribute('aria-pressed', String(on)); b.textContent = on ? 'Your pick' : 'Pick this direction'; });
    const ta = $('#dnote'); if (ta && document.activeElement !== ta) ta.value = p.note || '';
  } else {
    const all = UI_SCREENS.flatMap(([g, gl, l]) => l.map(x => ({ g, gl, id: x[0], name: x[1], d: x[2], frame: x[3], draw: x[4], extra: x[5] })));
    const d = dirNow();
    if (full || box.dataset.dir !== d.id) {
      box.dataset.dir = d.id;
      box.innerHTML = `<p class="hint">Every screen in <b>${esc(d.name)}</b> (change it in The direction), on real stills of your game. Click a still to see it full width; Approve what's right, Change with a note what isn't.</p>
<div class="chips" id="ufil">${[['all', `Everything (${all.length})`]].concat(UI_SCREENS.map(([g, gl, l]) => [g, `${gl} (${l.length})`])).map(([g, n]) => `<button class="chip" data-g="${g}" aria-pressed="${g === uFilter}">${esc(n)}</button>`).join('')}</div>
<div class="ugrid">${all.map(x => `<article class="ucard" data-id="${x.id}" data-g="${x.g}">${uiStage(x.frame, d.L, x.draw(), x.extra)}<div class="wb"><h4>${esc(x.name)}</h4>${UI_STATE[x.id] ? `<span class="lbl">${UI_STATE_T[UI_STATE[x.id]]}</span>` : ''}<p>${esc(x.d)}</p>
${UI_STATE[x.id] === 'own' ? '' : UI_STATE[x.id] === 'menu' ? `<div class="row"><button class="btn pass" data-c="pick" aria-pressed="false">Use this one</button><button class="btn skip" data-c="change" aria-pressed="false">Change</button><span class="saved"></span></div>
<textarea rows="1" placeholder="What to change (optional)" aria-label="Note on ${esc(x.name)}"></textarea>` : `<div class="row"><button class="btn pass" data-c="ok" aria-pressed="false">Approve</button><button class="btn skip" data-c="change" aria-pressed="false">Change</button><span class="saved"></span></div>
<textarea rows="1" placeholder="What to change (optional)" aria-label="Note on ${esc(x.name)}"></textarea>`}</div></article>`).join('')}</div><div class="status-line" id="usum"></div>`;
      box.onclick = async e => {
        const f = e.target.closest('#ufil .chip'); if (f) { uFilter = f.dataset.g; $$('#ufil .chip').forEach(c => c.setAttribute('aria-pressed', String(c === f))); paintUI(false); return; }
        const b = e.target.closest('button[data-c]'); if (!b) return; const card = b.closest('.ucard'), prev = DB.get('uiscreens', card.dataset.id) || {};
        if (b.dataset.c === 'pick' && prev.choice !== 'pick') { const other = Object.keys(MENU_PAIRS).find(k => k !== card.dataset.id && MENU_PAIRS[k] === MENU_PAIRS[card.dataset.id]); const po = DB.get('uiscreens', other) || {}; if (po.choice === 'pick') await saveTo('uiscreens', other, Object.assign({}, po, { choice: '' }), null); }
        await saveTo('uiscreens', card.dataset.id, { choice: prev.choice === b.dataset.c ? '' : b.dataset.c, note: $('textarea', card).value.slice(0, 2000), dir: d.id }, $('.saved', card)); paintUI(false); };
      box.onchange = async e => { if (e.target.tagName !== 'TEXTAREA') return; const card = e.target.closest('.ucard'), prev = DB.get('uiscreens', card.dataset.id) || {};
        await saveTo('uiscreens', card.dataset.id, { choice: prev.choice || '', note: e.target.value.slice(0, 2000), dir: d.id }, $('.saved', card)); };
    }
    let ok = 0, ch = 0;
    $$('.ucard', box).forEach(card => { const p = DB.get('uiscreens', card.dataset.id) || {};
      card.hidden = uFilter !== 'all' && card.dataset.g !== uFilter;
      $$('button[data-c]', card).forEach(b => b.setAttribute('aria-pressed', String(p.choice === b.dataset.c)));
      if (p.choice === 'ok' || p.choice === 'pick') ok++; if (p.choice === 'change') ch++; card.dataset.picked = String(p.choice === 'pick');
      const ta = $('textarea', card); if (ta && document.activeElement !== ta) ta.value = p.note || ''; });
    $('#usum').textContent = `${all.length} screens in ${d.name}: ${ok} approved, ${ch} to change.`;
  }
  $$('.stage', $('section[data-s="ui"]')).forEach(st => st.classList.toggle('play', uPlay));
}

// ================================================================ RELEASE: the road to Nexus, the mod stack, the design bible
function drawRelease() {
  const el = $('section[data-s="release"]'), B = D.bible;
  const stMap = { done: 'done', doing: 'building', testing: 'ready', todo: 'idea', next: 'idea' };
  el.innerHTML = `<h2 class="sec">Release</h2>
<p class="lead">Where the mod stands on the way to Nexus, the mods it plays with, and the design bible every decision is checked against.</p>
<h3 class="sub">The design bible</h3>
<p class="motto">Skyrim look, <span>BO3 punch.</span></p>
<p class="lead" style="margin-bottom:14px">Black Ops 3 Zombies, played in Skyrim: each piece of Zombies and what it is in The Curse of Alduin.</p>
<div class="tablewrap"><table class="bible-map"><thead><tr><th>In BO3 Zombies</th><th>In the Curse</th><th>How</th></tr></thead><tbody>${B.map.map(([a, b, c]) => `<tr><td>${esc(a)}</td><td>${esc(b)}</td><td class="muted">${esc(c)}</td></tr>`).join('')}</tbody></table></div>
<h3 class="sub">The rules every feature is held to</h3><div class="rules">${B.rules.map(([a, b]) => `<div><b>${esc(a)}</b><p>${esc(b)}</p></div>`).join('')}</div>
<h3 class="sub">How a feature gets done</h3><div class="flow">${B.flow.map(([a, b]) => `<div><b>${esc(a)}</b><p>${esc(b)}</p></div>`).join('')}</div>
<h3 class="sub">Road to Nexus</h3><p class="muted" style="margin-top:-4px;font-size:13px">Updated ${esc(D.nexus.updated)}.</p>
<div class="rgrid">${D.nexus.groups.map(([g, rows]) => `<div class="rbox"><h4>${esc(g)}</h4>${rows.map(r => `<div class="rrow"><span>${esc(r[1])}</span>${pill(stMap[r[2]] || 'idea')}${r[3] ? `<small>${esc(r[3])}</small>` : ''}</div>`).join('')}</div>`).join('')}</div>
<h3 class="sub">The mod stack</h3><p class="muted" style="margin-top:-4px;font-size:13px">${esc(D.modstack.about)}</p>
<div class="rgrid">${D.modstack.groups.map(([g, rows]) => `<div class="rbox"><h4>${esc(g)}</h4>${rows.map(r => `<div class="rrow"><span>${esc(r[1])}</span><span class="lbl">${esc(r[4] || r[3] || '')}</span><small>${esc(r[2])}</small></div>`).join('')}</div>`).join('')}</div>`;
}

// ================================================================ HISTORY: every build, installed or not, and your marks on it
function drawHistory() {
  const el = $('section[data-s="history"]');
  el.innerHTML = `<h2 class="sec">History</h2>
<p class="lead">Every build since Nov 24, newest first. The ones installed on your PC are marked; your test marks on this build are counted below.</p>
<div class="stats" id="hstats"></div><h3 class="sub">Builds</h3>
<h3 class="sub">Your notes, and what was done with them</h3>
<p class="hint">Each time I build from your notes, they are cleared from their boxes and kept here with what I did, so the boxes are empty for your next round.</p>
${(D.notelog || []).map(r => `<details class="more" open><summary>${esc(r.build)} &middot; ${r.notes.length} notes</summary><div class="nlog">${r.notes.map(n => `<div class="nrow"><span class="lbl">${esc(n.where.replace('/', ' > '))}</span><p class="q">"${esc(n.note)}"</p><p class="a">${esc(n.done)}</p></div>`).join('')}</div></details>`).join('')}
<h3 class="sub">Builds</h3>
<div class="hist">${D.builds.map(b => `<div class="hrow${D.installs[b.sha] ? ' inst' : ''}"><div><code>${esc(b.sha)}</code><div class="when">${esc(b.when)}</div>${D.installs[b.sha] ? '<span class="pill st-ready">Installed</span>' : ''}</div><p>${esc(b.what)}</p></div>`).join('')}</div>`;
  paintHistory();
}

// ================================================================ LORE: the story, the trials, the Easter eggs - read from the mod itself
function drawLore() {
  const el = $('section[data-s="lore"]');
  const qa = list => list && list.length ? `<div class="qa">${list.map(x => `<details><summary>${esc(x.q)}</summary>${x.a.map(t => `<p>${esc(t)}</p>`).join('')}</details>`).join('')}</div>` : '';
  el.innerHTML = `<h2 class="sec">Lore</h2>
<p class="lead">The story of The Curse of Alduin, its trials and its hidden quests, as the mod tells it now: read from the game's own text and Felldir's ${D.lore.reduce((n, c) => n + (c.qa || []).reduce((m, x) => m + x.a.length, 0), 0)} lines. Approve a chapter, or mark what to change with a note - fixes where it contradicts Skyrim are welcome.</p>
<nav class="lnav">${D.lore.map(c => `<a href="#lore" data-lj="${c.id}">${esc(c.title)}</a>`).join('')}</nav>
${D.lore.map(c => `<article class="lchap" id="lore-${c.id}" data-lid="${c.id}"><h3>${esc(c.title)}</h3><p class="llead">${esc(c.lead)}</p>
${(c.items || []).length ? `<div class="litems">${c.items.map(i => `<div class="litem"><b>${esc(i.t)}</b>${i.s ? `<span class="lbl">${esc(i.s)}</span>` : ''}<p>${esc(i.d)}</p></div>`).join('')}</div>` : ''}
${(c.eggs || []).map(g => `<div class="legg"><h4>${esc(g.t)} <span class="lbl">${esc(g.cls)}</span></h4>${g.open ? `<p class="lopen">"${esc(g.open)}"</p>` : ''}<ol>${g.steps.map(st => `<li><b>${esc(st.t)}</b> - ${esc(st.d)}${st.hint ? `<small>Felldir: "${esc(st.hint)}"</small>` : ''}</li>`).join('')}</ol><p class="muted">Reward: ${esc(g.reward)}</p>${g.whispers.length ? `<details><summary>Felldir's whispers for it (${g.whispers.length})</summary>${g.whispers.map(w => `<p>${esc(w)}</p>`).join('')}</details>` : ''}</div>`).join('')}
${c.qa && c.qa.length ? `<h4 class="lq">Felldir, asked</h4>${qa(c.qa)}` : ''}
<div class="row"><button class="btn pass" data-lc="ok" aria-pressed="false">Approve</button><button class="btn skip" data-lc="change" aria-pressed="false">Change</button><span class="saved"></span></div>
<textarea rows="2" placeholder="What to change in this chapter (optional)" aria-label="Note on ${esc(c.title)}"></textarea></article>`).join('')}`;
  el.onclick = async e => {
    const j = e.target.closest('[data-lj]'); if (j) { e.preventDefault(); $('#lore-' + j.dataset.lj).scrollIntoView({ behavior: 'smooth', block: 'start' }); return; }
    const b = e.target.closest('button[data-lc]'); if (!b) return; const card = b.closest('[data-lid]'), prev = DB.get('lore', card.dataset.lid) || {};
    await saveTo('lore', card.dataset.lid, { choice: prev.choice === b.dataset.lc ? '' : b.dataset.lc, note: $('textarea', card).value.slice(0, 2000) }, $('.saved', card)); paintLore();
  };
  el.onchange = async e => { if (e.target.tagName !== 'TEXTAREA') return; const card = e.target.closest('[data-lid]'), prev = DB.get('lore', card.dataset.lid) || {};
    await saveTo('lore', card.dataset.lid, { choice: prev.choice || '', note: e.target.value.slice(0, 2000) }, $('.saved', card)); };
  paintLore();
}
function paintLore() {
  if (!drawn.lore) return;
  $$('[data-lid]').forEach(card => { const p = DB.get('lore', card.dataset.lid) || {}; $$('button[data-lc]', card).forEach(b => b.setAttribute('aria-pressed', String(p.choice === b.dataset.lc))); const ta = $('textarea', card); if (document.activeElement !== ta) ta.value = p.note || ''; });
}
function paintHistory() {
  if (!drawn.history) return;
  const marks = Object.values(DB.all('marks')).filter(m => m.build === BUILD && m.r);
  $('#hstats').innerHTML = `<div class="stat"><b>${D.builds.length}</b><span>Builds</span></div><div class="stat"><b>${Object.keys(D.installs).length}</b><span>Installed</span></div><div class="stat green"><b>${marks.filter(m => m.r === 'pass').length}</b><span>Passed on ${esc(BUILD)}</span></div><div class="stat red"><b>${marks.filter(m => m.r === 'fail').length}</b><span>Failed on ${esc(BUILD)}</span></div>`;
}

const DRAW = { now: drawNow, systems: drawSystems, weapons: drawWeapons, ui: drawUI, lore: drawLore, release: drawRelease, history: drawHistory };
// (everything marked anywhere repaints what shows it)
DB.watch('marks', () => { paintBadge(); paintNow(); paintSystems(); paintHistory(); });
DB.watch('groups', () => { paintBadge(); paintNow(); paintSystems(); });
DB.watch('decisions', () => paintNow());
DB.watch('cwpick', () => { if (drawn.weapons && wTab === 'classes') paintClass(); });
DB.watch('tierlook', () => { if (drawn.weapons && wTab === 'classes') paintClass(); });
DB.watch('kits', () => { if (drawn.weapons && wTab === 'kits') paintKits(); });
DB.watch('tabnew', () => { if (drawn.weapons && wTab === 'tablet') paintTablet(); });
DB.watch('tablet', () => { if (drawn.weapons && wTab === 'tablet') paintTablet(); });
DB.watch('tabrar', () => { if (drawn.weapons && wTab === 'tablet') paintTablet(); });
DB.watch('tabroll', () => { if (drawn.weapons && wTab === 'tablet') paintTablet(); });
DB.watch('lore', () => paintLore());
DB.watch('abil', () => { if (drawn.weapons && wTab === 'classes' && curCls) paintAbil(D.classes.find(c => c.id === curCls)); });
DB.watch('uidir', () => { if (drawn.ui) paintUI(false); });
DB.watch('uiscreens', () => { if (drawn.ui && uTab === 'screens') paintUI(false); });
paintBadge();
route();
