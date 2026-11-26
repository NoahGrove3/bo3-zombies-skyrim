# The test build (Noah, Nov 26: "next I want to play in the actual game and test everything")

## What goes in (all of the workshop doc's picks, one build)
- **Looks**: the 34 weapon looks as marked (Krosis o2 red, Volsung o1 white, Cinderclaws o3 orange, Silver Reaping o2 moon; the rest as they are).
- **Scythes**: all nine OK'd, the Dragon Priest's blade from the staff's head.
- **Abilities**: the 58 saved picks (option, colour, size, brightness, area, speed, linger, density, light) written into fxmap; area changes the reach in game.
- **Sounds** (docs/workshop/sound_picks.json): every weapon and ability sound at 20%. Skyrim's own swing, draw and hit sounds at 20% for every weapon in the game. The flasks at 20% too, and the Mutagen Staff's flasks and elixir use the Alchemist flasks' sounds.
- **Hit feel**: daggers, claws and rapiers none; swords and maces 18/45 ms stop, 15% shake on heavies; greatswords, axes, hammers, scythes and halberds 25/60 ms, 22%; wands, staffs and flasks none.
  Hit marker and headshot marker off; kill sound kept; slow motion on the last kill of the night.
- **Deaths**: as now, except a plain heavy that kills throws the body.
- **Trials**: Restless 8 m, 15/20/25/30/35/40; Whisper 5 m; Hold the Light 20 m, blue ring and souls; seals 460. Sounds: start Ethereal call, step as now, freed Souls set free; soul Ghost wail, rise Ethereal call; inside Circle of Protection, slip Subsonic; seal Glass jaw; place found Ethereal call.
- **Big nights**: the stutters hunted first (Classes 85, Gun 75, Director 59, Shops 42 ms); bodies 12; fewer particles past 30 of the dead; light only from big effects; the rest as now.

## The Tester's Kit (a power, only when turned on)
- On when `Data/SKSE/Plugins/CurseOfAlduin_Test.ini` exists; delete the file and the power is gone (Nexus players never get it).
- Cast it for its menus:
  1. **Weapons**: pick a class, then a tier (Base, I, II, III, Ascended). You get that class's weapon, flasks and armour at that tier.
     You can also take every relic, every Tablet weapon, or the wonders (Javelin, Dragon Slayer, Alduin's Bane).
  2. **The night**: start the night now, or jump to round 5, 10, 15 or 20.
  3. **Stay alive**: god mode on/off, endless mana and stamina, +50,000 points.
  4. **The dead**: spawn 10 or 30 around you, or start any of the four trials at the nearest Echo Altar.

## A test route
1. Each class, Base to Ascended: draw, look, light and heavy, its sounds, its specials.
2. The relics, the Tablet weapons, the wonders: each one's hits and heavies on 10 of the dead.
3. Round 15 with 30 of the dead: hit feel, deaths, a big night (watch for stutters).
4. The four trials at one city.
5. Mark each line of the doc's Test list as you go.
