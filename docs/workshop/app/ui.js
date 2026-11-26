const WTHUMB = key => { const i = WTAB.cell[key]; if (i == null) return ''; const c = i % WTAB.cols, r = Math.floor(i / WTAB.cols);
  return `<div class="wthumb" role="img" aria-label="" style="background-image:url(weapons.jpg);background-size:${WTAB.cols * 100}% ${WTAB.rows * 100}%;background-position:${c / (WTAB.cols - 1) * 100}% ${r / (WTAB.rows - 1) * 100}%"></div>`; };
const WKEY_BY_NAME = (() => { const m = {}; WTAB.groups.forEach(g => g.items.forEach(it => { if (!m[it.name]) m[it.name] = it.key; })); return m; })();
// ================================================================ tabs
(function () {
  const nav = $('#tabs'), tabs = [...document.querySelectorAll('section.tab')];
  function show(id) {
    tabs.forEach(s => s.hidden = s.id !== id);
    nav.querySelectorAll('button').forEach(b => b.setAttribute('aria-selected', String(b.dataset.t === id)));
    try { localStorage.setItem('coa-tab', id); } catch (e) {}
    if (location.hash !== '#' + id) history.replaceState(null, '', '#' + id);
    window.dispatchEvent(new Event('tabshown'));
  }
  nav.addEventListener('click', e => { const b = e.target.closest('button[data-t]'); if (b) show(b.dataset.t); });
  let first = (location.hash || '').slice(1);
  if (!tabs.some(s => s.id === first)) { try { first = localStorage.getItem('coa-tab') || ''; } catch (e) {} }
  show(tabs.some(s => s.id === first) ? first : tabs[0].id);
  window.addEventListener('hashchange', () => { const h = location.hash.slice(1); if (tabs.some(s => s.id === h)) show(h); });
})();
// (Noah, 2026-11-25: "a simple card for all the weapon lists, all the abilities, and the scythes. And when I click on a weapon or an
// ability, it should bring up the full UI so that I can see the preview and all of the change options in one screen") every card
// is small in its grid; a click opens it over the page - the 3D preview on one side, everything to change on the other - and its
// scene is only built then
const DETAIL = (function () {
  const back = document.createElement('div'); back.className = 'dback'; back.hidden = true; document.body.appendChild(back);
  let cur = null;
  function open(el, then) {
    if (cur === el) return; if (cur) close();
    if (!el.querySelector(':scope > .dhead')) { const h = document.createElement('div'); h.className = 'dhead'; h.innerHTML = `<b></b><button class="btn sm dclose" type="button">Close ✕</button>`; el.prepend(h); }
    $(':scope > .dhead b', el).textContent = (el.querySelector('h3') || {}).textContent || '';
    el.classList.remove('compact'); el.classList.add('open'); el.setAttribute('role', 'dialog'); el.setAttribute('aria-modal', 'true');
    back.hidden = false; document.documentElement.classList.add('dopen'); cur = el;
    if (el.__init) { const f = el.__init; el.__init = null; f(); }
    if (then) then();
    $('.dclose', el).focus({ preventScroll: true });
  }
  function close() {
    if (!cur) return; const el = cur; cur = null;
    el.classList.remove('open'); el.classList.add('compact'); el.removeAttribute('role'); el.removeAttribute('aria-modal');
    back.hidden = true; document.documentElement.classList.remove('dopen');
    window.dispatchEvent(new CustomEvent('detailclosed', { detail: el }));
    el.focus({ preventScroll: true });
  }
  back.addEventListener('click', close);
  document.addEventListener('keydown', e => { if (e.key === 'Escape' && cur) close(); if ((e.key === 'Enter' || e.key === ' ') && e.target.classList && e.target.classList.contains('compact')) { e.preventDefault(); open(e.target); } });
  document.addEventListener('click', e => {
    if (e.target.closest('.dclose')) { close(); return; }
    const el = e.target.closest('.wcard.compact'); if (!el) return;
    e.preventDefault(); e.stopPropagation();
    const tab = e.target.closest('.abtabs button');
    open(el, tab ? () => tab.click() : null);
  }, true);
  window.addEventListener('tabshown', close);
  return { open, close, cur: () => cur };
})();
function whenNear(el, f) { el.__init = f; el.classList.add('compact'); el.tabIndex = 0; const g = el.closest('.wgrid'); if (g) g.classList.add('cgrid'); }

// ================================================================ what changed
(function () {
  $('#changes').innerHTML = CHANGES.map(([k, t, d, c]) => `<div class="card"><div class="k">${esc(k)} <span class="tag ${c}" style="margin-left:6px">${c === 'ok' ? 'in this build' : c === 'warn' ? 'next build' : c === 'info' ? 'my recommendation' : 'earlier build'}</span></div><h3>${esc(t)}</h3><p class="d">${esc(d)}</p></div>`).join('');
})();

