# Undying Nights — state and next steps (2026-10-07)

## Working (verified in game)
- Zombies visible (skinconv now writes per-vertex SSE weights); torso fix (continuous trunk remap) built, needs a close look.
- Wands in hand with HUD (name, ability line, infinity), tomes/grimoires on magicka, 19 signature cast sounds.
- Wand bolts: spells load their effects since the ESP group-order fix (MGEF before ENCH/SPEL). `bolt:` log shows real projectiles
  launching from the wand tip; 10 of 12 bolts fly on Skyrim's own spell projectiles. Still to confirm visually.
- One round per night, tally counter, 30-min day cycle (60/40), dawn despawn, night truce for NPCs, sprint OK on fresh character.
- Station stand with sloped walkable skirt, map markers, clutter cleared from the footprint.

## Open issues (in priority order)
1. Falkreath stand still floats: adoption keeps it ("alley already built"). Add a dev command that forces a town rebuild
   (Sweep + plan reset) and log GroundTop/low/coffer z on adoption to see why the 30-unit / slope checks pass.
2. Confirm bolts are visible (screenshot right after a real click); Nightshade + Bloodthirst use our own PROJ (no vanilla match).
3. Wand staff cast: ENCH is fire-and-forget aimed with the bolt effect; check the animation now that the enchant has effects.
4. Test loop: launch Skyrim ONLY when it isn't running (computer_open_application on a running game starts a second
   instance and truncates the log). Quit with test-channel `console qqq` in game, or Main menu > Quit by keyboard.
   steam_appid.txt (489830) in the game folder lets SkyrimSE.exe start directly.
5. Cmd file writes sometimes fail (ENOENT) while the game deletes it: write, then retry once.

## Roadmap Noah asked for
- Multiplayer (4-player co-op): build on Skyrim Together Reborn (STR) rather than custom netcode. Host-authoritative
  Director: only the host spawns undead and runs rounds; points/perks/slots per player synced through STR's script
  events or a small side channel; Coffer/Brew/Reforge purchases validated by the host.
- Apex-style movement polish: slide momentum curve, slide-hop, mantle onto ledges, wall-bounce, air strafe tuning.
- Creative content: special night events (blood moon, fog night), traps on the stand, Pack-a-Punch-style wand
  alt-fires at Reforge level 3, easter-egg quest per town, more elites, boss arena at Bleak Falls.
