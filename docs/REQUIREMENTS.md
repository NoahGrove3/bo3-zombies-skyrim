# The Curse of Alduin: requirements checklist (spec + implied sections)

The build guide PDF spells out Sections 0, 0.6, 12 and 13 in full. Sections 1-11 are only referenced by them, and Noah asked
for those to be built from scratch, so every requirement those four sections imply is listed here too.

Status columns:
- **Built**: in the code or data, with the commit it landed in.
- **In game**: seen working on Noah's PC (log and/or screenshot). Blank means not yet verified.

## A. Combat (implied by 12.3, 13.3)
| # | Requirement | Built | In game |
|---|---|---|---|
| A1 | 18 hand spells: tap bolt, hold to charge both hands, signature release | f5772ef, 980ac59 | earlier passes |
| A2 | Partial vs full charge: weaker early release (fizzle), full charge glows and announces itself | ce85e9f | |
| A3 | Beams/charge hums loop and stop on release, death, unload | ce85e9f (Audio loops) | |
| A4 | Explosive, piercing, homing bolts behave and sound per type (launch, travel, impact, burst) | ce85e9f | |
| A5 | Impact sounds only on real collision (not on click) | ce85e9f (hit sinks) | |
| A6 | Spell emblems (icons) on the HUD dock | bb3881e | earlier passes |
| A7 | Shrine (coffer) roll: gathers power, cycles spells over the crown, lands on the real pick | f04fc1c | earlier passes |

## B. Enemies (implied by 12.4, 12.5)
| # | Requirement | Built | In game |
|---|---|---|---|
| B1 | Replacement models: Hollow Bride (runner), Drowned Wailer, Stitched Abomination, Horned Bone Knight, Barrow King, Lich of the Dirge | ce85e9f | |
| B2 | 2-3 regular zombie variants that look and sound different | ce85e9f (runner/wailer/bloater + shambler) | |
| B3 | Per-type voices: idle, attack, hurt, death; dead undead stay silent | ce85e9f | |
| B4 | Enemy health bars (nearest wounded), named bars for champions/elites | 6a9c5ce | |
| B5 | Boss health bar (top of screen) | earlier | earlier passes |
| B6 | Grimmvar: telegraphed fire breath (in-breath, cone warning), claws, leap, quake, music | ce85e9f | |
| B7 | Champions: telegraph before chain/summon/scream, arrival stinger | ce85e9f | |

## C. Rites, shrine, nights (implied by 12.6-12.8)
| # | Requirement | Built | In game |
|---|---|---|---|
| C1 | Floating ritual circle with on-screen timer; timer resets when a zombie enters | ce85e9f (Curse ring) | |
| C2 | Rite start, step, reset, fail, completion sounds; hum loop stops when the rite ends | ce85e9f | |
| C3 | Special nights (Blood Moon, Fog, Ash Storm, Night of Iron): start cue, weather, tint | earlier + ce85e9f | |
| C4 | Bones rite: found tokens no longer make every piece re-place each frame | 6ddc1b9 | |

## D. Audio system (Section 12)
| # | Requirement | Built | In game |
|---|---|---|---|
| D1 | Every supplied file reviewed, mapped or flagged (docs/AUDIO_MAP.md) | 7dfa8cb | n/a |
| D2 | Only the supplied library: no Skyrim sound effects in our sounds | ce85e9f | startup log clean |
| D3 | Register/trigger/limit/stop framework (Audio.cpp: cooldowns, world budget, keyed loops) | ce85e9f | |
| D4 | 3D sounds at the right source, attenuation per sound | ce85e9f | |
| D5 | Co-op: sounds are played locally from synced state, nothing sent over the network | design | needs a second player |
| D6 | Known gap: footsteps and body impacts still come from the races' own (vanilla) sets; the library has no footsteps | documented | |

## E. Main menu (Section 13)
| # | Requirement | Built | In game |
|---|---|---|---|
| E1 | Realistic art made from scenes staged in the game itself (replaces the painted layers) | in progress | |
| E2 | Slow layered motion: fog, embers, flicker, glow, rare lightning, parallax | 2f239ef (to re-fit to new art) | painted version seen |
| E3 | Own button column, keys/pad/mouse, confirm dialogs | 1d7168c, 31e07c9 | seen (column, confirm) |
| E4 | Custom menu music via sMainMenuMusic; UI sounds ours | 2f239ef | log: music set |
| E5 | States: first launch, back from game, load, new game, quit | | |

## F. New in this pass (Noah's requests)
| # | Requirement | Built | In game |
|---|---|---|---|
| F1 | Intro film after a new character is created (paused world, music, skippable) | d647332 | |
| F2 | Lore book rewritten for the new systems | bb346f4 | |
| F3 | Loading screen / cover reworked to match the new art | pending | |

## G. Cleanup (Section 0.6)
Runs after the build above is verified. Candidates are retired objects (brew machine, forge, relic altar, staffs, bows,
coffer chest, crawler), retired weapon sounds, the painted menu layers, and `assets/models` sources that are no longer
built.
