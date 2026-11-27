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
const SECTIONS = ['now', 'systems', 'weapons', 'ui', 'release', 'history'];
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
let wTab = 'classes';
function drawWeapons() {
  const el = $('section[data-s="weapons"]');
  el.innerHTML = `<h2 class="sec">Weapons &amp; effects</h2>
<p class="lead">The foundation for every weapon and effect: one model per class weapon, the Tablet's new lineup, the element kits every effect will be built on, and your Test Range clips.</p>
<div class="chips" id="wtabs">${[['classes', 'Class weapons'], ['tablet', 'Tablet lineup'], ['elements', 'Element kits'], ['clips', 'Test Range clips']].map(([k, n]) => `<button class="chip" data-w="${k}" aria-pressed="${k === wTab}">${n}</button>`).join('')}</div>
<div id="wbody"></div>`;
  $('#wtabs').addEventListener('click', e => { const b = e.target.closest('.chip'); if (!b) return; wTab = b.dataset.w; $$('#wtabs .chip').forEach(c => c.setAttribute('aria-pressed', String(c === b))); paintWeapons(true); });
  paintWeapons(true);
}
const VIEWS = [];
function clearViews() { while (VIEWS.length) { const v = VIEWS.pop(); try { GL.remove(v); } catch (e) {} } }
function view3d(cv, key, bare) {
  if (!GL.ok || !key) return;
  const v = weaponView(cv); VIEWS.push(v);
  v.show(key, 'o1', 'own').then(() => { if (v.fx) v.fx.g.visible = !bare; });
  cv.__v = v;
}
function paintWeapons(full) {
  const box = $('#wbody');
  if (full) clearViews();
  if (wTab === 'classes') {
    if (full) {
      box.innerHTML = `<p class="hint">Your Nov 26 decision: each class keeps one model through every tier, and the tiers make its look stronger. Pick the best-looking model for each class; drag a model to turn it, scroll to zoom, double-click to reset. Whichever you pick is rebuilt from Skyrim's own meshes so it needs no mod.</p>`
        + D.classes.map(c => `<div class="classblock" data-cls="${c.id}"><div class="classhead"><h4>${esc(c.name)}: ${esc(c.weapon)}</h4><span>Today: ${esc(c.now)}</span></div>
<div class="wgrid">${c.cands.map(m => `<article class="wcard" data-key="${m.key}"><canvas aria-label="${esc(m.name)}, turnable 3D model"></canvas><div class="wb"><h4>${esc(m.name)}</h4><p>${esc(m.src)}</p>
<div class="row"><button class="btn pass" data-pick="${m.key}" aria-pressed="false">Pick this model</button><button class="chip" data-bare aria-pressed="false">Without its look</button></div></div></article>`).join('')}</div>
<textarea rows="1" placeholder="A note on the ${esc(c.name)}'s model (optional)" aria-label="Note on the ${esc(c.name)} model" style="margin-top:10px"></textarea><span class="saved"></span></div>`).join('');
      $$('.wcard', box).forEach(a => view3d($('canvas', a), a.dataset.key, false));
      box.onclick = async e => {
        const bb = e.target.closest('button[data-bare]');
        if (bb) { const on = bb.getAttribute('aria-pressed') !== 'true'; bb.setAttribute('aria-pressed', String(on)); const v = $('canvas', bb.closest('.wcard')).__v; if (v && v.fx) v.fx.g.visible = !on; return; }
        const pb = e.target.closest('button[data-pick]'); if (!pb) return;
        const blk = pb.closest('[data-cls]'), id = blk.dataset.cls, prev = DB.get('cwpick', id) || {};
        try { await DB.set('cwpick', id, { model: prev.model === pb.dataset.pick ? '' : pb.dataset.pick, note: $('textarea', blk).value.slice(0, 2000) }); savedMsg($('.saved', blk)); } catch (er) { savedMsg($('.saved', blk), er); }
        paintWeapons(false);
      };
      box.onchange = async e => { if (e.target.tagName !== 'TEXTAREA') return; const blk = e.target.closest('[data-cls]'), prev = DB.get('cwpick', blk.dataset.cls) || {};
        try { await DB.set('cwpick', blk.dataset.cls, { model: prev.model || '', note: e.target.value.slice(0, 2000) }); savedMsg($('.saved', blk)); } catch (er) { savedMsg($('.saved', blk), er); } };
    }
    $$('.classblock', box).forEach(blk => { const p = DB.get('cwpick', blk.dataset.cls) || {};
      $$('.wcard', blk).forEach(a => { const on = p.model === a.dataset.key; a.dataset.picked = String(on); const b = $('button[data-pick]', a); b.setAttribute('aria-pressed', String(on)); b.textContent = on ? 'Your pick' : 'Pick this model'; });
      const ta = $('textarea', blk); if (document.activeElement !== ta) ta.value = p.note || ''; });
  } else if (wTab === 'tablet') {
    if (full) {
      box.innerHTML = `<p class="hint">Your Nov 26 decision: the Tablet's lineup is redone on our own models, each with a BO3-style ability. Mark each of today's weapons Keep, Cut or Rework; then I'll propose new ones here for you to approve one by one.</p>
<div class="wgrid">${D.tablet.map(w => `<article class="wcard" data-tb="${w.id}">${w.key ? '<canvas aria-label="' + esc(w.name) + ', turnable 3D model"></canvas>' : '<div class="noview">No preview yet</div>'}<div class="wb"><h4>${esc(w.name)}</h4><span class="lbl">${esc(w.kind)}</span><p>${esc(w.does)}</p>
<div class="row"><button class="btn pass" data-tc="keep" aria-pressed="false">Keep</button><button class="btn skip" data-tc="rework" aria-pressed="false">Rework</button><button class="btn fail" data-tc="cut" aria-pressed="false">Cut</button></div>
<textarea rows="1" placeholder="Why, or what to change (optional)" aria-label="Note on ${esc(w.name)}"></textarea><span class="saved"></span></div></article>`).join('')}</div>
<h3 class="sub">Ideas for new Tablet weapons</h3><div data-tb="_ideas"><textarea rows="3" placeholder="Any weapon you'd like to see in the Tablet: what it is and what it does" aria-label="Ideas for new Tablet weapons"></textarea><span class="saved"></span></div>
<div class="status-line" id="tbsum"></div>`;
      $$('.wcard[data-tb]', box).forEach(a => { const w = D.tablet.find(x => x.id === a.dataset.tb); if (w.key) view3d($('canvas', a), w.key, false); });
      box.onclick = async e => { const b = e.target.closest('button[data-tc]'); if (!b) return; const card = b.closest('[data-tb]'), id = card.dataset.tb, prev = DB.get('tablet', id) || {};
        try { await DB.set('tablet', id, { choice: prev.choice === b.dataset.tc ? '' : b.dataset.tc, note: $('textarea', card).value.slice(0, 2000) }); savedMsg($('.saved', card)); } catch (er) { savedMsg($('.saved', card), er); } paintWeapons(false); };
      box.onchange = async e => { if (e.target.tagName !== 'TEXTAREA') return; const card = e.target.closest('[data-tb]'), prev = DB.get('tablet', card.dataset.tb) || {};
        try { await DB.set('tablet', card.dataset.tb, { choice: prev.choice || '', note: e.target.value.slice(0, 2000) }); savedMsg($('.saved', card)); } catch (er) { savedMsg($('.saved', card), er); } };
    }
    let k = 0, cu = 0, rw = 0;
    $$('[data-tb]', box).forEach(card => { const p = DB.get('tablet', card.dataset.tb) || {};
      $$('button[data-tc]', card).forEach(b => b.setAttribute('aria-pressed', String(p.choice === b.dataset.tc)));
      if (p.choice === 'keep') k++; if (p.choice === 'cut') cu++; if (p.choice === 'rework') rw++;
      const ta = $('textarea', card); if (document.activeElement !== ta) ta.value = p.note || ''; });
    $('#tbsum').textContent = `${D.tablet.length} weapons: ${k} keep, ${rw} rework, ${cu} cut, ${D.tablet.length - k - rw - cu} not marked yet.`;
  } else if (wTab === 'elements') {
    box.innerHTML = `<p class="hint">Why the effects felt like a mess: each was picked part by part, so a purple bolt could burn red and leave purple smoke. From now on every effect is built from one kit per element, made of Skyrim's own 3D effects, so an element always looks, moves and sounds the same. Each kit is shown here with live previews as it's built, one element at a time, and tested in the Test Range.</p>
<div class="egrid">${D.elements.map(x => `<div class="ecard" style="--c:${x.col}"><h4>${esc(x.name)}</h4><p>${esc(x.about)}</p><span class="pill st-idea">Next</span></div>`).join('')}</div>
<h3 class="sub">What every kit has</h3><div class="kit">${D.kit.map(k => `<div><b>${esc(k.name)}</b>${esc(k.about)}</div>`).join('')}</div>`;
  } else {
    box.innerHTML = `<p class="hint">The Test Range (coming with the element kits): a place in the game where each new effect plays on dummies, one labelled step at a time, so you can record it with NVIDIA. Your clips come from your Videos folder into this page, next to the feature they show.</p>
${(D.clips || []).length ? `<div class="wgrid">${D.clips.map(c => `<div class="clip"><video src="${esc(c.file)}" controls preload="metadata" playsinline></video><div class="wb"><b>${esc(c.title)}</b><p class="muted">${esc(c.when)}</p></div></div>`).join('')}</div>` : '<p class="empty">No clips yet: the first come with the Test Range.</p>'}`;
  }
}

