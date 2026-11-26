// ================================================================ data: the build, the weapons, the abilities, the tests
const BUILD = { sha: '__SHA__', date: 'Nov 25, 2026', installed: '8cd53a1' };
const COLS = __COLS__;
const CNAME = { white: 'White', blue: 'Storm blue', iceblue: 'Ice blue', orange: 'Ember orange', red: 'Blood red', green: 'Grave green', poison: 'Venom green', drain: 'Drain violet', purple: 'Purple', moon: 'Moonlight', fear: 'Dread crimson', gold: 'Gold', ss_none: 'Rapier red', arcane: 'Arcane blue', shockv: 'Shock violet', shadow: 'Shadow violet', carian: 'Carian blue' };
const LOOKS = {};
const LOOK3 = __LOOK3__;
const SRC = {
  ours: ['Ours', '#d6b46a'], sk: ['Skyrim', '#9fb7c9'], dg: ['Dawnguard', '#b49ad8'], db: ['Dragonborn', '#86c7a6'],
  apoc: ['Apocalypse', '#e48a6a'], myst: ['Mysticism', '#e2c05a'], aa: ['Animated Armoury', '#a3a3a3'], ful: ['Fulcimentum', '#c6a36f'],
  lgs: ['Light Greatswords', '#bfbfbf'], bs: ['BattleScythe', '#c98f8f'],
};
const SCYTHES = ['priest_vokun', 'tab_silverscythe', 'tab_bonereaper', 'tab_wyrmtooth', 'tab_bloodmoon', 'cls_forsworn_alch', 'cls_falmer_alch', 'cls_dwarven_alch', 'cls_dragonpriest_alch'];

// ---- the weapon workshop: every weapon with a look (data: live/<key>.json)
const WCATS = __WCATS__;

