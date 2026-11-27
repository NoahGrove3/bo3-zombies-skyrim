// Every in-game screen of the mod as a small preview (carried from the old workshop's UI tab, brought up to date): the workshop's
// In-game UI section draws them; each gets Approve / Change and a note.
const UI_SCREENS = (function () {
  // the pieces each preview is built from
  const night = (n, wave, left) => `<div class="u-night"><small>N I G H T</small><b>${n}</b><span>Wave ${wave} / 5 &middot; ${left} left</span></div>`;   // (as in the game now: smaller, cleaner)
  const bars = () => `<div class="u-bars"><i class="hp" style="--v:.72"></i><i class="mp" style="--v:.5"></i><i class="sp" style="--v:.86"></i></div>`;
  const perks = () => `<div class="u-perks">${['&#x16A0;', '&#x16B1;', '&#x16C9;', '&#x16DF;'].map(r => `<span>${r}</span>`).join('')}</div>`;
  const ult = v => `<div class="u-ult"><i style="width:${v * 100}%"></i></div>`;
  const comp = () => `<div class="u-comp"><span>W</span><span class="m">&#9670;</span><span>N</span><span class="r">&#9679;</span><span>E</span></div>`;
  const panel = (title, body, w) => `<div class="u-panel" style="${w ? 'width:' + w : ''}"><div class="u-ph">${title}</div>${body}</div>`;
  const rows = l => l.map(([a, b, on]) => `<div class="u-row${on ? ' on' : ''}"><span>${a}</span><em>${b || ''}</em></div>`).join('');
  const stage = (inner, cls) => `<div class="u-stage ${cls || ''}">${inner}</div>`;
  const G = [
    ['hud', 'During a night', [
      ['night', 'Night counter and wave', 'In the game now: top left, NIGHT in small spaced letters, the number under it, the wave and the dead left under that. No moon.', () => stage(night(12, 3, 17) + comp())],
      ['bars', 'Health, magicka, stamina, shout', 'Bottom centre: three thin bars with no frames; the shout card beside them with its cooldown ring.', () => stage(bars() + `<div class="u-shout"><b>FUS</b><i style="--v:.35"></i></div>`)],
      ['perks', 'Perks (brews)', 'Your brews as small runes in a row above the bars. (The ultimate meter is gone with the ultimates.)', () => stage(perks() + bars())],
      ['compass', 'Compass', 'Top centre: a thin line, letters, quest diamonds, the altars and the dead as dots.', () => stage(comp())],
      ['quests', 'Quests and trials', 'In the game now: under the night counter, smaller - the trial (its task, the count and its clock) and the quests you track.', () => stage(night(4, 1, 9) + `<div class="u-quests sm"><b>LAY THE RESTLESS TO REST</b><span>Slay the risen in the altar\'s light  7 / 15  0:42</span><i style="--v:.47"></i><b>THE GOLDEN CLAW</b><span>Retrieve the claw</span></div>`)],
      ['boss', 'Boss bar', 'Top centre under the compass: the name in capitals, a long thin bar; red edge when it is enraged.', () => stage(comp() + `<div class="u-boss"><b>GRAHZAH, ALDUIN'S GRAVE-WING</b><i style="--v:.62"></i></div>`)],
      ['foehp', 'Health over the dead', 'Small bars over the wounded near you; a name over the great ones.', () => stage(`<div class="u-foe" style="left:38%;top:44%"><i style="--v:.4"></i></div><div class="u-foe big" style="left:56%;top:30%"><b>Minotaur Lord</b><i style="--v:.8"></i></div>`)],
      ['essence', 'Cursed Essence and kills', 'Kept as in the game: bottom left, the burning essence with CURSED ESSENCE over the count; +10 where a kill falls.', () => stage(`<div class="u-ess left"><small>CURSED ESSENCE</small><b>12,480</b><span class="u-pop">+40 MULTI-KILL</span></div><span class="u-kill" style="left:46%;top:46%">+10</span>`)],
      ['hit', 'Getting hit', 'In the game now: the red arc toward a blow you can\'t see at half size, a red edge after a hit.', () => stage(`<div class="u-vig"></div><div class="u-arc half"></div>`)],
      ['noise', 'Noise and stealth', 'No noise meter (your note); the sneak icons as they are in the game.', () => stage(`<div class="u-eye"><i style="--v:.3"></i></div>`)],
      ['marker', 'Quest marker and prompts', 'Our objective marker with its distance; the Activate prompt for things of ours (an orb, the Tablet).', () => stage(`<div class="u-mark" style="left:62%;top:38%">&#9670;<small>84 m</small></div><div class="u-prompt"><kbd>E</kbd> Roll the Tablet <small>2,000</small></div>`)],
      ['between', 'Between nights', 'In the game now: a 10 s countdown only before a night (none between waves); the last three of a wave still marked at the screen\'s edge.', () => stage(`<div class="u-center"><small>THE NIGHT BEGINS IN</small><b>10</b></div><div class="u-last">&#9664; 3</div>`)],
      ['weapon', 'Weapon counters', 'The donated weapon\'s uses, the Soulcaller\'s souls, the Tablet hold: small rings beside the bars.', () => stage(bars() + `<div class="u-rings"><span><i style="--v:.66"></i>Uses</span><span><i style="--v:.4"></i>Souls 4/10</span></div>`)],
    ]],
    ['pop', 'Pop-ups and words', [
      ['banner', 'Night and wave banners', 'In the game now: no banner across the screen - the night counter says it.', () => stage(night(12, 1, 24))],
      ['slain', 'Dragon Priest slain', 'The relic card: the priest\'s name, the relic received, a small dragon-script line.', () => stage(`<div class="u-card"><small>&#x16DF; DRAGON PRIEST SLAIN &#x16DF;</small><b>Vahlok the Jailor</b><span>Jailor's Keyhammer received</span></div>`)],
      ['lore', 'Weapon cards', 'Removed (your Nov 26 note): drawing a weapon shows nothing.', () => stage(`<div class="u-ess left"><small>CURSED ESSENCE</small><b>12,480</b></div>`)],
      ['hints', 'Hints', 'In the game now: no hints (your note) - the weapon cards say what each does.', () => stage('')],
      ['subs', 'Subtitles', 'Centred, Jost, with the speaker\'s name in small capitals; the altar voice\'s in gold.', () => stage(`<div class="u-subs"><small>FELLDIR THE OLD</small>The city is yours again. Ask, and I will answer.</div>`)],
      ['death', 'Falling (the BO3 death)', 'The screen drains to grey; YOU FELL, the night you reached, and the bar to rise.', () => stage(`<div class="u-death"><b>YOU FELL</b><span>Night 12 &middot; 431 of the dead</span><i style="--v:.55"></i><small>HOLD TO RISE</small></div>`, 'grey')],
      ['msg', 'Message boxes', 'Skyrim\'s message boxes (the Tester\'s Kit, choices): a dark glass panel, buttons as thin outlined rows.', () => stage(`<div class="u-msg"><b>Tester's Kit</b>${['Weapons', 'The night', 'Stay alive', 'The dead', 'Close'].map(x => `<span>${x}</span>`).join('')}</div>`)],
    ]],
    ['altar', 'The Echo Altar', [
      ['altarmenu', 'The altar menu', 'Its sections down the left; the one open on the right. Gold rule under the city\'s name.', () => stage(`<div class="u-split">${panel('WHITERUN &middot; ECHO ALTAR', rows([['Your Kit', '', 1], ['Armory'], ['Perks'], ["Thu'um and dragon"], ['Trials'], ['Talk']]), '34%')}${panel('YOUR KIT', rows([['Radiant Quarterstaff', 'Tier II'], ['Arcane Conjurer Robes', 'Tier I'], ['Spells', '6'], ['Upgrade to Tier III', '8,000', 1]]), '56%')}</div>`)],
      ['armory', 'Armory and upgrades', 'Each item, its tier pips, the cost on the right; locked ones faint.', () => stage(panel('ARMORY', rows([['Fire Flask', '&#9679;&#9679;&#9675;&#9675;'], ['Frost Flask', '&#9679;&#9675;&#9675;&#9675;'], ['Shock Flask', 'locked']]), '60%'))],
      ['perkmenu', 'Perks and blessings', 'The night\'s blessings as runes in a row, what each does below.', () => stage(panel('BLESSINGS', `<div class="u-perks in">${['&#x16A0;', '&#x16B1;', '&#x16C9;', '&#x16DF;', '&#x16D2;'].map(r => `<span>${r}</span>`).join('')}</div><p class="u-p">Stone-skin: the dead hit 15% softer. 1,500</p>`, '60%'))],
      ['thuum', "Thu'um and dragon", 'The city\'s shout, its three words lighting as you learn them; your glider dragon\'s bond.', () => stage(panel("THU'UM", `<div class="u-words"><b class="on">FUS</b><b class="on">RO</b><b>DAH</b></div><p class="u-p">Bond with your dragon: 2 / 3</p>`, '60%'))],
      ['trialsmenu', 'Trials', 'The four trials, the one to take next, why they\'re shut, tonight\'s state.', () => stage(panel('TRIALS', rows([['Lay the Restless to Rest', 'done'], ["The Stone's Whisper", 'next', 1], ['Hold the Light', ''], ['Break the Seals', '']]), '60%'))],
      ['talk', 'Talk and whispers', 'Felldir\'s four topics, each with how many you haven\'t asked; the hidden quest\'s whispers as cards.', () => stage(panel('TALK', rows([['The Curse', '2 new', 1], ['The Dragon Priests', '1 new'], ['The cities'], ['Felldir']]), '60%'))],
      ['tablet', 'Tablet of Mysteries', 'Hold to roll: the ring fills, the weapon\'s name and its uses come up.', () => stage(`<div class="u-center"><small>THE TABLET OF MYSTERIES</small><b style="font-size:22px">Stormtalons</b><span>Glass claws &middot; 40 uses</span></div>`)],
      ['classpick', 'Choosing your class', 'Four columns, each class\'s weapon, wonder and flasks or spells; the cost to change below.', () => stage(`<div class="u-classes">${['Summoner', 'Battle-mage', 'Alchemist', 'Spellsword'].map((c, i) => `<div class="${i === 2 ? 'on' : ''}"><b>${c}</b><span>${['Radiant Quarterstaff', 'Carian Wand', 'Shadowflame Scythe', 'Spellbound Rapier'][i]}</span></div>`).join('')}</div>`)],
      ['codex', 'The Codex', 'The book of the mod: chapters on the left, the page on the right, in Jost with a gold initial.', () => stage(`<div class="u-split">${panel('CODEX', rows([['I. The Curse'], ['II. The Nights', '', 1], ['III. The Classes'], ['IV. Waking an Altar']]), '34%')}${panel('THE NIGHTS', '<p class="u-p">Each night the dead come down red beams of light. Waves climb; the last of each wave is marked...</p>', '56%')}</div>`)],
      ['freed', 'Freed cities on the map', 'Gold altar icons on the world map; the Tablet\'s place; the city under the curse in red.', () => stage(`<div class="u-map"><span class="g" style="left:30%;top:40%">&#9670;</span><span class="g" style="left:55%;top:60%">&#9670;</span><span class="r" style="left:70%;top:30%">&#9670;</span></div>`, 'map')],
    ]],
    ['start', 'Starting the game', [
      ['boot', 'Launch screen', 'The Alduin painting, the title in thin capitals, a gold rule; PRESS ANY KEY.', () => stage(`<div class="u-center"><b style="letter-spacing:.3em">THE CURSE OF ALDUIN</b><i class="rule"></i><small>PRESS ANY KEY</small></div>`, 'title')],
      ['setup', 'New character', 'After the creator: your Helgen companion and Survival Mode, as two simple choices.', () => stage(panel('A NEW LIFE', rows([['Ralof', '', 1], ['Hadvar'], ['Survival Mode', 'On']]), '50%'))],
      ['loading', 'Loading screens', 'Full screen art, one line of lore at the bottom, a thin progress line.', () => stage(`<div class="u-load"><span>The souls Alduin holds are restless. Free a city and its dead sleep.</span><i></i></div>`, 'title')],
    ]],
    ['skyrim', "Skyrim's own menus, re-skinned", [
      ['inv', 'Inventory', 'Edge-style list: categories as thin tabs, items in rows, a large item view; our class items marked with a gold rune.', () => stage(`<div class="u-split">${panel('INVENTORY', rows([['Radiant Quarterstaff &#x16DF;', '', 1], ['Fire Flask (6)'], ['Arcane Conjurer Robes &#x16DF;'], ['Health Potion (3)']]), '44%')}<div class="u-item"><b>Radiant Quarterstaff</b><span>Tier II &middot; Damage 32</span></div></div>`)],
      ['magic', 'Magic', 'The same list, the school down the side; our spells by element with their colour dot.', () => stage(panel('MAGIC', rows([['<i class="d f"></i>Fireball', '', 1], ['<i class="d i"></i>Ice Storm'], ['<i class="d s"></i>Chain Lightning']]), '50%'))],
      ['map', 'World map', 'Skyrim\'s map (your fantasy paper maps) with our markers in our style.', () => stage(`<div class="u-map"><span class="g" style="left:40%;top:50%">&#9670;</span></div>`, 'map')],
      ['skills', 'Skills and perks', 'The constellations kept; the labels and the level bar in Jost.', () => stage(`<div class="u-center"><small>DESTRUCTION</small><b>54</b><i class="rule"></i></div>`, 'sky')],
      ['journal', 'Journal and settings', 'Thin tabs (Quests, Stats, System), dark glass, Jost; our MCM page in the same look.', () => stage(panel('JOURNAL', rows([['Quests', '', 1], ['Stats'], ['System']]), '40%'))],
      ['dialog', 'Dialogue', 'The speaker\'s name in small capitals, topics in a short right-aligned list.', () => stage(`<div class="u-dialog"><small>RALOF</small>${['What happened at Helgen?', 'Where to now?', 'Goodbye.'].map(x => `<span>${x}</span>`).join('')}</div>`)],
      ['barter', 'Barter, containers, crafting', 'Two lists side by side, gold at the top, the same rows.', () => stage(`<div class="u-split">${panel('YOU', rows([['Iron Sword', '25'], ['Fire Flask', '60', 1]]), '45%')}${panel('ADRIANNE', rows([['Steel Ingot', '20'], ['Leather', '10']]), '45%')}</div>`)],
      ['levelup', 'Level up and pause', 'Level up: three choices as large thin numbers; the pause menu as a short centred list.', () => stage(`<div class="u-classes three">${['Health', 'Magicka', 'Stamina'].map((c, i) => `<div class="${i === 1 ? 'on' : ''}"><b>${c}</b><span>+10</span></div>`).join('')}</div>`)],
      ['main', 'Main menu', 'Continue, New, Load, Settings, Quit: a centred column of thin capitals under the title.', () => stage(`<div class="u-center"><b style="letter-spacing:.3em">THE CURSE OF ALDUIN</b><i class="rule"></i>${['CONTINUE', 'NEW', 'LOAD', 'SETTINGS', 'QUIT'].map(x => `<small style="margin-top:4px">${x}</small>`).join('')}</div>`, 'title')],
    ]],
  ];
  return G;
})();
// where each stands now: in the game, kept as it is (his note), next (approved, being built), removed
const UI_STATE = { night: 'in', bars: 'kept', perks: 'kept', compass: 'next', quests: 'in', boss: 'next', foehp: 'next', essence: 'kept', hit: 'in', noise: 'kept',
  marker: 'next', between: 'in', weapon: 'next', banner: 'in', slain: 'next', lore: 'removed', hints: 'in', subs: 'kept', death: 'kept', msg: 'next', altarmenu: 'kept',
  armory: 'kept', perkmenu: 'kept', thuum: 'next', trialsmenu: 'kept', talk: 'kept', tablet: 'next', classpick: 'kept', codex: 'kept', freed: 'next', boot: 'kept',
  setup: 'kept', loading: 'in', inv: 'next', magic: 'next', map: 'next', skills: 'next', journal: 'next', dialog: 'next', barter: 'next', levelup: 'kept', main: 'kept' };
