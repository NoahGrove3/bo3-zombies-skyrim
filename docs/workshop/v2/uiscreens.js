// Every in-game screen of the mod, drawn over a real frame of Noah's own game (his NVIDIA clips at 2560x1600, our old HUD
// painted out) at true size: the page is 16:10 like his screen, and every size is in cqw (1cqw = 25.6 px on his screen). Each
// screen is made of the same parts (the night, the quests, the essence...), and the direction picked styles those parts
// (Ember Tally .dA, Dragonbone .dD, Echo Glass .dE) - so picking a direction redraws every screen in it.
const ROMAN = n => { const r = [[10, 'X'], [9, 'IX'], [5, 'V'], [4, 'IV'], [1, 'I']]; let s = ''; for (const [v, c] of r) while (n >= v) { s += c; n -= v; } return s; };
function tallySvg(n) {   // BO3's round tally - four strokes and a slash through them - scratched like runes, in ember
  let s = '', x = 6; const r = i => ((Math.sin(i * 12.9898 + 3.1) * 43758.5453) % 1 + 1) % 1;
  for (let i = 0; i < n; i++) {
    if (i % 5 === 4) { const x0 = x - 4 * 13 - 3; s += `<path d="M${x0} ${44 - r(i) * 5} L${x - 8} ${17 + r(i + 3) * 4}"/>`; x += 15; continue; }
    s += `<path d="M${x + r(i) * 3} ${8 + r(i + 1) * 5} L${x + 2 - r(i + 2) * 3} ${54 - r(i + 5) * 5}"/>`; x += 13;
  }
  return `<svg class="tally" viewBox="0 0 ${x + 6} 62" preserveAspectRatio="xMinYMid meet" aria-hidden="true"><g class="t1">${s}</g><g class="t2">${s}</g></svg>`;
}
const UIP = {
  night: (n, wave, left) => `<div class="c-night"><small>NIGHT</small>${tallySvg(n)}<b class="num">${n}</b><b class="rom">${ROMAN(n)}</b><span class="sub">Wave ${wave} &middot; ${left} left</span></div>`,
  quests: list => `<div class="c-quests">${list.map(([t, l, v]) => `<div class="q"><b>${t}</b><span>${l}</span>${v != null ? `<i style="--v:${v * 100}%"></i>` : ''}</div>`).join('')}</div>`,
  compass: () => `<div class="c-comp"><span>W</span><em class="m">&#9670;</em><span>N</span><em class="r">&#9679;</em><em class="r">&#9679;</em><span>E</span></div>`,
  essence: (v, pop) => `<div class="c-ess">${pop ? `<span class="pop">${pop}</span>` : ''}<small>CURSED ESSENCE</small><b>${v}</b></div>`,
  brews: n => `<div class="c-brews">${[['#c0392b', '&#x16A0;'], ['#3f78c9', '&#x16B1;'], ['#6f9e4a', '&#x16C9;'], ['#d6a63a', '&#x16DF;'], ['#8a5cc9', '&#x16D2;']].slice(0, n).map(([c, r]) => `<span style="--bc:${c}"><i>${r}</i></span>`).join('')}</div>`,
  kill: (x, y, t) => `<span class="c-kill" style="left:${x}%;top:${y}%">${t}</span>`,
  boss: (name, v) => `<div class="c-boss"><b>${name}</b><i style="--v:${v * 100}%"></i></div>`,
  foe: (x, y, v, name) => `<div class="c-foe${name ? ' big' : ''}" style="left:${x}%;top:${y}%">${name ? `<b>${name}</b>` : ''}<i style="--v:${v * 100}%"></i></div>`,
  hit: () => `<div class="c-vig"></div><div class="c-arc"></div>`,
  prompt: (key, text, cost) => `<div class="c-prompt"><kbd>${key}</kbd>${text}${cost ? `<small>${cost}</small>` : ''}</div>`,
  hold: (v, text) => `<div class="c-hold"><i style="--v:${v * 100}%"></i><span>${text}</span></div>`,
  center: (small, big, sub) => `<div class="c-center"><small>${small}</small><b>${big}</b>${sub ? `<span>${sub}</span>` : ''}</div>`,
  card: (small, big, sub) => `<div class="c-card"><small>${small}</small><b>${big}</b><span>${sub}</span></div>`,
  subs: (who, text) => `<div class="c-subs"><small>${who}</small>${text}</div>`,
  death: (night, kills, v) => `<div class="c-death"><b>YOU FELL</b><span>Night ${night} &middot; ${kills} of the dead</span><i style="--v:${v * 100}%"></i><small>HOLD TO RISE</small></div>`,
  msg: (title, btns) => `<div class="c-msg"><b>${title}</b>${btns.map((x, i) => `<span class="${i === 0 ? 'on' : ''}">${x}</span>`).join('')}</div>`,
  panel: (title, rows, cls) => `<div class="c-panel ${cls || ''}"><div class="ph">${title}</div>${rows.map(([a, b, on]) => `<div class="row${on ? ' on' : ''}"><span>${a}</span><em>${b || ''}</em></div>`).join('')}</div>`,
  text: (title, body, cls) => `<div class="c-panel ${cls || ''}"><div class="ph">${title}</div><p>${body}</p></div>`,
  dim: () => `<div class="c-dim"></div>`,
  rings: list => `<div class="c-rings">${list.map(([v, t]) => `<span><i style="--v:${v * 100}%"></i>${t}</span>`).join('')}</div>`,
  countdown: (n) => `<div class="c-center count"><small>THE NIGHT BEGINS IN</small><b>${n}</b></div>`,
  classes: (list, on) => `<div class="c-classes">${list.map(([a, b], i) => `<div class="${i === on ? 'on' : ''}"><b>${a}</b><span>${b}</span></div>`).join('')}</div>`,
  words: (w, n) => `<div class="c-words">${w.map((x, i) => `<b class="${i < n ? 'on' : ''}">${x}</b>`).join('')}</div>`,
  title: () => `<div class="c-title"><b>THE CURSE OF ALDUIN</b><i></i><small>PRESS ANY KEY</small></div>`,
  loadline: t => `<div class="c-load"><span>${t}</span><i></i></div>`,
  // (Noah: "the tracked quests should only show under the night counter and for the echo altar quests it should only show above the echo altar")
  altarq: (x, y, t, l, v) => `<div class="c-altarq" style="left:${x}%;top:${y}%"><b>${t}</b><span>${l}</span>${v != null ? `<i style="--v:${v * 100}%"></i>` : ''}</div>`,
  // co-op (1-4 players): BO3's four colours - white, blue, yellow, green
  team: list => `<div class="c-team">${list.map(([n, c, ess, hp, down]) => `<div class="tm${down ? ' down' : ''}" style="--pc:${c}"><b>${n}</b><em>${ess}</em><i style="--v:${hp * 100}%"></i></div>`).join('')}</div>`,
  revive: (x, y, name, v) => `<div class="c-revive" style="left:${x}%;top:${y}%"><span class="ico"></span><b>${name} is down</b><i style="--v:${v * 100}%"></i></div>`,
  bleed: (v, n) => `<div class="c-bleed"><b>YOU ARE DOWN</b><i style="--v:${v * 100}%"></i><span>${n}</span></div>`,
  plate: (x, y, name, cls, c) => `<div class="c-plate" style="left:${x}%;top:${y}%;--pc:${c}"><b>${name}</b><small>${cls}</small></div>`,
  score: (night, rows) => `<div class="c-score"><div class="sh"><b>NIGHT ${night}</b><span>ESSENCE</span><span>KILLS</span><span>DOWNS</span><span>REVIVES</span><span>TRIALS</span></div>${rows.map(([n, c, cls, ...v]) => `<div class="sr" style="--pc:${c}"><b>${n}<small>${cls}</small></b>${v.map(x => `<span>${x}</span>`).join('')}</div>`).join('')}</div>`,
  feed: lines => `<div class="c-feed">${lines.map(([c, t]) => `<span style="--pc:${c}">${t}</span>`).join('')}</div>`,
  // the Echo Altar, rich (Noah: "include the class images and perk and tab icons - the preview looks too bland")
  altar: (tab, body) => `<div class="c-altar"><div class="at">${ALTAR_TABS.map(([id, n, ico]) => `<span class="${id === tab ? 'on' : ''}">${ico}<b>${n}</b></span>`).join('')}</div><div class="ab">${body}</div></div>`,
  // Skyrim's own menus, ours: recoloured (Untarnished UI's layout in the Curse's colours) or remade (our own pages)
  inv: (mode, kind) => INV[kind](mode),
  mainmenu: () => `<div class="c-title menu"><b>THE CURSE OF ALDUIN</b><i></i>${['CONTINUE', 'NEW', 'LOAD', 'CO-OP PARTY', 'SETTINGS', 'QUIT'].map((x, i) => `<small class="${i === 1 ? 'on' : ''}">${x}</small>`).join('')}</div>` + EMBERS() + MHINTS([['ARROWS', 'CHOOSE'], ['ENTER', 'SELECT']]),
  // (Noah, Nov 27 test of 2b534e1: the main menu's own pages, in the same look - Load, Settings, the party, its questions)
  mload: () => `<div class="c-mpage"><div class="mph"><b>LOAD</b><i></i></div><div class="mpf">&lsaquo;&nbsp;&nbsp; ALL CHARACTERS &nbsp;&nbsp;&rsaquo;</div>
    <div class="mpl">${[['PRISONER', 'Level 24 &middot; Whiterun', 'Nov 27, 2026  11:04 PM', 'SAVE', 1], ['PRISONER', 'Level 24 &middot; Dragonsreach', 'Nov 27, 2026  10:41 PM', 'AUTOSAVE'], ['PRISONER', 'Level 23 &middot; Riverwood', 'Nov 26, 2026  09:12 PM', 'SAVE'], ['ARVEL', 'Level 7 &middot; Helgen', 'Nov 25, 2026  06:30 PM', 'SAVE'], ['ARVEL', 'Level 6 &middot; Bleak Falls Barrow', 'Nov 25, 2026  05:58 PM', 'AUTOSAVE']].map(([n, l, d, t, on]) => `<div class="mr${on ? ' on' : ''}"><b>${n}</b><em>${d}</em><span>${l}</span><small class="${t === 'AUTOSAVE' ? 'auto' : ''}">${t}</small></div>`).join('')}</div>
    <div class="mpd"><div class="shot" style="background-image:url('frames/village.jpg')"></div><b>PRISONER</b><span>Level 24 &middot; Dark Elf</span><span>Whiterun</span><small>Nov 27, 2026  11:04 PM &middot; Played 14h 22m</small></div></div>` + MHINTS([['ENTER', 'LOAD'], ['DEL', 'DELETE'], ['PGUP PGDN', 'CHARACTER'], ['ESC', 'BACK']]),
  msettings: () => `<div class="c-mpage"><div class="mph"><b>SETTINGS</b><i></i></div><div class="mpt">${['GRAPHICS', 'DISPLAY', 'GAMEPLAY', 'CONTROLS', 'AUDIO'].map((t, i) => `<span class="${i === 1 ? 'on' : ''}">${t}</span>`).join('')}</div>
    <div class="mps">${[['RESOLUTION', '2560 x 1600', 0, 1, 1], ['DISPLAY MODE', 'BORDERLESS', 0, 1], ['FRAME RATE CAP', '120', 0, 0], ['VSYNC', 'OFF', 0, 0], ['FIELD OF VIEW - FIRST PERSON', '130', .89], ['FIELD OF VIEW - THIRD PERSON', '120', .74]].map(([l, v, sl, rs, on]) => `<div class="ms${on ? ' on' : ''}"><b>${l}${rs ? '<small>RESTART</small>' : ''}</b>${sl ? `<i style="--v:${sl * 100}%"></i><em>${v}</em>` : `<em class="ch">&lsaquo;&nbsp; ${v} &nbsp;&rsaquo;</em>`}</div>`).join('')}</div>
    <p class="mpn">Settings marked RESTART take effect the next time you start Skyrim.</p></div>` + MHINTS([['Q  E', 'TAB'], ['ARROWS', 'CHANGE'], ['ESC', 'BACK']]),
  mparty: () => `<div class="c-mpage"><div class="mph"><b>CO-OP PARTY</b><i></i></div><p class="mpst">In a party &middot; 2 in the party</p><div class="mpw"><span class="on">INVITE FRIENDS</span><span class="red">CLOSE THE PARTY</span></div>
    <div class="mpt2"><div class="hd"><span>TONIGHT</span><span>KILLS</span><span>HEADS</span><span>STREAK</span><span>ESSENCE</span><span>REVIVES</span></div><div><span>Noah (you)</span><span>431</span><span>88</span><span>42</span><span>12,480</span><span>5</span></div><div><span>Shane</span><span>302</span><span>41</span><span>27</span><span>9,215</span><span>3</span></div></div>
    <div class="mpx"><b>LAST NIGHT &middot; NIGHT 11</b><span>398 kills &middot; 72 headshots &middot; best streak 38 &middot; 11,020 Cursed Essence</span><b>RECORDS</b><span>best night 14 &middot; nights survived 63 &middot; kills 9,881 &middot; headshots 2,140</span></div></div>` + MHINTS([['ARROWS', 'CHOOSE'], ['ENTER', 'SELECT'], ['ESC', 'CLOSE']]),
  mask: () => `<div class="c-mask"><b>START A NEW GAME?</b><div><span class="on">YES</span><span>NO</span></div></div>` + MHINTS([['ENTER', 'CHOOSE'], ['ESC', 'NO']]),
};
// (Noah, Nov 27: "more dense realistic embers not blinking circles") the main menu's sparks: short bright streaks rising and cooling
function EMBERS() {
  const r = i => ((Math.sin(i * 91.7 + 1.3) * 43758.5453) % 1 + 1) % 1;
  let s = '';
  for (let i = 0; i < 90; i++) {
    const x = r(i) * 100, y = 30 + r(i + 7) * 70, l = 0.4 + r(i + 3) * 1.4, a = -8 - r(i + 11) * 22, o = .35 + r(i + 5) * .65, h = r(i + 13);
    s += `<i style="left:${x.toFixed(1)}%;top:${y.toFixed(1)}%;--l:${l.toFixed(2)};--a:${a.toFixed(0)}deg;--o:${o.toFixed(2)};--c:${h > .6 ? '#ffe2a8' : h > .25 ? '#ff9a3c' : '#c8401c'};animation-delay:-${(r(i + 17) * 6).toFixed(2)}s;animation-duration:${(4 + r(i + 19) * 5).toFixed(2)}s"></i>`;
  }
  return `<div class="c-embers" aria-hidden="true">${s}</div>`;
}
const MHINTS = list => `<div class="c-mhints">${list.map(([k, w]) => `<span><kbd>${k}</kbd>${w}</span>`).join('')}</div>`;
// the HUD of a night, as it stands (every night screen starts from it)
const NIGHT_HUD = () => UIP.night(12, 3, 17) + UIP.quests([['BEFORE THE STORM', 'Talk to the Jarl of Whiterun']]) + UIP.compass() + UIP.essence('12,480') + UIP.brews(4);
// [group id, group name, [[id, name, what it is, frame, draw]]] - frame: the real still behind it (frames/<name>.jpg)
const UI_SCREENS = [
  ['hud', 'During a night', [
    ['night', 'Night counter', 'The night, its wave and the dead left, top left - BO3\'s round counter in the Curse\'s look.', 'night', () => NIGHT_HUD()],
    ['quests', 'Quests and trials', 'Your note: the quests you track sit only under the night counter; an Echo Altar trial shows only above its altar, in the world, where you see it.', 'trial', () => UIP.night(4, 1, 9) + UIP.quests([['BEFORE THE STORM', 'Talk to the Jarl of Whiterun']]) + UIP.altarq(50, 21, 'LAY THE RESTLESS TO REST', 'Slay the risen in the altar\'s light  8 / 25', 0.32) + UIP.compass() + UIP.essence('109,283') + UIP.brews(3)],
    ['compass', 'Compass', 'Top centre: the directions, the quest marker, the altars and the dead as dots.', 'night2', () => UIP.compass()],
    ['essence', 'Cursed Essence and kill pops', 'Bottom right: your essence; the essence each kill earns rises where it fell, like BO3\'s points.', 'night', () => UIP.essence('12,480', '+50') + UIP.kill(46, 44, '+10') + UIP.kill(58, 40, '+10') + UIP.kill(70, 56, '+20 HEADSHOT')],
    ['perks', 'Brews (perks)', 'The brews you\'ve drunk as a row of glowing icons, bottom left - BO3\'s perk-a-colas.', 'night', () => UIP.brews(5)],
    ['boss', 'Boss bar', 'Top centre under the compass: the boss\'s name and its long bar.', 'boss', () => UIP.compass() + UIP.boss('GRAHZAH, ALDUIN\'S GRAVE-WING', 0.62)],
    ['foehp', 'Health over enemies', 'Your note, yes: small bars over every wounded enemy near you in all of Skyrim, night or day; a name over the great ones.', 'night', () => UIP.foe(36, 36, 0.4) + UIP.foe(55, 30, 0.8, 'Minotaur Lord')],
    ['hit', 'Getting hit', 'A red edge after a hit, and an arc toward a blow you can\'t see.', 'night2', () => UIP.hit()],
    ['marker', 'Prompts at the altar and the Tablet', 'What a held Activate does at one of ours: the Tablet, the altar.', 'altar', () => UIP.prompt('E', 'Hold to turn the Tablet', '1,500') + UIP.hold(0.45, '')],
    ['between', 'Before the night', 'Dusk: a 10 s count before the night; nothing between waves.', 'village', () => UIP.countdown(10) + UIP.essence('115,973') + UIP.compass()],
    ['weapon', 'A Tablet weapon\'s uses', 'Your note: beside the stamina bar on the right, clear of everything: the uses left as a small ring, in the weapon\'s rarity colour.', 'firstperson', () => UIP.rings([[0.66, 'USES 66']])],
  ]],
  ['pop', 'Pop-ups and words', [
    ['slain', 'Dragon Priest slain', 'The relic card: the priest\'s name and the relic you received.', 'boss', () => UIP.card('DRAGON PRIEST SLAIN', 'Vahlok the Jailor', 'Jailor\'s Keyhammer received')],
    ['subs', 'Subtitles', 'Felldir\'s words at his altar, centred low, his name small above.', 'altar', () => UIP.subs('FELLDIR THE OLD', 'The city is yours again. Ask, and I will answer.')],
    ['death', 'Falling', 'Your note: it all fades in and out smoothly - the grey drains in over a second, the words rise in, and fade away again when you rise.', 'night', () => UIP.death(12, 431, 0.55), 'grey'],
    ['msg', 'Message boxes', 'Choices from the mod (the Tester\'s Kit): a short list of buttons.', 'altar2', () => UIP.dim() + UIP.msg('Tester\'s Kit', ['Weapons', 'The night', 'Stay alive', 'The dead', 'Close'])],
  ]],
  ['altar', 'The Echo Altar', [
    ['altarmenu', 'The altar menu', 'Your note: the class painting, the tab icons along the top, every brew with its own icon. (Or keep your current altar menu: say so in the note.)', 'altar', () => UIP.dim() + UIP.altar('kit', ALTAR_BODY.kit())],
    ['armory', 'Armory and upgrades', 'Each item with its element icon, its tier as pips, the cost on the right; locked ones faint.', 'altar', () => UIP.dim() + UIP.altar('armory', ALTAR_BODY.armory())],
    ['perkmenu', 'Brews', 'Every brew with its own icon (the ones in your game), what it does, its price; the ones you have glow.', 'altar', () => UIP.dim() + UIP.altar('brews', ALTAR_BODY.brews())],
    ['thuum', 'Thu\'um', 'The city\'s shout, its words lighting as you learn them, the dragon you face for the last one.', 'altar2', () => UIP.dim() + UIP.altar('thuum', ALTAR_BODY.thuum())],
    ['trialsmenu', 'Trials', 'The four trials as a path, the one to take next lit, the party\'s progress.', 'altar', () => UIP.dim() + UIP.altar('trials', ALTAR_BODY.trials())],
    ['talk', 'Talk', 'Felldir\'s topics, each with how many you haven\'t asked; his words open on the right.', 'altar', () => UIP.dim() + UIP.altar('talk', ALTAR_BODY.talk())],
    ['tablet', 'Tablet of Mysteries', 'Hold to turn: the won weapon\'s name in its rarity colour and its uses come up.', 'altar2', () => UIP.center('THE TABLET OF MYSTERIES &middot; EPIC', 'Stormtalons', 'Glass claws &middot; 100 uses')],
    ['classpick', 'Choosing your class', 'Each class on its painting (the ones in your game); the one you point at grows and shows its words.', 'altar', () => UIP.dim() + CLASSPICK()],
    ['codex', 'The Codex', 'Your note: keep the current Codex book as it is in your game.', 'altar2', () => UIP.dim() + UIP.panel('CODEX', [['I. The Curse'], ['II. The Nights', '', 1], ['III. The Classes'], ['IV. Waking an Altar']], 'left') + UIP.text('THE NIGHTS', 'Each night the dead come down red beams of light. Waves climb; every fifth night a boss.', 'right')],
  ]],
  ['start', 'Starting the game', [
    ['boot', 'Launch screen', 'The Alduin painting, the title, PRESS ANY KEY. Your note: new menu music - pick it under Menu music below.', 'title', () => UIP.title()],
    ['main', 'Main menu', 'Your notes (Nov 27): denser, real embers (sparks streaking up and cooling, no circles); the title\'s glow a soft haze, not blocky; the orange line clear of the letters; the words larger. Continue, New, Load, Co-op Party, Settings, Quit.', 'title', () => UIP.mainmenu()],
    ['m_load', 'Load', 'Your picks: every save with its picture, Delete (asked first; the save is put aside in Saves\\Deleted) and a filter by character.', 'title', () => UIP.mload()],
    ['m_settings', 'Settings', 'Your pick: five tabs on one page - Graphics, Display, Gameplay, Controls, Audio. What needs a restart is saved now and marked RESTART.', 'title', () => UIP.msettings()],
    ['m_party', 'Co-op party', 'Your note: the party page in the same look as the menu, your cursor over it.', 'title', () => UIP.mparty()],
    ['m_ask', 'Questions', 'Your note: Skyrim\'s questions (Start a new game?) in our words, YES and NO.', 'title', () => UIP.mask()],
    ['setup', 'New character', 'Your note: keep the current screen as it is.', 'racemenu', () => UIP.dim() + UIP.panel('A NEW LIFE', [['Ralof', '', 1], ['Hadvar'], ['Survival Mode', 'On']], 'mid')],
    ['loading', 'Loading screens', 'Your note: the painting always covers the whole screen on your monitor and your laptop (any shape of screen: it fills, never letterboxed). A line of lore, a thin progress line.', 'loadscreen', () => UIP.loadline('The souls Alduin holds are restless. Free a city and its dead sleep.')],
  ]],
  ['coop', 'Co-op (1-4 players)', [
    ['co_team', 'The party', 'Bottom right above your essence: each player\'s name in their colour (BO3\'s white, blue, yellow, green), their essence and health.', 'night', () => NIGHT_HUD() + UIP.team([['Noah', '#f2efe6', '12,480', .9], ['Shane', '#5aa7ff', '9,215', .55], ['Kade', '#f2d14a', '14,030', .3, 1], ['Mira', '#5fd16a', '6,870', 1]])],
    ['co_down', 'A friend is down', 'Over their body: a bleed-out ring and their name; hold Activate near them to lift them. Off-screen, a mark on the compass.', 'night2', () => UIP.compass() + UIP.revive(52, 42, 'Kade', .62) + UIP.prompt('E', 'Hold to lift Kade') + UIP.hold(.35, '')],
    ['co_bleed', 'You are down', 'The screen drains to grey and red at the edges; the ring is how long you have left; your friends\' marks show where they are.', 'night', () => UIP.bleed(.4, 'Shane is coming - 14 m'), 'grey'],
    ['co_plates', 'Names over the party', 'Each player\'s name and class over their head, in their colour; fades out past 40 m.', 'village', () => UIP.plate(38, 40, 'Shane', 'Summoner', '#5aa7ff') + UIP.plate(63, 46, 'Mira', 'Spellsword', '#5fd16a')],
    ['co_score', 'The scoreboard', 'Hold Tab (or Back): BO3\'s scoreboard - each player\'s essence, kills, downs, revives and the trial steps they did (your note: no headshots).', 'night2', () => UIP.dim() + UIP.score(12, [['Noah', '#f2efe6', 'Destruction', '12,480', 431, 2, 5, 6], ['Shane', '#5aa7ff', 'Summoner', '9,215', 302, 4, 3, 4], ['Kade', '#f2d14a', 'Alchemist', '14,030', 455, 1, 6, 7], ['Mira', '#5fd16a', 'Spellsword', '6,870', 210, 3, 1, 2]])],
    ['co_trial', 'A shared trial', 'Above the altar: the trial\'s count for the whole party, and who helped.', 'trial', () => UIP.altarq(50, 21, 'LAY THE RESTLESS TO REST', 'The party: 18 / 25  &middot;  Noah 7  Shane 5  Kade 4  Mira 2', .72)],
    ['co_feed', 'Joins and falls', 'Top right, small, fading on its own: who joined, who fell, who was lifted.', 'village', () => UIP.feed([['#5aa7ff', 'Shane joined the night'], ['#f2d14a', 'Kade fell'], ['#f2efe6', 'Noah lifted Kade']])],
  ]],
  ['skyrim', 'Skyrim\'s menus, ours', [
    ['inv_re', 'Inventory: recoloured', 'Untarnished UI\'s layout as you have it, in the Curse\'s colours and type.', 'night2', () => UIP.inv('re', 'items'), 'blur'],
    ['inv_new', 'Inventory: remade', 'Our own page: categories along the top, the item large on the right with its stats and rarity, the list cleaner.', 'night2', () => UIP.inv('new', 'items'), 'blur'],
    ['mag_re', 'Magic: recoloured', 'Untarnished UI\'s magic list in the Curse\'s colours, each spell its element icon.', 'firstperson', () => UIP.inv('re', 'magic'), 'blur'],
    ['mag_new', 'Magic: remade', 'Our own: schools as tabs, spells as rows with their element, the spell large on the right with its cost and what it does.', 'firstperson', () => UIP.inv('new', 'magic'), 'blur'],
    ['tab_re', 'Tab menu: recoloured', 'Skyrim\'s Tab menu (Items, Magic, Quests, Map, Level up) in the Curse\'s colours.', 'tween', () => UIP.inv('re', 'tab')],
    ['tab_new', 'Tab menu: remade', 'Our own: four carved medallions round you, the night and your essence above, your class below.', 'village', () => UIP.inv('new', 'tab')],
  ]],
];
// where each stands: in the game now (in its old look), kept, next (to be built in the picked direction), Skyrim's own
const UI_STATE = { night: 'next', quests: 'next', compass: 'next', essence: 'next', perks: 'next', boss: 'next', foehp: 'next', hit: 'in', marker: 'next', between: 'in',
  weapon: 'next', slain: 'next', subs: 'in', death: 'next', msg: 'next', altarmenu: 'next', armory: 'next', perkmenu: 'next', thuum: 'next', trialsmenu: 'next', talk: 'next',
  tablet: 'next', classpick: 'next', codex: 'keep', boot: 'in', main: 'in', m_load: 'in', m_settings: 'in', m_party: 'in', m_ask: 'in', setup: 'keep', loading: 'in',
  co_team: 'next', co_down: 'next', co_bleed: 'next', co_plates: 'next', co_score: 'next', co_trial: 'next', co_feed: 'next',
  inv_re: 'menu', inv_new: 'menu', mag_re: 'menu', mag_new: 'menu', tab_re: 'menu', tab_new: 'menu' };