// ---- the ability workshop: every ability's effect, now and ours
const STYLES = {
  burst: ['Burst', 'A burst of the element where it strikes: a flash, a ball of fire or light, sparks and a wisp of smoke.'],
  shockwave: ['Shockwave', 'A ring of force racing out over the ground, a low wall of light on its edge. Our remake of the Labyrinthian ring.'],
  nova: ['Nova', 'A ring that throws up shards and mist as it goes.'],
  pillar: ['Pillar', 'A beam of light from the sky onto them, bursting at its foot.'],
  strike: ['Lightning strike', 'A bolt from the sky that flickers down onto them.'],
  chain: ['Chain', 'Lightning leaping from one of the dead to the next.'],
  bolt: ['Bolt', 'A bolt flying from you to them, bursting on contact.'],
  meteor: ['Meteor', 'A fireball falling from the sky, a ring where it lands.'],
  cyclone: ['Cyclone', 'A whirling funnel that pulls in sparks and debris.'],
  cloud: ['Cloud', 'A lingering cloud round them.'],
  pool: ['Ground fire', 'The ground left burning in the element.'],
  drain: ['Drain', 'Streams of life drawn out of them and back to you.'],
  bind: ['Bind', 'A rune circle under them and chains of light holding them.'],
  freeze: ['Freeze', 'Ice closes over them in shards.'],
  toll: ['Toll', 'Rings of sound rolling out from a struck bell.'],
  wave: ['Wave', 'A wave of force rolling out ahead of you.'],
  summon: ['Summon', 'A portal opens on the ground and something rises from it.'],
  mark: ['Mark', 'A glowing sign hangs over them while it lasts (slowed, weakened, asleep, blinded).'],
  slash: ['Slash', 'An arc of light following the blade.'],
  bounce: ['Bounce', 'The javelin itself arcing from one of the dead to the next, its element streaming behind it.'],
};
// [id, item, category, [[ability id, trigger, what it does, what it uses now, sources, style, colour, size %, where]]]
// where: 'hit' (on the one struck), 'self' (round you), 'area' (among the dead), 'line' (ahead of you)
const ABIL = [
  ['Dragon Priest relics', 'priest', [
    ['vahlok', "Jailor's Keyhammer", [
      ['hit', 'Every hit', 'Shackles the one struck in place for 2 s.', 'Ours: Skyrim\'s paralyze impact in green, with Skyrim\'s metal sparks.' /* was: 'Our lightning-chain streaks; Skyrim\'s void impact' */, 'ours sk', 'summon', 'green', 100, 'hit'],   // (your pick: Summon, kept)
      ['heavy', 'Heavy', 'Spectral chains shoot out from the one struck, catch the dead within 6 m and haul them in together.', '', 'ours sk', 'chain', 'green', 100, 'area']]],   // (your pick: Drain, kept)
    ['nahkriin', 'Mistblade', [
      ['hit', 'Every hit', 'Blinds the one struck: slower, weaker blows.', '', 'sk', 'burst', 'white', 60, 'hit'],   // (your pick: Slash, 60%)
      ['heavy', 'Heavy', 'A fog where the dead wander lost.', 'Ours: Skyrim\'s waterfall mist in white, with Skyrim\'s illusion light burst in white.' /* was: 'Skyrim\'s shout-area burst' */, 'sk', 'cloud', 'white', 100, 'area']]],   // (your pick: Cloud, 100%)
    ['volsung', 'Windcutter', [
      ['light', 'Light', 'A gust that staggers the dead 8 m ahead.', '', 'sk', 'slash', 'white', 90, 'line'],
      ['heavy', 'Heavy', 'A 12 m cone that knocks them down.', '', 'sk', 'toll', 'blue', 150, 'line']]],
    ['zahkriisos', 'Glacier Greatsword', [
      ['hit', 'Every hit', 'Freezes the one struck until it dies.', 'Ours: Skyrim\'s frost impact, with Skyrim\'s icicle impact.' /* was: 'Skyrim\'s frost impact' */, 'sk', 'freeze', 'iceblue', 100, 'hit'],
      ['heavy', 'Heavy', 'Shatters every frozen one round you.', 'Ours: Skyrim\'s frost explosion, with the ice wraith\'s death burst; the Labyrinthian ring in ice-blue.' /* was: 'Skyrim\'s frost explosion' */, 'sk', 'nova', 'iceblue', 130, 'self']]],
    ['otar', 'Storm Blade', [
      ['hit', 'Every hit', 'Lightning leaps to 2 more of the dead.', 'Ours: Skyrim\'s shock impact.' /* was: 'Our lightning streaks; Skyrim\'s shock sound' */, 'ours sk', 'chain', 'blue', 100, 'hit'],
      ['heavy', 'Heavy', 'Lightning arcs to all the dead round you.', 'Ours: Skyrim\'s mass shock explosion, with Skyrim\'s shock explosion; the Labyrinthian ring in blue.' /* was: 'Our lightning streaks' */, 'ours', 'strike', 'blue', 120, 'area']]],
    ['rahgot', 'Sacrifice Knife', [
      ['hit', 'Every hit', 'Poisons the one struck; it spreads to the next.', 'Ours: Skyrim\'s poison spray.' /* was: 'Mysticism\'s poison explosion' */, 'myst', 'cloud', 'poison', 70, 'hit'],
      ['heavy', 'Heavy', 'Every poisoned one bursts.', 'Ours: Dragonborn\'s poison gas explosion.' /* was: 'Dragonborn\'s poison-gas explosion' */, 'db', 'burst', 'poison', 120, 'area']]],
    ['krosis', 'Grief Blade', [
      ['hit', 'Every hit', 'Heals you 1%.', 'Ours: Skyrim\'s absorb hit in red, with Dawnguard\'s vampire drain impact.' /* was: 'Skyrim\'s absorb impact' */, 'sk', 'drain', 'red', 80, 'hit'],
      ['heavy', 'Heavy', 'A wail that weakens the dead round you.', 'Ours: Skyrim\'s dark illusion burst in red; the Labyrinthian ring in red.' /* was: 'Skyrim\'s absorb impact' */, 'sk', 'toll', 'red', 120, 'self']]],
    ['hevnoraak', 'Sealbreaker', [
      ['hit', 'Every hit', 'Shatters the one struck\'s armour.', 'Ours: Skyrim\'s metal sparks, with Skyrim\'s green absorb impact.' /* was: 'Dawnguard\'s sun burst' */, 'dg', 'burst', 'green', 90, 'hit'],
      ['heavy', 'Heavy', 'A shockwave that staggers all round you.', 'Ours: Skyrim\'s dust burst, with Skyrim\'s green absorb impact; the Labyrinthian ring in green.' /* was: 'Skyrim\'s Labyrinthian ring' */, 'sk', 'shockwave', 'green', 120, 'self']]],
    ['vokun', "Nightcaller's Scythe", [
      ['hit', 'Every hit', 'Puts the one struck to sleep.', 'Ours: Skyrim\'s illusion impact in purple.' /* was: 'Skyrim\'s absorb impact' */, 'sk', 'bind', 'purple', 100, 'hit'],
      ['heavy', 'Heavy', 'A nightmare fog that slows them.', 'Ours: Skyrim\'s dark illusion burst in shadow, with Skyrim\'s waterfall mist in purple.' /* was: 'Skyrim\'s absorb impact' */, 'sk', 'cloud', 'shadow', 140, 'area']]],
    ['morokei', 'Arcane Maul', [
      ['hit', 'Every hit', 'Bursts with magic.', 'Ours: Skyrim\'s blue absorb impact in drain-blue.' /* was: 'Skyrim\'s absorb impact' */, 'sk', 'slash', 'drain', 100, 'hit'],
      ['heavy', 'Heavy', 'A magicka shockwave that refills your magicka.', 'Ours: the Staff of Magnus\'s impact; the Labyrinthian ring in drain-blue.' /* was: 'Skyrim\'s Labyrinthian ring' */, 'sk', 'shockwave', 'drain', 130, 'self']]],
  ]],
  ['Tablet of Mysteries', 'tablet', [
    ['stormtalons', 'Stormtalons', [
      ['third', 'Every 3rd hit', 'Chains lightning to 2 more of the dead.', 'Ours: Skyrim\'s shock impact.' /* was: 'Our lightning streaks; Apocalypse\'s discharge' */, 'ours apoc', 'chain', 'blue', 100, 'hit'],
      ['heavy', 'Heavy', 'Stuns.', 'Ours: the storm atronach\'s blast.' /* was: 'Apocalypse\'s discharge' */, 'apoc', 'strike', 'blue', 100, 'hit']]],
    ['gravebell', 'Gravebell Mace', [
      ['fifth', 'Every 5th hit', 'Tolls the bell, stunning the dead within 5 m.', 'Ours: Skyrim\'s turn-undead impact in gold; the Labyrinthian ring in gold.' /* was: 'Skyrim\'s Labyrinthian ring in gold, turn-undead explosion; our toll sound' */, 'sk ours', 'toll', 'gold', 120, 'self']]],
    ['talons', 'Talons of Woe', [
      ['hit', 'Every hit', 'Bleeding twice as deep; kills heal you a little.', 'Ours: Skyrim\'s blood mist, with Dawnguard\'s drain spray in blood-red.' /* was: 'Apocalypse\'s illusion burst painted blood red' */, 'apoc', 'drain', 'red', 80, 'hit']]],
    ['riverstone', 'Riverstone Halberd', [
      ['hit', 'Every hit', 'Knocks the dead back.', 'Ours: Skyrim\'s small dust burst.' /* was: 'Skyrim\'s giant-slam explosion' */, 'sk', 'burst', 'green', 90, 'hit'],
      ['heavy', 'Heavy', 'Splits the ground in a line ahead.', 'Ours: the giant\'s slam, with Skyrim\'s giant dirt explosion, with Skyrim\'s dust burst.' /* was: 'Skyrim\'s giant-slam explosions and ring' */, 'sk', 'wave', 'green', 140, 'line']]],
    ['wyrmspine_pike', 'Wyrmspine Pike', [
      ['hit', 'Thrust', 'Pierces 2 more of the dead in a line.', 'Ours: dragonfire\'s impact in fear-red.' /* was: 'Skyrim\'s dragonfire impact' */, 'sk', 'slash', 'fear', 100, 'line'],
      ['heavy', 'Heavy', 'A shockwave that staggers the dead within 8 m.', 'Ours: a dragon\'s landing; the Labyrinthian ring in fear-red.' /* was: 'Skyrim\'s Labyrinthian ring' */, 'sk', 'shockwave', 'fear', 90, 'self']]],
    ['lanternwick', 'Rimewick Quarterstaff', [
      ['light', 'Light', 'Freezes the dead for 3 s.', 'Ours: Skyrim\'s icicle impact.' /* was: 'Skyrim\'s frost impact' */, 'sk', 'freeze', 'iceblue', 90, 'hit'],
      ['heavy', 'Heavy', 'A 10 m blizzard for 6 s.', 'Ours: Skyrim\'s frost explosion.' /* was: 'Apocalypse\'s tornado with Skyrim\'s ice' */, 'apoc sk', 'cyclone', 'iceblue', 100, 'area']]],
    ['cinderclaws', 'Cinderclaws', [
      ['hit', 'Every swipe', 'Sets the dead burning.', 'Ours: the fireball\'s impact.' /* was: 'Skyrim\'s dragonfire impact' */, 'sk', 'burst', 'orange', 80, 'hit'],
      ['heavy', 'Heavy', 'A fan of embers.', 'Ours: dragonfire on the ground.' /* was: 'Skyrim\'s dragonfire impacts' */, 'sk', 'wave', 'orange', 120, 'line']]],
    ['eclipse', 'Eclipse Edge', [
      ['hit', 'Every hit', 'Frost and sun by turns.', 'Ours: Dawnguard\'s eclipse burst.' /* was: 'Skyrim\'s frost impact; Dawnguard\'s sun impact' */, 'sk dg', 'slash', 'orange', 100, 'hit'],
      ['heavy', 'Heavy', 'An ice shockwave, then a sun shockwave (10 m).', 'Ours: Dawnguard\'s sunburst, with Skyrim\'s frost explosion; the Labyrinthian ring in ice-blue.' /* was: 'Skyrim\'s Labyrinthian rings in frost and gold' */, 'sk', 'shockwave', 'iceblue', 100, 'self']]],
    ['wanderstar', 'The Wandering Star', [
      ['cast', 'Hold to cast', 'A gold star every 0.5 s that bounces to up to 4 of the dead.', 'Ours: Auriel\'s sky-bolt explosion.' /* was: 'Dawnguard\'s sunlight bolt in gold' */, 'dg', 'bolt', 'gold', 70, 'hit'],
      ['fifth', 'Every 5th hit', 'Throws them far.', 'Ours: Auriel\'s sky-bolt explosion.' /* was: 'Dawnguard\'s sun impact' */, 'dg', 'burst', 'gold', 110, 'hit']]],
    ['frostmourn', 'Frostmourn Rapier', [
      ['hit', 'Thrust', 'Slows the dead.', 'Ours: Skyrim\'s ice-weapon impact.' /* was: 'Skyrim\'s frost impact' */, 'sk', 'slash', 'iceblue', 90, 'hit'],
      ['heavy', 'Heavy', 'Freezes a ring of the dead round you.', 'Ours: Skyrim\'s frost explosion in ice-blue; the Labyrinthian ring in frost.' /* was: 'Skyrim\'s frost ring and impacts' */, 'sk', 'nova', 'iceblue', 140, 'self']]],
    ['silverscythe', 'Silver Reaping Scythe', [
      ['fourth', 'Every 4th hit', 'A silver flash that briefly slows the dead.', 'Ours: Skyrim\'s illusion light burst in moon-silver.' /* was: 'Dawnguard\'s sun-fire impact in gold' */, 'dg', 'bind', 'moon', 120, 'hit']]],
    ['bonereaper', 'Bonereaper', [
      ['kill', '1 kill in 4', 'Rises to fight for you for 20 s.', 'Ours: Skyrim\'s reanimate flash.' /* was: 'Skyrim\'s absorb impact' */, 'sk', 'summon', 'white', 100, 'hit']]],
    ['wyrmtooth', 'Wyrmtooth Scythe', [
      ['hit', 'Every hit', 'Makes the dead bleed.', 'Ours: Skyrim\'s blood spray.' /* was: 'Apocalypse\'s illusion burst painted red' */, 'apoc', 'slash', 'red', 80, 'hit'],
      ['heavy', 'Heavy', 'A shockwave that staggers the dead within 8 m.', 'Ours: a dragon\'s tail stomp; the Labyrinthian ring in red.' /* was: 'Skyrim\'s Labyrinthian ring' */, 'sk', 'shockwave', 'red', 150, 'self']]],
    ['bloodmoon', 'Bloodmoon Scythe', [
      ['hit', 'Every hit', 'Drains blood to heal you.', 'Ours: Dawnguard\'s drain.' /* was: 'Apocalypse\'s illusion burst painted red' */, 'apoc', 'drain', 'red', 90, 'hit'],
      ['heavy', 'Heavy', 'Drains everything it touches.', 'Ours: Dawnguard\'s blood explosion.' /* was: 'Apocalypse\'s illusion bursts painted red' */, 'apoc', 'drain', 'red', 140, 'area']]],
  ]],
  ['Wonders and Alduin\'s Bane', 'wonder', [
    ['javelin', 'Wyrmspine Javelin', [
      ['throw', 'Hold and let go', 'The throw: the javelin streaks out in your other hand\'s element.', 'Skyrim\'s bolt of your element behind it (Dawnguard\'s for sun)', 'sk dg', 'bolt', 'orange', 110, 'hit'],
      ['land', 'Where it lands', 'A burst and a ring, then it bounces on to the next.', 'Ours, by its element: dragonfire\'s impact (fire), Skyrim\'s icicle impact (frost), its shock explosion (shock), its small shout burst (wind), Dawnguard\'s sun-fire impact (sun), Apocalypse\'s arcane burst (arcane), its red absorb impact (void), its poison spray (poison).' /* was: 'Skyrim and Apocalypse element bursts; Skyrim\'s Labyrinthian ring' */, 'sk apoc', 'shockwave', 'orange', 110, 'hit'],
      ['bounce', 'Its bounce', 'The javelin itself arcs on, point first and slow enough to follow, to the next of the dead, your element streaming behind it; a flash and a burst at each it strikes.', 'The javelin in Skyrim\'s bolt of your element (Dawnguard\'s for sun); Skyrim\'s light flash' /* was: \'Skyrim\'s light flash\', chain */, 'sk dg', 'bounce', 'orange', 100, 'hit']]],
    ['soulcaller', "Soulcaller's Staff", [
      ['beam', 'Hold', 'The soul beam.', 'Skyrim\'s Staff of Magnus beam', 'sk', 'drain', 'arcane', 110, 'hit'],
      ['wolves', 'Its summons', 'Spectral wolves and a sabre cat come, each out of Skyrim\'s conjuring portal.', 'Skyrim\'s conjuring portal; Skyrim\'s wolf and sabre cat under our ghost glow', 'sk ours', 'summon', 'arcane', 100, 'area']]],
    ['mutagen', 'Mutagen Staff', [
      ['flasks', 'Hold block', 'A flask every 0.5 s from the staff: fire, frost, storm, acid, holy water, void in turn (no beam).', 'Ours: each flask\'s own impact (as the Alchemist\'s flasks below).' /* was: 'Our flasks\' bursts' */, 'ours', 'burst', 'gold', 100, 'hit'],
      ['elixir', 'The Elixir', 'A golden burst and a cloud that turns the dead.', 'Mysticism\'s sunbeam explosion in gold; Skyrim\'s poison spray in gold', 'myst sk', 'cloud', 'gold', 130, 'area']]],
    ['slayer', 'Dragon Slayer', [
      ['light', 'Light', 'A golden fireball.', 'Ours: dragonfire\'s impact in gold, with Auriel\'s sky-bolt explosion in gold.' /* was: 'Skyrim\'s fireball painted gold' */, 'sk', 'bolt', 'gold', 120, 'hit'],
      ['heavy', 'Heavy', '5 s of golden meteors.', 'Skyrim\'s fireball falling from the sky in gold', 'sk', 'meteor', 'gold', 130, 'area']]],
    ['bane', "Alduin's Bane", [
      ['light', 'Light', 'Slashes, and a meteor on each one hit.', 'Ours: the fireball\'s blast, with Alduin\'s own explosion, and the Labyrinthian ring in fire.' /* was: 'Skyrim\'s Unrelenting Force wave; Skyrim\'s fireball from the sky' */, 'sk', 'meteor', 'orange', 140, 'hit'],
      ['heavy', 'Heavy', 'A blast of wind round you (10 m) that hurls the dead far away.' /* was: \'A bright white tornado for 5 s that flings the dead away.\' */, 'Ours: Skyrim\'s shout burst in white; the Labyrinthian ring in white.' /* was: 'Skyrim\'s shout explosion, Labyrinthian ring in white; Apocalypse\'s tornado' */, 'sk apoc', 'cyclone', 'white', 170, 'area']]],
  ]],
  ['Class weapons', 'class', [
    ['scythe', 'Shadowflame Scythe', [
      ['light', 'Light', 'A violet bolt; the dead it strikes burn violet.', 'Ours: dragonfire\'s impact in violet, with Skyrim\'s dark illusion burst in shadow.' /* was: 'Skyrim\'s firebolt painted violet; Apocalypse\'s wizard-fire burst in violet' */, 'sk apoc', 'bolt', 'shadow', 100, 'hit'],
      ['heavy', 'Heavy', 'The violet fire tornado, leaving the ground burning.', 'Ours: Apocalypse\'s wizard fire in violet, with the College\'s fire pillar in violet.' /* was: 'Apocalypse\'s tornado and Skyrim\'s fire pillar, both violet' */, 'apoc sk', 'cyclone', 'shadow', 150, 'area']]],
    ['wand', 'Carian Wand', [
      ['light', 'Cast', 'Bolts cycling fire, lightning and ice.', 'Skyrim\'s firebolt, firebolt painted as shock, ice spike', 'sk', 'bolt', 'carian', 90, 'hit'],
      ['hit', 'Each one hit', 'A burst in the bolt\'s element.', 'Ours, by the bolt: the fireball\'s impact (fire), Skyrim\'s icicle shatter (ice), Skyrim\'s shock spark (lightning).' /* was: 'Skyrim\'s dragonfire and frost impacts; Apocalypse\'s discharge; Skyrim\'s ice-storm shatter' */, 'sk apoc', 'burst', 'carian', 90, 'hit'],
      ['heavy', 'Tier II–III', 'Bolts cycling fire, lightning and ice; every 2nd bolt a great one that pierces; at Tier III a volley either side of it.', 'Skyrim\'s Firebolt, Firebolt as shock and Ice Spike, each bolt\'s own impact' /* was: \'The storm cyclone\' */, 'sk', 'bolt', 'carian', 150, 'hit']]],
    ['staff', 'Radiant Quarterstaff', [
      ['light', 'Light', 'A white sun ball.', 'Ours: Dawnguard\'s sun-fire impact in white, with Dawnguard\'s sunburst.' /* was: 'Dawnguard\'s sunlight bolt, painted white' */, 'dg', 'bolt', 'white', 110, 'hit'],
      ['heavy', 'Heavy (Tier II–III)', 'A white sunbeam strikes out of the sky, with a ring.', 'Dawnguard\'s sunlight bolt, Mysticism\'s sunbeam explosion, Auriel\'s sunbeam and Dawnguard\'s sun-fire motes, all in white; the Labyrinthian ring in white', 'dg myst sk', 'pillar', 'white', 140, 'hit']]],
    ['rapier', 'Spellbound Rapier', [
      ['light', 'Light', 'A bolt in your other hand\'s element.', 'Ours, by your other hand: the fireball\'s impact (fire), Skyrim\'s icicle shatter (frost), Skyrim\'s shock spark (shock), Dawnguard\'s sun-fire impact (sun), Apocalypse\'s arcane burst (arcane).' /* was: 'Skyrim\'s firebolt, ice spike, shock; Dawnguard\'s sunlight; Apocalypse\'s arcane bolt' */, 'sk dg apoc', 'bolt', 'orange', 100, 'hit'],
      ['heavy', 'Heavy', 'A tornado that casts your spell.', 'Ours, by your other hand: dragonfire on the ground, Skyrim\'s icicle impact, its shock impact, Dawnguard\'s sun-fire impact, Apocalypse\'s arcane burst.' /* was: 'Apocalypse\'s tornado with Skyrim\'s pillar or ice, or Apocalypse\'s lightning strike' */, 'apoc sk', 'cyclone', 'orange', 140, 'area'],
      ['line', 'Heavy (frost or arcane)', 'A shockwave line of ice shards.', 'Skyrim\'s Ice Spike volley shards', 'sk', 'nova', 'iceblue', 110, 'line']]],
  ]],
  ['Class spells and flasks', 'spells', [
    ['dfire', 'Destruction: fire', [
      ['firebolt', 'Firebolt', 'Its impact on the dead it strikes.', '', 'ours sk', 'burst', 'orange', 60, 'hit'],
      ['fireball', 'Fireball', 'Its impact on the dead it strikes.', '', 'ours sk', 'burst', 'orange', 90, 'hit'],
      ['incinerate', 'Incinerate', 'Its impact on the dead it strikes.', '', 'ours sk', 'burst', 'orange', 80, 'hit'],
      ['immolate', 'Immolate', 'Its impact on the dead it strikes.', '', 'ours sk', 'pillar', 'orange', 70, 'hit']]],
    ['dfrost', 'Destruction: frost', [
      ['icespike', 'Ice Spike', 'Its impact on the dead it strikes.', '', 'ours sk', 'freeze', 'iceblue', 60, 'hit'],
      ['icestorm', 'Ice Storm', 'Its impact on the dead it strikes.', '', 'ours sk', 'cloud', 'iceblue', 70, 'hit'],
      ['icyspear', 'Icy Spear', 'Its impact on the dead it strikes.', '', 'ours sk', 'nova', 'iceblue', 70, 'hit'],
      ['throatoftheworld', 'Throat of the World', 'Its impact on the dead it strikes.', '', 'ours sk', 'nova', 'iceblue', 100, 'hit']]],
    ['dshock', 'Destruction: shock', [
      ['chainlightning', 'Chain Lightning', 'Its impact on the dead it strikes.', '', 'ours sk', 'chain', 'blue', 70, 'hit'],
      ['shocknova', 'Shock Nova', 'Its impact on the dead it strikes.', '', 'ours sk', 'shockwave', 'blue', 80, 'hit'],
      ['fingerofthemountain', 'Finger of the Mountain', 'Its impact on the dead it strikes.', '', 'ours sk', 'strike', 'blue', 80, 'hit']]],
    ['dsun', 'Destruction: sun', [
      ['solarflare', 'Solar Flare', 'Its impact on the dead it strikes.', '', 'ours sk', 'burst', 'gold', 70, 'hit'],
      ['sunbeam', 'Sunbeam', 'Its impact on the dead it strikes.', '', 'ours sk', 'mark', 'gold', 50, 'hit'],
      ['forbiddensun', 'Forbidden Sun', 'Its impact on the dead it strikes.', '', 'ours sk', 'pillar', 'gold', 80, 'hit'],
      ['daybreak', 'Daybreak', 'Its impact on the dead it strikes.', '', 'ours sk', 'shockwave', 'gold', 90, 'hit']]],
    ['darcane', 'Destruction: arcane', [
      ['arcanebolt', 'Arcane Bolt', 'Its impact on the dead it strikes.', '', 'ours sk', 'burst', 'arcane', 60, 'hit'],
      ['manavoid', 'Mana Void', 'Its impact on the dead it strikes.', '', 'ours sk', 'drain', 'arcane', 80, 'hit'],
      ['lunartide', 'Lunar Tide', 'Its impact on the dead it strikes.', '', 'ours sk', 'shockwave', 'moon', 80, 'hit'],
      ['apocalypse', 'Apocalypse', 'Its impact on the dead it strikes.', '', 'ours sk', 'meteor', 'arcane', 100, 'hit']]],
    ['dwalls', 'Destruction: walls', [
      ['wallofflames', 'Wall of Flames', 'Its impact on the dead it strikes.', '', 'ours sk', 'pool', 'orange', 40, 'hit'],
      ['walloffrost', 'Wall of Frost', 'Its impact on the dead it strikes.', '', 'ours sk', 'pool', 'iceblue', 40, 'hit'],
      ['wallofstorms', 'Wall of Storms', 'Its impact on the dead it strikes.', '', 'ours sk', 'pool', 'blue', 40, 'hit'],
      ['inferno', 'Inferno', 'Its impact on the dead it strikes.', '', 'ours sk', 'pool', 'orange', 50, 'hit']]],
    ['dstorms', 'Destruction: storms', [
      ['frostnova', 'Frost Nova', 'Its impact on the dead it strikes.', '', 'ours sk', 'nova', 'iceblue', 60, 'hit'],
      ['lightningstorm', 'Lightning Storm', 'Its impact on the dead it strikes.', '', 'ours sk', 'chain', 'blue', 50, 'hit'],
      ['multivortex', 'Multivortex', 'Its impact on the dead it strikes.', '', 'ours sk', 'cyclone', 'arcane', 50, 'hit'],
      ['lightningstrike', 'Lightning Strike', 'Its impact on the dead it strikes.', '', 'ours sk', 'strike', 'blue', 90, 'hit'],
      ['cinderstorm', 'Cinder Storm', 'Its impact on the dead it strikes.', '', 'ours sk', 'cloud', 'orange', 60, 'hit']]],
    ['tfire', 'Spellsword: fire touches', [
      ['burningtouch', 'Burning Touch', 'Its impact on the dead it strikes.', '', 'ours sk', 'slash', 'orange', 50, 'hit'],
      ['flametouch', 'Flame Touch', 'Its impact on the dead it strikes.', '', 'ours sk', 'slash', 'orange', 60, 'hit'],
      ['fierygrasp', 'Fiery Grasp', 'Its impact on the dead it strikes.', '', 'ours sk', 'burst', 'orange', 70, 'hit']]],
    ['tfrost', 'Spellsword: frost touches', [
      ['chillingtouch', 'Chilling Touch', 'Its impact on the dead it strikes.', '', 'ours sk', 'slash', 'iceblue', 50, 'hit'],
      ['icytouch', 'Icy Touch', 'Its impact on the dead it strikes.', '', 'ours sk', 'slash', 'iceblue', 60, 'hit'],
      ['freezinggrasp', 'Freezing Grasp', 'Its impact on the dead it strikes.', '', 'ours sk', 'freeze', 'iceblue', 70, 'hit']]],
    ['tshock', 'Spellsword: shock touches', [
      ['shockingtouch', 'Shocking Touch', 'Its impact on the dead it strikes.', '', 'ours sk', 'slash', 'blue', 50, 'hit'],
      ['statictouch', 'Static Touch', 'Its impact on the dead it strikes.', '', 'ours sk', 'slash', 'blue', 60, 'hit'],
      ['thunderinggrasp', 'Thundering Grasp', 'Its impact on the dead it strikes.', '', 'ours sk', 'chain', 'blue', 70, 'hit']]],
    ['tsun', 'Spellsword: sun touches', [
      ['sunlighttouch', 'Sunlight Touch', 'Its impact on the dead it strikes.', '', 'ours sk', 'slash', 'gold', 50, 'hit'],
      ['radianttouch', 'Radiant Touch', 'Its impact on the dead it strikes.', '', 'ours sk', 'slash', 'gold', 60, 'hit'],
      ['searinggrasp', 'Searing Grasp', 'Its impact on the dead it strikes.', '', 'ours sk', 'burst', 'gold', 70, 'hit']]],
    ['tarcane', 'Spellsword: arcane touches', [
      ['arcaneedge', 'Arcane Edge', 'Its impact on the dead it strikes.', '', 'ours sk', 'slash', 'arcane', 50, 'hit'],
      ['arcanebrand', 'Arcane Brand', 'Its impact on the dead it strikes.', '', 'ours sk', 'mark', 'arcane', 60, 'hit'],
      ['arcanecascade', 'Arcane Cascade', 'Its impact on the dead it strikes.', '', 'ours sk', 'burst', 'arcane', 70, 'hit']]],
    ['ffire', 'Alchemist: Fire Flask', [
      ['land', 'Where it breaks', 'A burst and the ground left burning.', 'Ours: the fireball\'s impact, with the oil-lamp trap\'s fire.' /* was: 'Skyrim\'s oil fire' */, 'sk', 'pool', 'orange', 110, 'hit'],
      ['power', 'Tier power', 'Firestorm: a ring of fire outward.', 'Skyrim\'s Fire Storm', 'sk', 'shockwave', 'orange', 150, 'hit']]],
    ['ffrost', 'Alchemist: Frost Flask', [
      ['land', 'Where it breaks', 'Ice on the ground that slows.', 'Ours: Skyrim\'s frost explosion.' /* was: 'Skyrim\'s ice hazard' */, 'sk', 'pool', 'iceblue', 110, 'hit'],
      ['power', 'Tier power', 'Frost Nova.', 'Apocalypse\'s frost nova', 'apoc', 'nova', 'iceblue', 140, 'hit']]],
    ['fshock', 'Alchemist: Shock Flask', [
      ['land', 'Where it breaks', 'A storm on the ground.', 'Ours: Skyrim\'s shock explosion.' /* was: 'Skyrim\'s storm hazard' */, 'sk', 'pool', 'blue', 110, 'hit'],
      ['power', 'Tier power', 'Lightning Strike.', 'Apocalypse\'s lightning strike', 'apoc', 'strike', 'blue', 130, 'hit']]],
    ['facid', 'Alchemist: Acid Flask', [
      ['land', 'Where it breaks', 'A pool of acid.', 'Ours: Dragonborn\'s poison gas explosion.' /* was: 'Skyrim\'s ice hazard painted green; Mysticism\'s poison explosion' */, 'sk myst', 'cloud', 'poison', 110, 'hit']]],
    ['fholy', 'Alchemist: Holy Water', [
      ['land', 'Where it breaks', 'Holy light that lingers; the dead burn in it.', 'Ours: Skyrim\'s turn-undead impact in gold, with Dawnguard\'s sun-fire impact in gold.' /* was: 'Skyrim\'s light and turn-undead hazards in gold' */, 'sk', 'pillar', 'gold', 110, 'hit']]],
    ['fvoid', 'Alchemist: Voidglass Flask', [
      ['land', 'Where it breaks', 'Mana Void: the dead dragged in; what it drains comes back to you.', 'Ours: Skyrim\'s red absorb impact, with Dawnguard\'s absorb explosion.' /* was: 'Apocalypse\'s mana void' */, 'apoc', 'drain', 'arcane', 120, 'hit'],
      ['power', 'Then', 'Lunar Tide: a ring that throws them back out.', 'Apocalypse\'s shadow implosion', 'apoc', 'shockwave', 'shadow', 140, 'hit']]],
    ['summon', 'Summoner: every conjuration', [
      ['arrive', 'A summon arrives', 'The portal your summons step out of.', 'Our conjurations (Phase 2): Skyrim\'s own conjure effects', 'ours sk', 'summon', 'arcane', 110, 'area']]],
  ]],
];

