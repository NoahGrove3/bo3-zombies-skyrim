# BO3 Zombies: Skyrim (working title)

Play Skyrim's main quest as a Black Ops III Zombies crew member. Every night the dead rise in rounds that
never stop getting harder; by day you quest as normal.

**Status: step 1 of 6, early test build.** Back up your saves.

## Step 1: what it does
- **Night rounds.** From 8 pm to 6 am on Skyrim's clock, undead (skeletons, draugr, and later archers and
  warlocks) rise around you in BO3 Zombies rounds: BO3's zombie count and health curve, scaled to Skyrim.
  Every 6th round from round 5 is a **Death Hound round**. Clear a round to get the next one. Dawn cuts the
  round short: the undead collapse and that round starts again at nightfall.
- **Rounds start after Helgen** (once the intro quest "Unbound" is done).
- **Points.** 500 to start, +10 per gun hit, +50 per kill, +100 per headshot kill, +130 per kill with Skyrim
  weapons or spells. (Points become spendable in step 2.)
- **M1911.** Press **G** to take out the pistol and again to go back to Skyrim weapons, magic and shouts.
  Left click fires, **R** reloads. 8-round clip, 80 in reserve. Headshots do double damage. No gun model yet:
  crosshair, hit markers and sound only, until the BO3 converter (step 5).
- **BO3 HUD.** Round tallies, points with +popups, ammo, crosshair and hit markers.

Round, points and ammo are stored in your save.

## Requirements
- The Elder Scrolls V: Skyrim Special Edition / Anniversary Edition **1.7.104** (current Steam version),
  with Dawnguard (included in SE/AE).
- Ultimate ASI Loader (Melty installs it).
- Call of Duty: Black Ops III will be needed from step 5 (its guns, characters and sounds are converted
  from your own copy; none of BO3's files are included).

## Credits
- Address Library for SKSE Plugins by meh321 (bundled `versionlib-1-7-104-0.bin`, with permission for free mods).
- CommonLibSSE-NG (GPL-3.0 with modding exceptions), Dear ImGui (MIT), spdlog (MIT).
- Present-hook approach from SkyCraft by chasmlol (MIT).
- Built with Claude (AI). Fan project: not affiliated with Bethesda, Activision or Treyarch. You need to own the games.