// ================================================================ IN-GAME UI
let uTab = 'direction', uFilter = 'all';
const DIRS = [
  { id: 'tally', name: 'Ember Tally', tag: 'BO3\'s layout and punch, in Skyrim\'s fire',
    pts: ['The night as glowing ember tally marks (BO3\'s chalk round counter, scratched like runes).', 'Cursed Essence big and gold bottom right, with +10s that pop up like BO3\'s points.', 'Your brews as a row of round glowing icons bottom left, like perk-a-colas.', 'Skyrim\'s thin compass and quiet bars; everything else hidden until it matters.'],
    html: () => `<div class="hud dA"><div class="sky"></div><div class="figs"><span></span><span></span><span></span><span></span><span></span><span></span></div><div class="weap"></div>
<div class="tally"><small>NIGHT</small>${tallySvg(12)}<span class="w">WAVE 3 &middot; 17 LEFT</span></div>
<div class="q"><b>LAY THE RESTLESS TO REST</b><span>Slay the risen in the altar's light &middot; 7 / 15</span><i></i></div>
<div class="comp"><span>W</span><em>&#9670;</em><span>N</span><em>&#9679;</em><span>E</span></div>
<div class="brews">${['#c0392b', '#3f78c9', '#6f9e4a', '#d6a63a'].map((c, i) => `<span style="--bc:${c}">${['&#x16A0;', '&#x16B1;', '&#x16C9;', '&#x16DF;'][i]}</span>`).join('')}</div>
<div class="bars"><i style="--v:72%;--c:#c0392b"></i><i style="--v:50%;--c:#3f78c9"></i><i style="--v:86%;--c:#6f9e4a"></i></div>
<div class="pts"><span class="pop">+50</span><small>CURSED ESSENCE</small><b>12,480</b></div></div>` },
  { id: 'stone', name: 'Dragon Stone', tag: 'Skyrim\'s carved stone and gold, BO3\'s places on the screen',
    pts: ['Every readout on a small carved plate edged in gold, like Skyrim\'s Dragon-script stones.', 'The night in large Cinzel numerals; the essence the same, bottom right.', 'Brews as gold-framed diamonds; bars framed like Skyrim\'s.', 'The richest look of the three; also the busiest.'],
    html: () => `<div class="hud dB"><div class="sky"></div><div class="figs"><span></span><span></span><span></span><span></span><span></span><span></span></div><div class="weap"></div>
<div class="plate night"><small>NIGHT</small><b>XII</b><span>WAVE 3 &middot; 17 LEFT</span></div>
<div class="plate q"><b>Lay the Restless to Rest</b>Slay the risen in the altar's light &middot; 7 / 15</div>
<div class="comp"><span>W</span><em>&#9670;</em><span>N</span><em>&#9679;</em><span>E</span></div>
<div class="brews">${['#e05a3a', '#78a9e0', '#7cbf62', '#d6b26a'].map((c, i) => `<span><i style="--bc:${c}">${['&#x16A0;', '&#x16B1;', '&#x16C9;', '&#x16DF;'][i]}</i></span>`).join('')}</div>
<div class="bars"><i style="--v:72%;--c:#b8392f"></i><i style="--v:50%;--c:#3f78c9"></i><i style="--v:86%;--c:#6f9e4a"></i></div>
<div class="plate pts"><small>CURSED ESSENCE</small><b>12,480</b></div></div>` },
  { id: 'edge', name: 'Edge Clean', tag: 'Today\'s quiet Edge UI, with BO3\'s colour only where it counts',
    pts: ['Close to what is in the game now: thin type, no frames, lots of space.', 'BO3 shows only in the colours: red for the night, gold for essence pops.', 'The calmest of the three; the least like Black Ops.'],
    html: () => `<div class="hud dC"><div class="sky"></div><div class="figs"><span></span><span></span><span></span><span></span><span></span><span></span></div><div class="weap"></div>
<div class="night"><small>N I G H T</small><b>12</b><span>Wave 3 / 5 &middot; 17 left</span></div>
<div class="q"><b>LAY THE RESTLESS TO REST</b><span>Slay the risen in the altar's light  7 / 15</span></div>
<div class="comp"><span>W</span><em>&#9670;</em><span>N</span><em>&#9679;</em><span>E</span></div>
<div class="pts"><small>CURSED ESSENCE</small><b>12,480</b></div>
<div class="brews">${['&#x16A0;', '&#x16B1;', '&#x16C9;', '&#x16DF;'].map(r => `<span>${r}</span>`).join('')}</div>
<div class="bars"><i style="--v:72%;--c:#b8392f"></i><i style="--v:50%;--c:#3f78c9"></i><i style="--v:86%;--c:#6f9e4a"></i></div></div>` },
];
function tallySvg(n) {   // BO3's round tally, five strokes to a group, drawn as scratched ember lines
  let s = '', x = 6; const rnd = i => ((Math.sin(i * 12.9898) * 43758.5453) % 1 + 1) % 1;
  for (let i = 0; i < n; i++) {
    const g = i % 5;
    if (g === 4) { const x0 = x - 4 * 13 - 4; s += `<path d="M${x0} ${46 - rnd(i) * 4} L${x - 6} ${16 + rnd(i + 3) * 4}" />`; x += 14; continue; }
    s += `<path d="M${x + rnd(i) * 3} ${8 + rnd(i + 1) * 4} L${x + 2 - rnd(i + 2) * 3} ${54 - rnd(i + 5) * 4}" />`; x += 13;
  }
  return `<svg viewBox="0 0 ${x + 6} 60" preserveAspectRatio="xMinYMid meet"><g stroke="#ff7a3c" stroke-width="5" stroke-linecap="round" fill="none">${s}</g><g stroke="#ffd2a8" stroke-width="1.6" stroke-linecap="round" fill="none" opacity=".8">${s}</g></svg>`;
}
const UI_STATE_T = { in: 'In the game now', kept: 'Kept as it is (your note)', next: 'Approved: to be built', removed: 'Removed (your note)' };
function drawUI() {
  const el = $('section[data-s="ui"]');
  el.innerHTML = `<h2 class="sec">In-game UI</h2>
<p class="lead">Every screen of the mod, aiming for the perfect blend of Skyrim and Black Ops 3 Zombies. First pick the direction the whole UI follows; then every screen is redrawn in it here for your Approve or Change before it goes in the game.</p>
<div class="chips" id="utabs">${[['direction', 'The direction'], ['screens', 'Every screen']].map(([k, n]) => `<button class="chip" data-u="${k}" aria-pressed="${k === uTab}">${n}</button>`).join('')}</div>
<div id="ubody"></div>`;
  $('#utabs').addEventListener('click', e => { const b = e.target.closest('.chip'); if (!b) return; uTab = b.dataset.u; $$('#utabs .chip').forEach(c => c.setAttribute('aria-pressed', String(c === b))); paintUI(true); });
  paintUI(true);
}
function paintUI(full) {
  const box = $('#ubody');
  if (uTab === 'direction') {
    if (full) {
      box.innerHTML = `<p class="hint">Three directions for the whole in-game UI, each drawn as the same moment of a night (night 12, a trial running, 12,480 essence). Pick one, or pick one and say what to take from another.</p>
<div class="dgrid">${DIRS.map(d => `<article class="dcard" data-dir="${d.id}">${d.html()}<div class="wb"><h4>${esc(d.name)}</h4><span class="lbl">${esc(d.tag)}</span><ul>${d.pts.map(p => `<li>${esc(p)}</li>`).join('')}</ul>
<div class="row"><button class="btn pass big" data-dpick="${d.id}" aria-pressed="false">Pick this direction</button></div></div></article>`).join('')}</div>
<div style="margin-top:14px"><textarea rows="2" id="dnote" placeholder="What to take from the others, or anything to change (optional)" aria-label="Note on the UI direction"></textarea><span class="saved" id="dsaved"></span></div>`;
      box.onclick = async e => { const b = e.target.closest('button[data-dpick]'); if (!b) return; const prev = DB.get('uidir', 'pick') || {};
        try { await DB.set('uidir', 'pick', { pick: prev.pick === b.dataset.dpick ? '' : b.dataset.dpick, note: $('#dnote').value.slice(0, 2000) }); savedMsg($('#dsaved')); } catch (er) { savedMsg($('#dsaved'), er); } paintUI(false); };
      box.onchange = async e => { if (e.target.id !== 'dnote') return; const prev = DB.get('uidir', 'pick') || {};
        try { await DB.set('uidir', 'pick', { pick: prev.pick || '', note: e.target.value.slice(0, 2000) }); savedMsg($('#dsaved')); } catch (er) { savedMsg($('#dsaved'), er); } };
    }
    const p = DB.get('uidir', 'pick') || {};
    $$('.dcard', box).forEach(c => { const on = p.pick === c.dataset.dir; c.dataset.picked = String(on); const b = $('button', c); b.setAttribute('aria-pressed', String(on)); b.textContent = on ? 'Your pick' : 'Pick this direction'; });
    const ta = $('#dnote'); if (ta && document.activeElement !== ta) ta.value = p.note || '';
  } else {
    const all = UI_SCREENS.flatMap(([g, gl, l]) => l.map(x => ({ g, gl, id: x[0], name: x[1], d: x[2], draw: x[3] })));
    if (full) {
      box.innerHTML = `<p class="hint">Today's screens, drawn as they are or as planned. Once you pick a direction, each is redrawn in it here; Approve what's right, Change with a note what isn't.</p>
<div class="chips" id="ufil">${[['all', `Everything (${all.length})`]].concat(UI_SCREENS.map(([g, gl, l]) => [g, `${gl} (${l.length})`])).map(([g, n]) => `<button class="chip" data-g="${g}" aria-pressed="${g === uFilter}">${esc(n)}</button>`).join('')}</div>
<div class="ugrid">${all.map(x => `<article class="ucard" data-id="${x.id}" data-g="${x.g}">${x.draw()}<div class="wb"><h4>${esc(x.name)}</h4>${UI_STATE[x.id] ? `<span class="lbl">${UI_STATE_T[UI_STATE[x.id]]}</span>` : ''}<p>${esc(x.d)}</p>
<div class="row"><button class="btn pass" data-c="ok" aria-pressed="false">Approve</button><button class="btn skip" data-c="change" aria-pressed="false">Change</button><span class="saved"></span></div>
<textarea rows="1" placeholder="What to change (optional)" aria-label="Note on ${esc(x.name)}"></textarea></div></article>`).join('')}</div><div class="status-line" id="usum"></div>`;
      box.onclick = async e => {
        const f = e.target.closest('#ufil .chip'); if (f) { uFilter = f.dataset.g; $$('#ufil .chip').forEach(c => c.setAttribute('aria-pressed', String(c === f))); paintUI(false); return; }
        const b = e.target.closest('button[data-c]'); if (!b) return; const card = b.closest('.ucard'), prev = DB.get('uiscreens', card.dataset.id) || {};
        try { await DB.set('uiscreens', card.dataset.id, { choice: prev.choice === b.dataset.c ? '' : b.dataset.c, note: $('textarea', card).value.slice(0, 2000) }); savedMsg($('.saved', card)); } catch (er) { savedMsg($('.saved', card), er); } paintUI(false); };
      box.onchange = async e => { if (e.target.tagName !== 'TEXTAREA') return; const card = e.target.closest('.ucard'), prev = DB.get('uiscreens', card.dataset.id) || {};
        try { await DB.set('uiscreens', card.dataset.id, { choice: prev.choice || '', note: e.target.value.slice(0, 2000) }); savedMsg($('.saved', card)); } catch (er) { savedMsg($('.saved', card), er); } };
    }
    let ok = 0, ch = 0;
    $$('.ucard', box).forEach(card => { const p = DB.get('uiscreens', card.dataset.id) || {};
      card.hidden = uFilter !== 'all' && card.dataset.g !== uFilter;
      $$('button[data-c]', card).forEach(b => b.setAttribute('aria-pressed', String(p.choice === b.dataset.c)));
      if (p.choice === 'ok') ok++; if (p.choice === 'change') ch++;
      const ta = $('textarea', card); if (document.activeElement !== ta) ta.value = p.note || ''; });
    $('#usum').textContent = `${all.length} screens: ${ok} approved, ${ch} to change.`;
  }
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
<div class="hist">${D.builds.map(b => `<div class="hrow${D.installs[b.sha] ? ' inst' : ''}"><div><code>${esc(b.sha)}</code><div class="when">${esc(b.when)}</div>${D.installs[b.sha] ? '<span class="pill st-ready">Installed</span>' : ''}</div><p>${esc(b.what)}</p></div>`).join('')}</div>`;
  paintHistory();
}
function paintHistory() {
  if (!drawn.history) return;
  const marks = Object.values(DB.all('marks')).filter(m => m.build === BUILD && m.r);
  $('#hstats').innerHTML = `<div class="stat"><b>${D.builds.length}</b><span>Builds</span></div><div class="stat"><b>${Object.keys(D.installs).length}</b><span>Installed</span></div><div class="stat green"><b>${marks.filter(m => m.r === 'pass').length}</b><span>Passed on ${esc(BUILD)}</span></div><div class="stat red"><b>${marks.filter(m => m.r === 'fail').length}</b><span>Failed on ${esc(BUILD)}</span></div>`;
}

const DRAW = { now: drawNow, systems: drawSystems, weapons: drawWeapons, ui: drawUI, release: drawRelease, history: drawHistory };
// (everything marked anywhere repaints what shows it)
DB.watch('marks', () => { paintBadge(); paintNow(); paintSystems(); paintHistory(); });
DB.watch('groups', () => { paintBadge(); paintNow(); paintSystems(); });
DB.watch('decisions', () => paintNow());
DB.watch('cwpick', () => { if (drawn.weapons && wTab === 'classes') paintWeapons(false); });
DB.watch('tablet', () => { if (drawn.weapons && wTab === 'tablet') paintWeapons(false); });
DB.watch('uidir', () => { if (drawn.ui && uTab === 'direction') paintUI(false); });
DB.watch('uiscreens', () => { if (drawn.ui && uTab === 'screens') paintUI(false); });
paintBadge();
route();