// ---- each ability's own effect, rebuilt from real parts (tools/realfx.py)
const AB_STYLES = {};
const AB_OWN = {};
const AB_PARTS = __AB_PARTS__;
const REALFX = __REALFX__;
const AB_PICKS = __AB_PICKS__;
const CARRY = __CARRY__;   // (your picks and marks from the last doc, carried here)
const SOUNDS = __SOUNDS__;   // (tools/soundopts.py: every weapon's and ability's sounds, five options each)
const FEEL = __FEEL__;
const TRIALS = __TRIALS__;
const DEATHS = __DEATHS__;
const PERF = __PERF__;
const DRAGONS = __DRAGONS__;   // (Diverse Dragons Collection: its 28 kinds, seven levels each - read from Noah's plugin)
const FOES = __FOES__;   // (the night's enemies: sheets/enemies.json)
const ISSUES = __ISSUES__;   // (your picks so far: docs/workshop/ability_picks.json)   // (tools/realfx.py: the themes' real parts, the styles, each card's theme - real.js puts them together)   // (tools/realfx.py: each card's real parts)
const SCY = __SCY__;   // (tools/scythes.py: our own scythes, for your OK)
// ---- the test list: what to do in game, what to look for
const TESTS = __TESTS__;
// ---- the decisions this build
const RECS = __RECS__;
// ---- the last few days, and the mod now
const RECENT = __RECENT__;
const MODNOW = __MODNOW__;
const MODNOW_ABOUT = __MODNOW_ABOUT__;
const NEXUS = __NEXUS__;
const CHANGES = __CHANGES__;