// the two ways for each of Skyrim's menus (pick one of each pair)
const MENU_PAIRS = { inv_re: 'inv', inv_new: 'inv', mag_re: 'mag', mag_new: 'mag', tab_re: 'tab', tab_new: 'tab' };
function uiStage(frame, dir, inner, extra) {   // (D and E are built on A's places: the same screen, a new look)
  const cls = dir === 'A' ? 'dA' : 'dA d' + dir;
  return `<div class="stage ${cls}${extra ? ' ' + extra : ''}"><div class="bg" style="background-image:url('frames/${frame}.jpg')"></div>${inner}</div>`;
}
// ---- the Echo Altar's own art: tab icons, the class painting, every brew's icon
const ico = d => `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="${d}"/></svg>`;
const ALTAR_TABS = [
  ['kit', 'Your Kit', ico('M12 2l7 3v6c0 5-3.5 9-7 11-3.5-2-7-6-7-11V5z M12 6v12 M8 10h8')],
  ['armory', 'Armory', ico('M5 19L19 5 M15 5h4v4 M5 15l4 4 M3 21l3-3 M9 9l-4-4 M5 5v3 M5 5h3')],
  ['brews', 'Brews', ico('M9 3h6 M10 3v5l-4 8a3 3 0 003 5h6a3 3 0 003-5l-4-8V3 M7.5 14h9')],
  ['thuum', 'Thu\'um', ico('M4 12c3-6 6-6 8 0s5 6 8 0 M4 7c3-4 6-4 8 0 M12 17c2 4 5 4 8 0')],
  ['trials', 'Trials', ico('M12 3a9 9 0 110 18 9 9 0 010-18z M12 7v5l3 3 M3 12h2 M19 12h2')],
  ['talk', 'Talk', ico('M4 5h16v10H9l-5 4z M8 9h8 M8 12h5')],
];
const ART = n => `art/${n}`;
const BREWS = [['trollshide', 'Troll\'s Hide Mead', '+50 health', '3,000', 1], ['stormrunner', 'Stormrunner Brew', 'Run faster, sprint longer', '3,000', 1], ['laststand', 'Last Stand Ale', 'Fight on the ground when you fall', '3,000', 1],
  ['quickrevive', 'Quick Revive Draught', 'Lift a friend twice as fast', '3,000'], ['dragonsblood', 'Dragonsblood Cider', 'Shouts recover faster', '3,000'], ['manafont', 'Mana Font Draught', 'Magicka flows back faster', '3,000'],
  ['bladesong', 'Bladesong Mead', 'Faster swings', '3,000'], ['spellguard', 'Spellguard Ale', 'Less harm from magic', '3,000']];
