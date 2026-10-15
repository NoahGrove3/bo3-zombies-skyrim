# The Curse of Alduin: audit and plan for the big pass (2026-10-11)

This audit covers Spec Section 0. It is the baseline for Sections 12 (audio), 13 (main menu) and 0.6 (cleanup), plus the new
enemy models and the move to a Nexus Mods release.

## 1. Architecture (as built)
- **Game**: Skyrim SE/AE 1.7.104 (Steam). Plugin `CurseOfAlduin.asi` (CommonLibSSE-NG, C++23, MSVC in GitHub Actions). It
  loads two ways:
  - through Ultimate ASI Loader (`dinput8.dll`) on a normal launch;
  - through SKSE as `Data/SKSE/Plugins/CurseOfAlduin.dll` when Skyrim Together Reborn starts the game. The second copy
    switches itself off if the first is already running.
- **Data plugin** `CurseOfAlduin.esp` (ESL-flagged) is generated from JSON sheets:
  `sheets/*.json` -> `tools/gen.py` (`src/Generated.h`) and `tools/gen_esp.py` (records). CI fails when either output is
  stale.
- **Runtime systems (src/)**:

  | File | What it runs |
  |---|---|
  | Director | nights, rounds, spawning, boss, elites, champions, events, power-ups |
  | Undead | per-type rigs and special attacks |
  | Gun | hand spells: tap, charge, unleash, blasts; shouts |
  | Shops | the shrine: roll, reforge, brews, Thu'um |
  | Curse | the city rites and the way-marker target |
  | Hud | ImGui over the game's Present: HUD, shrine menu, main menu |
  | Net / Relay / Coop | Steam lobby, state sync, Skyrim Together tunnel |
  | Forms | form lookup, sound playback |
  | Events | death, hit and menu sinks |
  | DevTools | file test channel |

- **Audio today**:
  - `sheets/sounds.json` holds 133 SNDR records. Most point at vanilla `Skyrim - Sounds.bsa` files, with pitch shift,
    a second "layer" sound and a static attenuation column.
  - Code calls `Forms::Play` (2D) and `Forms::PlayAt` (3D, follows a reference).
  - There are no stoppable handles, no loops, no per-cue rate limits, no positional one-shots at a point and no music
    control. The main menu plays Skyrim's own theme.
- **Main menu today**:
  - A full-screen ImGui picture (`assets/ui/menu_bg.png`) with drifting fog, embers, a pulsing moon and lightning.
  - Skyrim's buttons are luma-keyed back from a copy of the frame (lower-right corner only since a8852ef).
  - Our own cursor, plus the CO-OP PARTY panel.
- **Enemies today**:

  | Model | Built | Used by |
  |---|---|---|
  | `zombie.nif` | earlier pass | every zombie type (shambler, runner, wailer, bloater; crawler retired) |
  | `shadow.nif` | earlier pass | Gloomstalker |
  | `boss.nif` | earlier pass | Grimmvar |
  | `reaper.nif` | `tools/autorig.py` from a Tripo export | Iron Reaper |
  | `necro.nif` | `tools/autorig.py` from a Tripo export | Voidbound Necromancer |

  The champions and elites reuse these models. The zombie types differ only in body language (rig offsets), so they do
  not look different enough.

## 2. Assets supplied in this pass
- **The Curse of Alduin SFX**: 31 Epidemic Sound MP3s (2 music loops, 1 full track, 28 effects; several hold 3-7 takes each),
  plus a copy of the spec PDF. The inventory and assignments are in `docs/AUDIO_MAP.md`.
- **The Curse of Alduin Models**: 7 Sketchfab/Tripo exports.

  | Model | Format | Rigged? |
  |---|---|---|
  | stitched patchwork abomination | FBX ~60 MB + PBR jpgs | no |
  | corrupted king in black armour | FBX ~60 MB + PBR jpgs | no |
  | bone knight with greatsword | FBX ~60 MB + PBR jpgs | no |
  | skeletal lich king | FBX ~60 MB + PBR jpgs | no |
  | drowned hag | FBX ~60 MB + PBR jpgs | no |
  | hooded horror woman | GLB, 30k verts | yes, own armature, "RunFast" clip |
  | voidbound necromancer | GLB | already in the mod |

  All need re-rigging to Skyrim's skeleton. The pipeline that built the reaper and necromancer (`objexp`, `simplify`,
  `autorig.py`, `rigwrite`) handles this.

## 3. Bugs, conflicts and limits found
1. **Boss and champion abilities share three generic sounds** (`hound_night`, `rise`, `knife`). There is no telegraph
   distinct from impact, which breaks Spec 12.5.
