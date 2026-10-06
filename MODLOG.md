# MODLOG

## Facts
- Skyrim SE at D:\SteamLibrary\steamapps\common\Skyrim Special Edition, SkyrimSE.exe 1.7.104.0. No SKSE installed.
- BO3 at C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Black Ops III (zone/*.xpak, 220 files).
- Melty: ultimate-asi-loader auto-installed; SKSE not auto for 1.7.104 -> .asi route (CommonLibSSE-NG d61bca4, as SkyCraft).
- Build: GitHub Actions windows-latest (no MSVC here; can't type in terminals on the PC).
- Address IDs used were checked against versionlib-1-7-104-0.bin with tools/check_ids.py (all present).
- Form IDs in sheets come from scanning the player's Skyrim.esm / Dawnguard.esm (tools/esmscan.py).

## Route
.asi plugin (CommonLibSSE-NG), thread from DllMain waits for UI, data-loaded on first menu event once
LookupByID(7) exists (Melty game_info note). Player Update vfunc 0xAD for ticks; Present vtable for HUD.

## Log
- 2026-10-06: sheets + preflight + generator; step 1 source written; first CI build next.