// ================================================================ the weapon workshop
(function () {
  const grid = $('#wgrid'), st = $('#wstatus'), cats = $('#wcats');
  const all = WCATS.flatMap(c => c.items.map(it => ({ ...it, cat: c.id })));
  const LN = (it, k) => ((LOOK3[it.id] || [])[+String(k).slice(1) - 1] || [k])[0];   // (a weapon's own look's name)
  $('#lookkey').innerHTML = '';
  let cur = 'all';
  cats.innerHTML = `<button data-c="all" aria-pressed="true">All<span class="n">${all.length}</span></button>` + WCATS.map(c => `<button data-c="${c.id}" aria-pressed="false">${esc(c.name)}<span class="n">${c.items.length}</span></button>`).join('');
  const cards = {};
  function pickOf(it) { const s = DB.get('weaponlooks', it.id); return { look: (s && s.choice === 'change' && s.look) || it.look, col: (s && s.choice === 'change' && s.colour) || it.col, s }; }
  function build() {
    grid.innerHTML = WCATS.map(c => `<div class="wgroup" data-c="${c.id}" style="grid-column:1/-1;display:grid;gap:2px;margin-top:6px"><h3 style="font-family:var(--display);font-weight:400;font-size:20px;color:var(--accent)">${esc(c.name)}</h3><p class="sub" style="font-size:13px">${esc(c.sub)}</p></div>` +
      c.items.map(it => `<article class="wcard" data-id="${it.id}" data-c="${c.id}">${WTHUMB(it.key)}
<div class="stage"><canvas aria-label="${esc(it.name)}, live"></canvas><div class="ld">${GL.ok ? 'Loading…' : 'Live previews need WebGL'}</div><div class="ov"><span class="ovl"></span><span class="ovr"></span></div></div>
${it.tiers ? `<div class="tiers" role="group" aria-label="Tier">${it.tiers.map(([k, n], i) => `<button data-k="${k}" aria-pressed="${i === it.tiers.length - 1}">${esc(n)}</button>`).join('')}</div>` : ''}
<div class="wbody"><div class="t"><h3>${esc(it.name)}</h3><span class="saved"></span></div>
<div class="now">In this build: <b>${esc(LN(it, it.look))}</b> in <b>${esc(colName(it.col))}</b>${it.where ? ' · ' + esc(it.where) : ''}</div>
${it.note ? `<p class="note">${esc(it.note)}</p>` : ''}
<div class="row"><button class="btn sm" data-a="keep" aria-pressed="false">Keep as it is</button><button class="btn sm" data-a="change" aria-pressed="false">Change</button></div>
<div class="pick" hidden>
<div class="lbl">Its own looks</div><div class="opts">${(LOOK3[it.id] || []).map(([n, d], i) => `<button class="opt" data-look="o${i + 1}" aria-pressed="false"><b>${esc(n)}</b><span>${esc(d)}</span></button>`).join('')}</div>
<div class="lbl">Colour</div><div class="row">${it.col.includes('+') ? `<button class="chip" data-col="${it.col}" aria-pressed="false"><i style="background:linear-gradient(90deg,${it.col.split('+').map(x => COLS[x]).join(',')})"></i>Its two colours</button>` : ''}${Object.entries(COLS).map(([k, h]) => `<button class="chip" data-col="${k}" aria-pressed="false"><i style="background:${h}"></i>${CNAME[k] || k}</button>`).join('')}<label class="chip wheel"><input type="color" value="#ffffff" aria-label="Pick any colour" style="width:22px;height:18px;border:0;padding:0;background:none;cursor:pointer">Any colour</label></div>
</div>
<textarea rows="1" placeholder="A note for me (optional)" aria-label="Note on ${esc(it.name)}"></textarea>
</div></article>`).join('')).join('');
    grid.querySelectorAll('.wcard').forEach(el => {
      const it = all.find(x => x.id === el.dataset.id); const c = { el, it, key: it.key, open: true };
      cards[it.id] = c;
      whenNear(el, () => {
        if (!GL.ok) return;
        c.view = weaponView($('canvas', el), info => {
          const ld = $('.ld', el); if (info.error) { ld.textContent = 'This preview didn\'t load.'; return; }
          ld.textContent = ''; $('.ovr', el).textContent = info.length ? `${info.length.toFixed(2)} m long${SCYTHES.includes(c.key) ? ' (25% larger)' : ''}` : '';
        });
        refresh(c);
      });
    });
    paintAll();
  }
  function refresh(c) {
    const p = pickOf(c.it);
    if (c.view) c.view.show(c.key, p.look, p.col === c.it.col ? 'own' : p.col);
    $('.ovl', c.el).textContent = `${LN(c.it, p.look)} · ${colName(p.col)}`;
  }
  function paint(c) {
    const { s, look, col } = pickOf(c.it), el = c.el, changed = s && s.choice === 'change';
    el.classList.toggle('changed', !!changed);
    $('.saved', el).textContent = s ? (DB.can() ? 'Saved: ' : 'Picked: ') + (s.choice === 'keep' ? 'keep' : `${LN(c.it, look)}, ${colName(col)}`) : '';
    el.querySelector('[data-a="keep"]').setAttribute('aria-pressed', String(!!s && s.choice === 'keep'));
    el.querySelector('[data-a="change"]').setAttribute('aria-pressed', String(!!changed));
    $('.pick', el).hidden = !(changed || c.open);
    el.querySelectorAll('[data-look]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.look === look)));
    el.querySelectorAll('[data-col]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.col === col)));
    const wh = $('.wheel', el); if (wh) { wh.setAttribute('aria-pressed', String(String(col).startsWith('#'))); if (String(col).startsWith('#') && document.activeElement !== $('input', wh)) $('input', wh).value = col; }
    const ta = $('textarea', el); if (document.activeElement !== ta && s && typeof s.note === 'string') ta.value = s.note;
  }
  function paintAll() { Object.values(cards).forEach(paint); }
  async function save(c, patch) {
    const prev = DB.get('weaponlooks', c.it.id) || {};
    const row = { id: c.it.id, name: c.it.name, now: c.it.look + ' ' + c.it.col, choice: prev.choice || 'change', look: prev.look || c.it.look, colour: prev.colour || c.it.col, note: $('textarea', c.el).value.slice(0, 2000), ...patch };
    // (seen at once, saved or not)
    if (c.view) c.view.show(c.key, row.choice === 'change' ? row.look : c.it.look, row.choice === 'change' && row.colour !== c.it.col ? row.colour : 'own');
    $('.ovl', c.el).textContent = row.choice === 'change' ? `${LN(c.it, row.look)} · ${colName(row.colour)}` : `${LN(c.it, c.it.look)} · ${colName(c.it.col)}`;
    try { await DB.set('weaponlooks', c.it.id, row); saveMsg(st, null); count(); } catch (e) { saveMsg(st, e); }
    paint(c);
  }
  grid.addEventListener('click', e => {
    const el = e.target.closest('.wcard'); if (!el) return; const c = cards[el.dataset.id], b = e.target.closest('button'); if (!b || b.disabled) return;
    if (b.dataset.k) { c.key = b.dataset.k; el.querySelectorAll('.tiers button').forEach(x => x.setAttribute('aria-pressed', String(x === b))); $('.ld', el).textContent = 'Loading…'; refresh(c); return; }
    if (b.dataset.a === 'keep') { save(c, { choice: 'keep', look: c.it.look, colour: c.it.col }); }
    else if (b.dataset.a === 'change') { c.open = true; paint(c); }
    else if (b.dataset.look) save(c, { choice: 'change', look: b.dataset.look });
    else if (b.dataset.col) save(c, { choice: 'change', colour: b.dataset.col });
  });
  grid.addEventListener('input', e => { if (e.target.type !== 'color') return; const c = cards[e.target.closest('.wcard').dataset.id], p = pickOf(c.it);
    if (c.view) c.view.show(c.key, p.look, e.target.value); $('.ovl', c.el).textContent = `${LN(c.it, p.look)} · ${e.target.value}`; });
  grid.addEventListener('change', e => { const c = cards[e.target.closest('.wcard').dataset.id];
    if (e.target.type === 'color') save(c, { choice: 'change', colour: e.target.value });
    else if (e.target.tagName === 'TEXTAREA') save(c, {}); });
  cats.addEventListener('click', e => { const b = e.target.closest('button'); if (!b) return; cur = b.dataset.c; cats.querySelectorAll('button').forEach(x => x.setAttribute('aria-pressed', String(x === b)));
    grid.querySelectorAll('[data-c]').forEach(x => x.hidden = cur !== 'all' && x.dataset.c !== cur); });
  function count() { const n = Object.keys(DB.all('weaponlooks')).length, ch = Object.values(DB.all('weaponlooks')).filter(x => x.choice === 'change').length; st.textContent = n ? `${n} of ${all.length} set (${ch} changed). Saved here for me to build from.` : `None of the ${all.length} set yet. Each one shows your pick the moment you choose it.`; }
  build();
  DB.on('weaponlooks', (d, err) => { if (err) { st.textContent = err === 'offline' ? 'You can try every look here; saving works when this page is opened in Claude.' : 'Your saved picks could not be loaded.'; return; }
    count(); paintAll(); Object.values(cards).forEach(c => { if (c.view) refresh(c); }); });
})();

// ================================================================ the ability workshop
(function () {
  const grid = $('#agrid'), st = $('#astatus'), cats = $('#acats');
  $('#styles').innerHTML = Object.entries(STYLES).map(([k, [n, d]]) => `<div class="look"><b>${n}</b><span>${esc(d)}</span></div>`).join('');
  const items = []; ABIL.forEach(([cat, cid, list]) => list.forEach(([id, name, abs]) => items.push({ cat, cid, id, name, abs: abs.map(([aid, trig, does, now, src, style, col, size, where]) => ({ key: id + '_' + aid, trig, does, now, src, style, col, size, where })) })));
  const nAb = items.reduce((s, x) => s + x.abs.length, 0);
  let cur = 'all';
  cats.innerHTML = `<button data-c="all" aria-pressed="true">All<span class="n">${items.length}</span></button>` + ABIL.map(([n, id, l]) => `<button data-c="${id}" aria-pressed="false">${esc(n)}<span class="n">${l.length}</span></button>`).join('');
  const cards = {};
  // (Noah, 2026-11-25: five options of its own per ability, any colour, and sliders to tailor it - tools/abilityopts.py)
  const SL = [['size', 'Size', 40, 220], ['bright', 'Brightness', 30, 250], ['area', 'Area of effect', 50, 200], ['vib', 'Vibrance', 0, 200], ['speed', 'Speed', 40, 200], ['linger', 'Linger', 50, 300], ['density', 'Density', 25, 300], ['light', 'Light cast', 0, 300]];
  const SLHELP = { size: 'how big the effect is', bright: 'how bright it burns', area: 'how wide it spreads - and in game, how far it reaches the dead', vib: 'how strong its colour is', speed: 'how fast it plays out', linger: 'how long it hangs after', density: 'how many particles', light: 'how much it lights the ground and the dead' };
  const pickOf = a => { const s = DB.get('abilities', a.key); const ch = s && s.choice === 'change'; const pk = AB_PICKS[a.key] || {}; const defCol = pk.col || a.col;
    const r = { s, style: pk.style || a.style, opt: (ch && s.opt) || 'a1', col: (ch && s.colour) || defCol, defCol };
    for (const [k] of SL) r[k] = ch && s[k] != null ? +s[k] : k === 'size' ? (pk.size || a.size) : 100;
    return r; };
  const optsOf = a => ((REALFX.opts || {})[a.key] || { opts: [] }).opts;
  // (the sound workshop plays each ability as you've set it here)
  window.abArgs = key => { const a = items.flatMap(x => x.abs).find(x => x.key === key); if (!a) return null; const p = pickOf(a); return [p.style, p.col, p.size, a.where, optsOf(a).length ? { pick: p } : null]; };
  grid.innerHTML = ABIL.map(([cat, cid, list]) => `<div data-c="${cid}" style="grid-column:1/-1;display:grid;gap:2px;margin-top:6px"><h3 style="font-family:var(--display);font-weight:400;font-size:20px;color:var(--accent)">${esc(cat)}</h3></div>` +
    list.map(([id, name, abs]) => `<article class="wcard" data-id="${id}" data-c="${cid}">${WTHUMB(WKEY_BY_NAME[name] || '')}
<div class="abtabs" role="tablist" aria-label="${esc(name)}'s abilities">${abs.map((a, i) => `<button role="tab" data-i="${i}" aria-selected="${i === 0}">${esc(a[1])}</button>`).join('')}</div>
<div class="stage" style="margin-top:8px"><canvas aria-label="${esc(name)}'s ability, live"></canvas><div class="ld">${GL.ok ? '' : 'Live previews need WebGL'}</div><div class="ov"><span class="ovl"></span><span class="ovr">loops</span></div></div>
<div class="wbody"><div class="t"><h3>${esc(name)}</h3><span class="saved"></span></div>
<p class="now ab-does"></p><div class="now ab-now"></div>
<div class="row"><button class="btn sm" data-a="keep" aria-pressed="false">Looks right</button><button class="btn sm" data-a="change" aria-pressed="false">Change</button></div>
<div class="pick" hidden>
<div class="lbl">Its own options</div><div class="opts aopts"></div>
<div class="lbl">Colour</div><div class="row">${Object.entries(COLS).map(([k, h]) => `<button class="chip" data-col="${k}" aria-pressed="false"><i style="background:${h}"></i>${CNAME[k] || k}</button>`).join('')}<label class="chip wheel"><input type="color" value="#ffffff" aria-label="Pick any colour" style="width:22px;height:18px;border:0;padding:0;background:none;cursor:pointer">Any colour</label></div>
<div class="sliders">${SL.map(([k, n, lo, hi]) => `<label class="sl" title="${SLHELP[k]}"><span>${n}</span><input type="range" data-k="${k}" min="${lo}" max="${hi}" step="5" aria-label="${n}"><b class="v"></b></label>`).join('')}</div>
</div>
<textarea rows="1" placeholder="A note for me about this ability (optional)" aria-label="Note"></textarea>
</div></article>`).join('')).join('');
  grid.querySelectorAll('.wcard').forEach(el => {
    const it = items.find(x => x.id === el.dataset.id); const c = { el, it, i: 0, open: true }; cards[it.id] = c;
    whenNear(el, () => { if (GL.ok) { c.view = abilityView($('canvas', el)); play(c); } });
    paint(c);
  });
  function isSpell(a) { return /^(d|t)(fire|frost|shock|sun|arcane|walls|storms)_/.test(a.key); }
  function play(c, over) { const a = c.it.abs[c.i], p = Object.assign(pickOf(a), over || {}); const sp = isSpell(a); $('.stage', c.el).hidden = sp; if (sp) return;
    const os = optsOf(a), o = os.find(x => x.id === p.opt) || os[0];
    if (c.view) { c.view.key = a.key; c.view.play(p.style, p.col, p.size, a.where, os.length ? { pick: p } : null); }
    $('.ovl', c.el).textContent = (o ? o.name : a.trig) + ` · ${colName(p.col)} · ${p.size}%`; }
  function partsText(a, col) {
    const os = optsOf(a), p = pickOf(a), o = os.find(x => x.id === p.opt);
    if (o && !isSpell(a) && p.opt !== 'a1') { const L = [...new Set(o.parts.map(x => x.label))]; return `<b>${esc(o.name)}</b>: ${esc(o.desc)}<ul class="parts">${L.map(x => `<li>${esc(x)}</li>`).join('')}${o.link ? '<li>A beam between the dead: Skyrim\'s Staff of Magnus beam, in its colour</li>' : ''}</ul><span class="pnote">Real parts, as the game would play them.</span>`; }
    const P = AB_PARTS[a.key];
    if (isSpell(a)) return '<b>The spell\'s own effect.</b> In game it plays the impact its own mod made for it (Apocalypse\'s or Skyrim\'s), with nothing of ours over it, as you asked.';
    if (!P) return `Uses: ${esc(a.now)} ${srcTags(a.src)}`;
    if ((REALFX.shows || {})[a.key]) return `<b>Real parts, on the beats the game plays them:</b><ul class="parts">${P.parts.map(x => `<li>${esc(x)}</li>`).join('')}</ul>${P.note ? `<span class="pnote">${esc(P.note)}</span>` : ''}<span class="pnote">The preview above plays them as the game does; your colour and size picks below don't change it.</span>`;
    const own = P.many ? ' Each element its own (fire, frost, shock, sun, arcane): the parts below are the first.' : '';
    return `<b>Rebuilt from real parts</b> (what the game plays), in ${esc(colName(col))}:<ul class="parts">${P.parts.map(x => `<li>${esc(x)}</li>`).join('')}</ul>${P.note ? `<span class="pnote">${esc(P.note)}</span>` : ''}${own}<span class="pnote">The preview above is a close picture of it; the game plays the real effects.</span>`;
  }
  function paint(c) {
    const a = c.it.abs[c.i], { s, style, col, size } = pickOf(a), el = c.el, changed = s && s.choice === 'change';
    el.querySelectorAll('.abtabs button').forEach((b, i) => { b.setAttribute('aria-selected', String(i === c.i)); const sv = DB.get('abilities', c.it.abs[i].key); b.innerHTML = esc(c.it.abs[i].trig) + (sv ? '<span class="dot" aria-label="saved"></span>' : ''); });
    $('.ab-does', el).innerHTML = `<b>${esc(a.trig)}:</b> ${esc(a.does)}`;
    $('.ab-now', el).innerHTML = partsText(a, col);
    const sp = isSpell(a); el.querySelector('.wbody > .row').hidden = sp; if (sp) $('.pick', el).hidden = true;
    const pk = pickOf(a), os = optsOf(a), o = os.find(x => x.id === pk.opt);
    $('.saved', el).textContent = s ? (DB.can() ? 'Saved: ' : 'Picked: ') + (s.choice === 'keep' ? 'looks right' : `${o ? o.name : ''}, ${colName(col)}, ${size}%`) : '';
    $('.aopts', el).innerHTML = os.map(x => `<button class="opt" data-opt="${x.id}" aria-pressed="${x.id === pk.opt}"><b>${esc(x.name)}</b><span>${esc(x.desc)}</span></button>`).join('');
    el.querySelectorAll('.sl input').forEach(r => { const k = r.dataset.k; if (document.activeElement !== r) r.value = pk[k]; r.nextElementSibling.textContent = pk[k] + '%'; });
    el.classList.toggle('changed', !!changed);
    el.querySelector('[data-a="keep"]').setAttribute('aria-pressed', String(!!s && s.choice === 'keep'));
    el.querySelector('[data-a="change"]').setAttribute('aria-pressed', String(!!changed));
    $('.pick', el).hidden = sp || !(changed || c.open);
    el.querySelectorAll('[data-col]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.col === col)));
    const wh = $('.wheel', el); if (wh) { wh.setAttribute('aria-pressed', String(String(col).startsWith('#'))); if (String(col).startsWith('#')) $('input', wh).value = col; }
    const ta = $('textarea', el); if (document.activeElement !== ta) ta.value = s && typeof s.note === 'string' ? s.note : '';
  }
  async function save(c, patch) {
    const a = c.it.abs[c.i], prev = DB.get('abilities', a.key) || {};
    const pk = pickOf(a), row = { id: a.key, item: c.it.name, trigger: a.trig, choice: prev.choice || 'change', opt: pk.opt, optName: ((optsOf(a).find(x => x.id === (patch.opt || pk.opt)) || {}).name) || '', colour: pk.col, note: $('textarea', c.el).value.slice(0, 2000) };
    for (const [k] of SL) row[k] = pk[k];
    Object.assign(row, patch); if (patch.opt) row.optName = (optsOf(a).find(x => x.id === patch.opt) || {}).name || '';
    try { await DB.set('abilities', a.key, row); saveMsg(st, null); count(); } catch (e) { saveMsg(st, e); }
    paint(c); play(c);
  }
  grid.addEventListener('click', e => {
    const el = e.target.closest('.wcard'); if (!el) return; const c = cards[el.dataset.id], b = e.target.closest('button'); if (!b || b.disabled) return;
    if (b.dataset.i != null) { c.i = +b.dataset.i; paint(c); play(c); return; }
    const a = c.it.abs[c.i];
    if (b.dataset.a === 'keep') { const d = { choice: 'keep', opt: 'a1', colour: pickOf(a).defCol }; for (const [k] of SL) d[k] = k === 'size' ? ((AB_PICKS[a.key] || {}).size || a.size) : 100; save(c, d); }
    else if (b.dataset.a === 'change') { c.open = true; paint(c); }
    else if (b.dataset.opt) save(c, { choice: 'change', opt: b.dataset.opt });
    else if (b.dataset.col) save(c, { choice: 'change', colour: b.dataset.col });
  });
  grid.addEventListener('input', e => { const card = e.target.closest('.wcard'); if (!card || !card.querySelector('.aopts')) return; const c = cards[card.dataset.id]; if (!c) return;
    if (e.target.type === 'color') { play(c, { col: e.target.value }); return; }
    if (e.target.type === 'range' && e.target.dataset.k) { e.target.nextElementSibling.textContent = e.target.value + '%'; play(c, { [e.target.dataset.k]: +e.target.value }); } });
  grid.addEventListener('change', e => {
    const card = e.target.closest('.wcard'); if (!card) return; const c = cards[card.dataset.id];
    if (e.target.type === 'range' && e.target.dataset.k) save(c, { choice: 'change', [e.target.dataset.k]: +e.target.value });
    else if (e.target.type === 'color') save(c, { choice: 'change', colour: e.target.value });
    else if (e.target.tagName === 'TEXTAREA') save(c, {});
  });
  cats.addEventListener('click', e => { const b = e.target.closest('button'); if (!b) return; cur = b.dataset.c; cats.querySelectorAll('button').forEach(x => x.setAttribute('aria-pressed', String(x === b)));
    grid.querySelectorAll('[data-c]').forEach(x => x.hidden = cur !== 'all' && x.dataset.c !== cur); });
  function count() { const d = Object.values(DB.all('abilities')); const ch = d.filter(x => x.choice === 'change').length; st.textContent = d.length ? `${d.length} of ${nAb} abilities set (${ch} changed). Saved here for me to build from.` : `None of the ${nAb} abilities set yet.`; }
  DB.on('abilities', (d, err) => { if (err) { st.textContent = err === 'offline' ? 'You can try every style here; saving works when this page is opened in Claude.' : 'Your saved picks could not be loaded.'; return; }
    count(); Object.values(cards).forEach(c => { paint(c); if (c.view) play(c); }); });
})();

// ================================================================ the test list
(function () {
  const box = $('#tlist'), st = $('#tstatus'), prog = $('#tprog');
  const flat = TESTS.flatMap(([g, list]) => list.map(t => ({ g, ...t })));
  box.innerHTML = TESTS.map(([g, list]) => `<h3 class="tgroup">${esc(g)}</h3>` + list.map(t => `<div class="test" data-id="${t.id}">
<div><h3>${esc(t.t)}</h3><p class="how">${esc(t.how)}</p><p class="look4"><b>Look for:</b> ${esc(t.look)}</p></div>
<div class="row"><button class="btn sm good" data-r="pass" aria-pressed="false">Works</button><button class="btn sm bad" data-r="fail" aria-pressed="false">Problem</button><button class="btn sm" data-r="skip" aria-pressed="false">Not yet</button><span class="saved"></span></div>
<textarea rows="1" placeholder="What you saw (optional)" aria-label="Notes on ${esc(t.t)}"></textarea></div>`).join('')).join('');
  function paint() {
    const d = DB.all('tests');
    box.querySelectorAll('.test').forEach(el => { const s = d[el.dataset.id];
      el.classList.toggle('pass', !!s && s.result === 'pass'); el.classList.toggle('fail', !!s && s.result === 'fail');
      el.querySelectorAll('[data-r]').forEach(b => b.setAttribute('aria-pressed', String(!!s && s.result === b.dataset.r)));
      $('.saved', el).textContent = s ? 'Saved' : '';
      const ta = $('textarea', el); if (document.activeElement !== ta && s && typeof s.note === 'string') ta.value = s.note; });
    const v = Object.values(d); prog.innerHTML = `<span><b>${v.filter(x => x.result === 'pass').length}</b> work</span><span><b>${v.filter(x => x.result === 'fail').length}</b> problems</span><span><b>${flat.length - v.filter(x => x.result === 'pass' || x.result === 'fail').length}</b> still to test</span>`;
  }
  async function save(id, result) {
    const el = box.querySelector(`.test[data-id="${id}"]`), t = flat.find(x => x.id === id), prev = DB.get('tests', id) || {};
    try { await DB.set('tests', id, { id, test: t.t, result: result || prev.result || '', note: $('textarea', el).value.slice(0, 2000) }); saveMsg(st, null); } catch (e) { saveMsg(st, e); }
    paint();
  }
  box.addEventListener('click', e => { const b = e.target.closest('button[data-r]'); if (b) save(b.closest('.test').dataset.id, b.dataset.r); });
  box.addEventListener('change', e => { if (e.target.tagName === 'TEXTAREA') save(e.target.closest('.test').dataset.id, null); });
  paint();
  DB.on('tests', (d, err) => { if (err) { st.textContent = err === 'offline' ? 'Marking tests works when this page is opened in Claude.' : 'Your test results could not be loaded.'; return; } st.textContent = ''; paint(); });
})();

// ================================================================ decisions
(function () {
  const box = $('#recs'), st = $('#decstatus');
  box.innerHTML = RECS.map(r => `<div class="rec" data-id="${r.id}"><div class="h"><h3>${esc(r.t)}</h3><span class="saved"></span></div><p>${esc(r.d)}</p>
${(r.quotes || []).map(([k, q]) => `<blockquote><b>${esc(k)}.</b> ${esc(q)}</blockquote>`).join('')}
<div class="row">${r.opts.map(o => `<button class="btn" data-c="${esc(o)}" aria-pressed="false">${esc(o)}</button>`).join('')}</div>
<textarea rows="1" placeholder="A note for me (optional)" aria-label="Note on ${esc(r.t)}"></textarea></div>`).join('');
  function paint() {
    const d = DB.all('decisions');
    box.querySelectorAll('.rec').forEach(el => { const s = d[el.dataset.id];
      el.querySelectorAll('button[data-c]').forEach(b => b.setAttribute('aria-pressed', String(!!s && s.decision === b.dataset.c)));
      $('.saved', el).textContent = s && s.decision ? 'Saved: ' + s.decision : '';
      const ta = $('textarea', el); if (document.activeElement !== ta && s && typeof s.note === 'string') ta.value = s.note; });
    const n = Object.values(d).filter(x => x.decision).length; st.textContent = n ? `${n} of ${RECS.length} decided. Saved here for me to build from.` : 'Nothing decided yet.';
  }
  async function save(id, decision) {
    const el = box.querySelector(`.rec[data-id="${id}"]`), r = RECS.find(x => x.id === id), prev = DB.get('decisions', id) || {};
    try { await DB.set('decisions', id, { id, title: r.t, decision: decision || prev.decision || '', note: $('textarea', el).value.slice(0, 2000) }); } catch (e) { saveMsg(st, e); return; }
    paint();
  }
  box.addEventListener('click', e => { const b = e.target.closest('button[data-c]'); if (b) save(b.closest('.rec').dataset.id, b.dataset.c); });
  box.addEventListener('change', e => { if (e.target.tagName === 'TEXTAREA') save(e.target.closest('.rec').dataset.id, null); });
  paint();
  DB.on('decisions', (d, err) => { if (err) { st.textContent = err === 'offline' ? 'Saving decisions works when this page is opened in Claude.' : 'Your decisions could not be loaded.'; return; } paint(); });
})();

// ================================================================ the last few days, and the mod now
(function () {
  $('#modnowabout').textContent = MODNOW_ABOUT;
  $('#modnow').innerHTML = MODNOW.map(([h, list]) => `<div class="card"><div class="k">${esc(h)}</div><ul class="plain">${list.map(x => `<li>${x}</li>`).join('')}</ul></div>`).join('');
  $('#recent').innerHTML = RECENT.map(([day, list]) => `<div class="day"><h3>${esc(day)}</h3><ul>${list.map(x => `<li>${x}</li>`).join('')}</ul></div>`).join('');
  const counts = { looks: WCATS.reduce((s, c) => s + c.items.length, 0), abil: ABIL.reduce((s, [, , l]) => s + l.length, 0), tests: TESTS.reduce((s, [, l]) => s + l.length, 0), scy: SCY.length };
  document.querySelectorAll('[data-count]').forEach(el => el.textContent = counts[el.dataset.count]);
})();

// ================================================================ the road to Nexus
(function () {
  const box = $('#nlist'), st = $('#nstatus'), prog = $('#nprog');
  const ST = { done: ['Done', 'ok'], testing: ['Testing', 'cold'], doing: ['In progress', 'warn'], todo: ['To do', ''] };
  const flat = NEXUS.flatMap(([g, l]) => l.map(([id, t, s0, n]) => ({ g, id, t, s0, n })));
  box.innerHTML = NEXUS.map(([g, l]) => `<div class="card"><div class="k">${esc(g)}</div><div style="display:grid;gap:8px">${l.map(([id, t, s0, n]) => `<div class="nrow" data-id="${id}" style="display:grid;gap:4px;padding:8px 0;border-top:1px solid var(--line)">
<div class="row" style="justify-content:space-between"><b style="font-weight:600">${esc(t)}</b><span class="row">${Object.entries(ST).map(([k, [lab]]) => `<button class="chip" data-s="${k}" aria-pressed="false">${lab}</button>`).join('')}</span></div>
${n ? `<p class="note">${esc(n)}</p>` : ''}<textarea rows="1" placeholder="A note (optional)" aria-label="Note on ${esc(t)}"></textarea></div>`).join('')}</div></div>`).join('');
  function paint() {
    const d = DB.all('nexus'); let c = { done: 0, testing: 0, doing: 0, todo: 0 };
    box.querySelectorAll('.nrow').forEach(el => { const it = flat.find(x => x.id === el.dataset.id), s = d[el.dataset.id], cur = (s && s.status) || it.s0; c[cur]++;
      el.querySelectorAll('[data-s]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.s === cur)));
      const ta = $('textarea', el); if (document.activeElement !== ta && s && typeof s.note === 'string') ta.value = s.note; });
    const pct = Math.round(100 * (c.done + 0.5 * c.testing + 0.3 * c.doing) / flat.length);
    { const bar = $('#nbar'); if (bar) { $('i', bar).style.width = pct + '%'; bar.setAttribute('aria-valuenow', String(pct)); } }
    prog.innerHTML = `<span><b>${pct}%</b> of the way</span><span><b>${c.done}</b> done</span><span><b>${c.testing}</b> testing</span><span><b>${c.doing}</b> in progress</span><span><b>${c.todo}</b> to do</span>`;
  }
  async function save(id, status) { const el = box.querySelector(`.nrow[data-id="${id}"]`), it = flat.find(x => x.id === id), prev = DB.get('nexus', id) || {};
    try { await DB.set('nexus', id, { id, item: it.t, status: status || prev.status || it.s0, note: $('textarea', el).value.slice(0, 2000) }); st.textContent = 'Saved.'; } catch (e) { saveMsg(st, e); } paint(); }
  box.addEventListener('click', e => { const b = e.target.closest('button[data-s]'); if (b) save(b.closest('.nrow').dataset.id, b.dataset.s); });
  box.addEventListener('change', e => { if (e.target.tagName === 'TEXTAREA') save(e.target.closest('.nrow').dataset.id, null); });
  paint(); DB.on('nexus', (d, err) => { if (err) { st.textContent = err === 'offline' ? 'Marking lines works when this page is opened in Claude.' : ''; return; } paint(); });
})();

// ================================================================ the Weapons tab: every weapon model of ours, a picture on each card, 3D when opened
(function () {
  const grid = $('#wtgrid'), fl = $('#wtfilter'); if (!grid) return;
  let show = 'all';
  fl.innerHTML = [['all', 'All']].concat(WTAB.groups.map(g => [g.id, g.name])).map(([k, n]) => `<button class="chip" data-g="${k}" aria-pressed="${k === show}">${esc(n)}</button>`).join('');
  grid.innerHTML = WTAB.groups.map(g => `<div class="wtg" data-g="${g.id}" style="grid-column:1/-1;margin-top:6px"><h3 style="font-family:var(--display);font-weight:400;font-size:20px;color:var(--accent)">${esc(g.name)}</h3></div>` +
    g.items.map(it => `<article class="wcard" data-key="${esc(it.key)}" data-g="${g.id}">${WTHUMB(it.key)}
<div class="stage"><canvas aria-label="${esc(it.name)}, 3D"></canvas><div class="ld">${GL.ok ? 'Loading…' : 'Live previews need WebGL'}</div><div class="ov"><span class="ovl"></span><span class="ovr"></span></div></div>
<div class="wbody"><div class="t"><h3>${esc(it.name)}</h3></div><p class="now">${esc([it.tier, it.where].filter(Boolean).join(' · '))}</p></div></article>`).join('')).join('');
  grid.querySelectorAll('.wcard').forEach(el => whenNear(el, () => { if (!GL.ok) return; const v = weaponView($('canvas', el), info => { const ld = $('.ld', el); if (info.error) { ld.textContent = 'This preview didn\'t load.'; return; } ld.textContent = ''; $('.ovr', el).textContent = info.length ? `${info.length.toFixed(2)} m long` : ''; }); v.show(el.dataset.key, 'o1', 'own'); }));
  fl.addEventListener('click', e => { const b = e.target.closest('button[data-g]'); if (!b) return; show = b.dataset.g; fl.querySelectorAll('button').forEach(q => q.setAttribute('aria-pressed', String(q === b)));
    grid.querySelectorAll('[data-g]').forEach(x => x.hidden = show !== 'all' && x.dataset.g !== show); });
})();