2. **No sound handles**: nothing can loop or be stopped, so charge loops, beams, the ritual hum and boss music are
   impossible today (Spec 12.2 / 12.3).
3. **Spell audio stops at two stages** (cast, unleash) plus two shared charge cues. There is no impact at the bolt's
   landing and no difference between an early and a full release (Spec 12.3).
4. **Our undead have no voice of their own.** They borrow the draugr race's vanilla sounds, so they don't meet Spec 12.4.
5. **Main menu music is Skyrim's theme**, which Spec 12.2 / 13.6 rule out.
6. **Licensing for a Nexus release**:
   - Epidemic Sound files are licensed to the subscriber for their own content. Shipping the raw files in a public mod
     is likely not covered (open question to Noah).
   - Sketchfab model licences are unknown.
   - Mitigation: the audio sheet gets a vanilla `fallback` column, and `gen_esp.py LICENSED=0` builds a Nexus plugin
     that uses only Skyrim's own sounds. Unconfirmed models stay out of the Nexus package.
7. **Co-op audio**:
   - Sound is played locally on each machine from local events. The host relays night, event, boss and power-up
     state, and the client plays the matching cues from that state.
   - Nothing sound-specific goes over the network, and that will stay so (Spec 12.10).
8. **Testing limits**:
   - In-game testing runs on Noah's PC through the DevTools channel and computer use.
   - A true two-player co-op test needs a second person.

## 4. Plan (order = dependencies)
1. **Audio framework**
   - `src/Audio.cpp`: one-shots at a reference or a point, keyed loops with Stop/StopAll, per-cue cooldowns and voice
     caps, and cleanup of loops whose owner died or unloaded.
   - `tools/sfx.py` cuts, trims, normalises and converts the supplied files into `assets/sound/un/*.wav`, driven by
     `sheets/sfx.json`.
   - New SNDR rows reference those files.
2. **Spell audio**: per spell, charge start, charge loop, full charge, early release, full release, travel and impact,
   built from the supplied files where they fit and layered vanilla elsewhere.
3. **Enemy, boss, champion and elite audio**: idle, aggro, attack wind-up and hit, hurt, death and specials, driven from
   Undead/Director state, the hit and death sinks and animation events. Each boss ability gets a distinct telegraph and
   impact.
4. **Shrine, coffer, rite (ward/hold circle) and special-night audio**, plus boss music and the curse-lifted stinger.
5. **New enemy models**:

   | Model | Becomes |
   |---|---|
   | hooded woman | Runner |
   | drowned hag | Wailer |
   | abomination | Bloater |
   | bone knight | elite / Night of Iron champion |
   | corrupted king | city champion |
   | lich king | necromancer champion |

   Each gets its own audio profile.
6. **Main menu**:
   - New layered art: background, mid ruins, foreground silhouettes, parallax and fog.
   - Custom-styled buttons drawn by us over Skyrim's (which stay hidden but functional).
   - Fade-in and transitions.
   - Menu music through a MUSC/MUST override of the main-menu music type, so Skyrim's volume slider and
     fade-out-on-load still apply.
7. **Testing on Noah's PC** after each stage, checking logs and screenshots.
8. **Cleanup (0.6)**: retire the leftover models, textures, sounds and code paths (brew machine, forge, relic altar,
   staffs, bows, coffer chest, crawler); rebuild and retest; then the Nexus package (installer, README, credits,
   permissions).

## 5. Acceptance criteria (tested in game)
- **Audio**:
  - every supplied file is reviewed and either mapped or flagged;
  - each of the 18 spells has a distinct charge/release/impact set;
  - loops stop on release, death and unload;
  - no stacking or spam across a 40-zombie round;
  - the boss telegraphs are audible before the hit;
  - no missing-file errors.
- **Enemies**:
  - each new model loads, animates with Skyrim's skeleton without stretching, stands on the ground, dies and ragdolls;
  - no T-poses and no invisible meshes.
- **Menu**:
  - art and animation show from the first launch and after returning from a game;
  - all of Skyrim's options work by mouse, keyboard and pad;
  - the music starts once, fades on load and never plays in game;
  - no stray picture or particles after the menu closes;
  - Skyrim Together's main-menu entries are reachable.
- **Regression**: a night spawns, the shrine works, the rites advance, the shouts work, the HUD draws, the co-op lobby
  hosts.
- **Package**: a clean install from the Nexus zip on a vanilla 1.7.104 install loads the mod (log line "loading as").