const ALTAR_BODY = {
  kit: () => `<div class="akit"><div class="apaint" style="background-image:url('${ART('cls_magicschooldestruction.jpg')}')"><b>DESTRUCTION</b><small>Tier II &middot; Battle-mage</small></div>
    <div class="alist"><div class="arow on"><img src="${ART('el_arcane.png')}" alt=""><span>Carian Wand<small>Glintstone Mist &middot; Tier II</small></span><em>Upgrade &middot; 8,000</em></div>
    <div class="arow"><img src="${ART('el_fire.png')}" alt=""><span>Destruction Set<small>Arch-Mage\'s robes &middot; Tier I</small></span><em>Claim</em></div>
    <div class="arow"><img src="${ART('el_shock.png')}" alt=""><span>Spells<small>6 learned &middot; 2 to claim</small></span><em>Open</em></div>
    <div class="abrews">${BREWS.filter(b => b[4]).map(b => `<img src="${ART(b[0] + '.png')}" alt="${b[1]}" title="${b[1]}">`).join('')}<small>Your brews</small></div></div></div>`,
  armory: () => `<div class="alist wide">${[['el_fire', 'Fire Flask', 3, '4,000'], ['el_frost', 'Frost Flask', 2, '3,000'], ['el_shock', 'Shock Flask', 1, '2,000', 1], ['el_poison', 'Acid Flask', 0, 'locked'], ['el_sun', 'Holy Water', 0, 'locked']].map(([i, n, t, c, on]) => `<div class="arow${on ? ' on' : ''}${c === 'locked' ? ' lock' : ''}"><img src="${ART(i + '.png')}" alt=""><span>${n}<small class="pips">${[1, 2, 3].map(k => `<i class="${k <= t ? 'f' : ''}"></i>`).join('')}</small></span><em>${c}</em></div>`).join('')}</div>`,
  brews: () => `<div class="agrid">${BREWS.map(([id, n, d, c, have], i) => `<div class="abrew${have ? ' have' : ''}${i === 3 ? ' on' : ''}"><img src="${ART(id + '.png')}" alt=""><b>${n}</b><span>${d}</span><em>${have ? 'Drunk' : c}</em></div>`).join('')}</div>`,
  thuum: () => `<div class="athu"><b>UNRELENTING FORCE</b><small>Whiterun &middot; 2 of 3 words</small><div class="c-words" style="position:static;transform:none;justify-content:center;margin-top:calc(1.6 * var(--u))">${['FUS', 'RO', 'DAH'].map((w, i) => `<b class="${i < 2 ? 'on' : ''}">${w}</b>`).join('')}</div><p>The last word: slay the dragon that comes on night 15.</p></div>`,
  trials: () => `<div class="apath">${[['Lay the Restless to Rest', 'done'], ['The Stone\'s Whisper', 'next'], ['Repeat the Echo', ''], ['Break the Priest\'s Seals', '']].map(([n, st], i) => `<div class="ast ${st}"><span>${['I', 'II', 'III', 'IV'][i]}</span><b>${n}</b><small>${st === 'done' ? 'Done' : st === 'next' ? 'Next - tonight' : 'Locked'}</small></div>`).join('<i></i>')}</div>`,
  talk: () => `<div class="atalk"><div class="alist">${[['The story so far', '2 new', 1], ['This city', '1 new'], ['Your path', ''], ['Who are you?', '']].map(([n, c, on]) => `<div class="arow${on ? ' on' : ''}"><span>${n}</span><em>${c}</em></div>`).join('')}</div><p class="aq">"Alduin feeds on the dead of Skyrim. Each city you free takes a mouthful back from him."<small>FELLDIR THE OLD</small></p></div>`,
};
const CLASSPICK = () => `<div class="c-classart">${[['cls_magicconjurer.jpg', 'Summoner', 'Radiant Quarterstaff'], ['cls_magicschooldestruction.jpg', 'Destruction', 'Carian Wand'], ['cls_skillalchemyalchemist.jpg', 'Alchemist', 'Shadowflame Scythe'], ['cls_magicbattlemage.jpg', 'Spellsword', 'Spellbound Rapier']].map(([p, n, w], i) => `<div class="${i === 2 ? 'on' : ''}" style="background-image:url('${ART(p)}')"><b>${n}</b><span>${w}</span></div>`).join('')}</div><div class="c-classhead"><b>CHOOSE YOUR PATH</b><small>Each player picks their own path.</small></div>`;
// ---- Skyrim's menus: recoloured (Untarnished UI's layout) or remade (ours)
const SPELLS = [['el_fire', 'Fireball', 'Adept', '45'], ['el_frost', 'Ice Spike', 'Apprentice', '27'], ['el_shock', 'Chain Lightning', 'Adept', '52'], ['el_arcane', 'Arcane Cascade', 'Apprentice', '20'], ['el_sun', 'Searing Grasp', 'Expert', '45/s'], ['el_poison', 'Acid Spray', 'Adept', '38']];
const ITEMS = [['Carian Wand', 'Wand', '24', 'Epic'], ['Destruction Set: Robes', 'Robes', '', ''], ['Stormtalons', 'Tablet &middot; 66 uses', '31', 'Epic'], ['Fire Flask', 'Flask', '3', ''], ['Sweetroll', 'Food', '', ''], ['Troll\'s Hide Mead', 'Brew', '', '']];
const INV = {
  items: m => m === 're'
    ? `<div class="m-re"><div class="mh"><b>ALL</b><span>FILTER</span><span>SETTINGS</span></div><div class="mcol"><span>NAME</span><span>TYPE</span><span>DMG</span><span>VALUE</span></div>${ITEMS.map((r, i) => `<div class="mrow${i === 2 ? ' on' : ''}"><span>${r[0]}</span><span>${r[1]}</span><span>${r[2]}</span><span>${r[3]}</span></div>`).join('')}<div class="mcard"><b>STORMTALONS</b><span>Every 3rd hit chains lightning to 2 more.</span></div></div>`
    : `<div class="m-new"><div class="mtabs">${['ALL', 'WEAPONS', 'APPAREL', 'FLASKS', 'BREWS', 'MISC'].map((t, i) => `<span class="${i === 1 ? 'on' : ''}">${t}</span>`).join('')}</div><div class="mlist">${ITEMS.slice(0, 4).map((r, i) => `<div class="mrow${i === 2 ? ' on' : ''}"><i class="${r[3] ? 'r-' + r[3].toLowerCase() : ''}"></i><span>${r[0]}<small>${r[1]}</small></span><em>${r[2]}</em></div>`).join('')}</div><div class="mdet"><small class="r-epic">EPIC &middot; TABLET OF MYSTERIES</small><b>Stormtalons</b><div class="mstats"><span>Damage<b>31</b></span><span>Uses<b>66</b></span><span>Element<b>Shock</b></span></div><p>Every 3rd hit chains lightning to 2 more of the dead. Heavy: a lightning stun.</p><div class="muse"><i style="--v:66%"></i></div></div></div>`,
  magic: m => m === 're'
    ? `<div class="m-re"><div class="mh"><b>DESTRUCTION</b><span>FILTER</span><span>SETTINGS</span></div><div class="mcol"><span>NAME</span><span>LEVEL</span><span>COST</span></div>${SPELLS.map((r, i) => `<div class="mrow${i === 2 ? ' on' : ''}"><span><img src="${ART(r[0] + '.png')}" alt="">${r[1]}</span><span>${r[2]}</span><span>${r[3]}</span></div>`).join('')}<div class="mcard"><b>CHAIN LIGHTNING</b><span>Lightning that leaps to 2 more of the dead.</span></div></div>`
    : `<div class="m-new"><div class="mtabs">${['ALL', 'DESTRUCTION', 'CONJURATION', 'RESTORATION', 'POWERS', 'SHOUTS'].map((t, i) => `<span class="${i === 1 ? 'on' : ''}">${t}</span>`).join('')}</div><div class="mlist">${SPELLS.map((r, i) => `<div class="mrow${i === 2 ? ' on' : ''}"><img src="${ART(r[0] + '.png')}" alt=""><span>${r[1]}<small>${r[2]}</small></span><em>${r[3]}</em></div>`).join('')}</div><div class="mdet"><small>DESTRUCTION &middot; ADEPT</small><b>Chain Lightning</b><div class="mstats"><span>Magicka<b>52</b></span><span>Damage<b>40</b></span><span>Leaps<b>2</b></span></div><p>Lightning that strikes one of the dead and leaps to 2 more.</p></div></div>`,
  tab: m => m === 're'
    ? `<div class="m-tabre"><div class="mlvl">LEVEL 24 <i style="--v:62%"></i><span>Night 12 &middot; Whiterun</span></div><div class="mitems">${['ITEMS', 'MAGIC', 'QUESTS', 'MAP', 'LEVEL UP'].map((t, i) => `<span class="${i === 0 ? 'on' : ''}">${t}</span>`).join('')}</div></div>`
    : `<div class="m-tabnew"><div class="mtop"><b>NIGHT 12</b><span>12,480 CURSED ESSENCE</span></div>${[['ITEMS', 'M6 8h12l-1 12H7z M9 8a3 3 0 016 0'], ['MAGIC', 'M12 3c3 4 5 7 5 10a5 5 0 01-10 0c0-3 2-6 5-10z'], ['QUESTS', 'M6 4h10l2 3v13H6z M9 9h6 M9 13h6'], ['MAP', 'M3 6l6-2 6 2 6-2v14l-6 2-6-2-6 2z M9 4v14 M15 6v14']].map(([t, d], i) => `<div class="medal m${i}${i === 0 ? ' on' : ''}"><svg viewBox="0 0 24 24"><path d="${d}"/></svg><b>${t}</b></div>`).join('')}<div class="mbot">DESTRUCTION &middot; LEVEL 24<i style="--v:62%"></i></div></div>`,
};
