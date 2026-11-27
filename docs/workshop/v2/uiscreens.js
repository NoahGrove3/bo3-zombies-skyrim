// Every in-game screen of the mod, drawn over a real frame of Noah's own game (his NVIDIA clips at 2560x1600, our old HUD
// painted out) at true size: the page is 16:10 like his screen, and every size is in cqw (1cqw = 25.6 px on his screen). Each
// screen is made of the same parts (the night, the quests, the essence...), and the direction picked styles those parts
// (Ember Tally .dA, Dragon Stone .dB, Edge Clean .dC) - so picking a direction redraws every screen in it.
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
  mainmenu: () => `<div class="c-title menu"><b>THE CURSE OF ALDUIN</b><i></i>${['CONTINUE', 'NEW', 'LOAD', 'SETTINGS', 'QUIT'].map((x, i) => `<small class="${i === 0 ? 'on' : ''}">${x}</small>`).join('')}</div>`,
};
// the HUD of a night, as it stands (every night screen starts from it)
const NIGHT_HUD = () => UIP.night(12, 3, 17) + UIP.quests([['LAY THE RESTLESS TO REST', 'Slay the risen in the altar\'s light  7 / 15', 0.47]]) + UIP.compass() + UIP.essence('12,480') + UIP.brews(4);
// [group id, group name, [[id, name, what it is, frame, draw]]] - frame: the real still behind it (frames/<name>.jpg)
const UI_SCREENS = [
  ['hud', 'During a night', [
    ['night', 'Night counter', 'The night, its wave and the dead left, top left - BO3\'s round counter in the Curse\'s look.', 'night', () => NIGHT_HUD()],
    ['quests', 'Quests and trials', 'Under the night counter: the trial (its task and count) and the quests you track.', 'trial', () => UIP.night(4, 1, 9) + UIP.quests([['LAY THE RESTLESS TO REST', 'Slay the risen in the altar\'s light  8 / 25', 0.32], ['BEFORE THE STORM', 'Talk to the Jarl of Whiterun']]) + UIP.compass() + UIP.essence('109,283') + UIP.brews(3)],
    ['compass', 'Compass', 'Top centre: the directions, the quest marker, the altars and the dead as dots.', 'night2', () => UIP.compass()],
    ['essence', 'Cursed Essence and kill pops', 'Bottom right: your essence; the essence each kill earns rises where it fell, like BO3\'s points.', 'night', () => UIP.essence('12,480', '+50') + UIP.kill(46, 44, '+10') + UIP.kill(58, 40, '+10') + UIP.kill(70, 56, '+20 HEADSHOT')],
    ['perks', 'Brews (perks)', 'The brews you\'ve drunk as a row of glowing icons, bottom left - BO3\'s perk-a-colas.', 'night', () => UIP.brews(5)],
    ['boss', 'Boss bar', 'Top centre under the compass: the boss\'s name and its long bar.', 'boss', () => UIP.compass() + UIP.boss('GRAHZAH, ALDUIN\'S GRAVE-WING', 0.62)],
    ['foehp', 'Health over the dead', 'Small bars over the wounded near you; a name over the great ones.', 'night', () => UIP.foe(36, 36, 0.4) + UIP.foe(55, 30, 0.8, 'Minotaur Lord')],
    ['hit', 'Getting hit', 'A red edge after a hit, and an arc toward a blow you can\'t see.', 'night2', () => UIP.hit()],
    ['marker', 'Prompts at the altar and the Tablet', 'What a held Activate does at one of ours: the Tablet, the altar.', 'altar', () => UIP.prompt('E', 'Hold to turn the Tablet', '1,500') + UIP.hold(0.45, '')],
    ['between', 'Before the night', 'Dusk: a 10 s count before the night; nothing between waves.', 'village', () => UIP.countdown(10) + UIP.essence('115,973') + UIP.compass()],
    ['weapon', 'A Tablet weapon\'s uses', 'A donated weapon\'s uses left as a small ring beside the bars.', 'firstperson', () => UIP.rings([[0.66, 'USES 66']])],
  ]],
  ['pop', 'Pop-ups and words', [
    ['slain', 'Dragon Priest slain', 'The relic card: the priest\'s name and the relic you received.', 'boss', () => UIP.card('DRAGON PRIEST SLAIN', 'Vahlok the Jailor', 'Jailor\'s Keyhammer received')],
    ['subs', 'Subtitles', 'Felldir\'s words at his altar, centred low, his name small above.', 'altar', () => UIP.subs('FELLDIR THE OLD', 'The city is yours again. Ask, and I will answer.')],
    ['death', 'Falling', 'The screen drains to grey; YOU FELL, the night you reached, and the bar to rise.', 'night', () => UIP.death(12, 431, 0.55), 'grey'],
    ['msg', 'Message boxes', 'Choices from the mod (the Tester\'s Kit): a short list of buttons.', 'altar2', () => UIP.dim() + UIP.msg('Tester\'s Kit', ['Weapons', 'The night', 'Stay alive', 'The dead', 'Close'])],
  ]],
  ['altar', 'The Echo Altar', [
    ['altarmenu', 'The altar menu', 'Its sections down the left; the one open on the right.', 'altar', () => UIP.dim() + UIP.panel('WHITERUN &middot; ECHO ALTAR', [['Your Kit', '', 1], ['Armory'], ['Brews'], ['Thu\'um and dragon'], ['Trials'], ['Talk']], 'left') + UIP.panel('YOUR KIT', [['Radiant Quarterstaff', 'Tier II'], ['Summoner Set', 'Tier I'], ['Spells', '6'], ['Upgrade to Tier III', '8,000', 1]], 'right')],
    ['armory', 'Armory and upgrades', 'Each item, its tier, the cost on the right; locked ones faint.', 'altar', () => UIP.dim() + UIP.panel('ARMORY', [['Fire Flask', 'Tier II'], ['Frost Flask', 'Tier I'], ['Shock Flask', 'locked']], 'mid')],
    ['perkmenu', 'Brews', 'The brews you can buy: each its rune, what it does, its price.', 'altar', () => UIP.dim() + UIP.panel('BREWS', [['Troll\'s Hide Mead', '2,500', 1], ['Stormrunner Brew', '2,000'], ['Last Stand Ale', '1,500'], ['Dragonsblood Cider', '3,000']], 'mid')],
    ['thuum', 'Thu\'um', 'The city\'s shout, its words lighting as you learn them.', 'altar2', () => UIP.dim() + UIP.text('THU\'UM', 'Unrelenting Force &middot; 2 of 3 words', 'mid') + UIP.words(['FUS', 'RO', 'DAH'], 2)],
    ['trialsmenu', 'Trials', 'The four trials, the one to take next.', 'altar', () => UIP.dim() + UIP.panel('TRIALS', [['Lay the Restless to Rest', 'done'], ['The Stone\'s Whisper', 'next', 1], ['Hold the Light', ''], ['Break the Seals', '']], 'mid')],
    ['talk', 'Talk', 'Felldir\'s four topics, each with how many you haven\'t asked (four questions at most each).', 'altar', () => UIP.dim() + UIP.panel('TALK', [['The Curse', '2 new', 1], ['The Dragon Priests', '1 new'], ['The cities'], ['Felldir']], 'mid')],
    ['tablet', 'Tablet of Mysteries', 'Hold to turn: the weapon\'s name and its uses come up.', 'altar2', () => UIP.center('THE TABLET OF MYSTERIES', 'Stormtalons', 'Glass claws &middot; 100 uses')],
    ['classpick', 'Choosing your class', 'Four columns: each class\'s weapon.', 'altar', () => UIP.dim() + UIP.classes([['Summoner', 'Radiant Quarterstaff'], ['Destruction', 'Carian Wand'], ['Alchemist', 'Shadowflame Scythe'], ['Spellsword', 'Spellbound Rapier']], 2)],
    ['codex', 'The Codex', 'The book of the mod: chapters on the left, the page on the right.', 'altar2', () => UIP.dim() + UIP.panel('CODEX', [['I. The Curse'], ['II. The Nights', '', 1], ['III. The Classes'], ['IV. Waking an Altar']], 'left') + UIP.text('THE NIGHTS', 'Each night the dead come down red beams of light. Waves climb; every fifth night a boss.', 'right')],
  ]],
  ['start', 'Starting the game', [
    ['boot', 'Launch screen', 'The Alduin painting (the one the game shows at launch), the title, PRESS ANY KEY.', 'title', () => UIP.title()],
    ['main', 'Main menu', 'Continue, New, Load, Settings, Quit under the title, on the Alduin painting.', 'title', () => UIP.mainmenu()],
    ['setup', 'New character', 'After the creator: your Helgen companion and Survival Mode.', 'racemenu', () => UIP.dim() + UIP.panel('A NEW LIFE', [['Ralof', '', 1], ['Hadvar'], ['Survival Mode', 'On']], 'mid')],
    ['loading', 'Loading screens', 'One of the 123 fitting screens (here the Draugr); a line of lore, a thin progress line.', 'loadscreen', () => UIP.loadline('The souls Alduin holds are restless. Free a city and its dead sleep.')],
  ]],
  ['skyrim', 'Skyrim\'s own menus', [
    ['inv', 'Inventory', 'Skyrim\'s own (Edge UI), as it is in your game: kept.', 'inventory', () => ''],
    ['magic', 'Magic', 'Skyrim\'s own (Edge UI): kept.', 'magic', () => ''],
    ['tween', 'Menu wheel', 'Skyrim\'s own (Edge UI): kept.', 'tween', () => ''],
  ]],
];
// where each stands: in the game now (in its old look), kept, next (to be built in the picked direction), Skyrim's own
const UI_STATE = { night: 'next', quests: 'next', compass: 'next', essence: 'next', perks: 'next', boss: 'next', foehp: 'next', hit: 'in', marker: 'next', between: 'in',
  weapon: 'next', slain: 'next', subs: 'in', death: 'next', msg: 'next', altarmenu: 'next', armory: 'next', perkmenu: 'next', thuum: 'next', trialsmenu: 'next', talk: 'next',
  tablet: 'next', classpick: 'next', codex: 'next', boot: 'in', main: 'in', setup: 'next', loading: 'in', inv: 'own', magic: 'own', tween: 'own' };
function uiStage(frame, dir, inner, extra) {
  return `<div class="stage d${dir}${extra ? ' ' + extra : ''}" style="background-image:url('frames/${frame}.jpg')">${inner}</div>`;
}
