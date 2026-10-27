"""The Curse of Alduin in Skyrim's Mod Configuration menu (SkyUI + MCM Helper), and its Experience file.

    python tools/mcm.py <Data dir>

writes
    MCM/Config/CurseOfAlduin/config.json     the pages (MCM Helper format)
    MCM/Config/CurseOfAlduin/settings.ini    the defaults (MCM Helper keeps the player's own in MCM/Settings/CurseOfAlduin.ini)
    Scripts/CurseOfAlduin_MCM.pex            the page's script: a bare MCM_ConfigBase (assets/mcm, made by tools/pexrename.py)
    SKSE/Plugins/Experience/Actors/CurseOfAlduin.ini   the night's dead give a quarter of Experience's usual XP

The game reads the same ini (src/Settings.cpp); the quest that registers the page is in the plugin (tools/gen_esp.py).
Mods without a menu of their own (QuickLoot, Character Menu) get a page here; their files are written when the game
starts (src/Settings.cpp ApplyMods), so changes there count from the next launch.
"""
import json, pathlib, shutil, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'build' / 'Data')


def slider(id, text, help, lo, hi, step, fmt='{0}'):
    return {'id': id, 'text': text, 'type': 'slider', 'help': help, 'valueOptions': {'min': lo, 'max': hi, 'step': step, 'formatString': fmt, 'sourceType': 'ModSettingFloat'}}


def stepper(id, text, help, options):
    return {'id': id, 'text': text, 'type': 'stepper', 'help': help, 'valueOptions': {'options': options, 'sourceType': 'ModSettingInt'}}


def toggle(id, text, help):
    return {'id': id, 'text': text, 'type': 'toggle', 'help': help, 'valueOptions': {'sourceType': 'ModSettingBool'}}


def key(id, text, help):
    return {'id': id, 'text': text, 'type': 'keymap', 'help': help, 'valueOptions': {'sourceType': 'ModSettingInt'}}


def header(t):
    return {'text': t, 'type': 'header'}


def text(t, help=''):
    return {'text': t, 'type': 'text', 'help': help}


# (Noah: easier for new players - which mod changes what - and The Curse of Alduin first in the list) the pages run from a
# Start Here guide through the game's own settings to the other mods' pages; the "!" sorts it to the top of SkyUI's list
PAGES = [
    {'pageDisplayName': '1. Start Here', 'cursorFillMode': 'topToBottom', 'content': [
        header('Welcome to The Curse of Alduin'),
        text('Survive the nights: the dead rise in waves.', 'Every night brings more waves of undead. Clear the last wave and dawn breaks. Day lasts about 18 minutes.'),
        text('F1 opens the Codex (story + guide).', 'The Survivor\'s Guide explains the Echo Altar, trials, spells, bows, shouts, the dragon and the hidden quests.'),
        text('Esc > PARTY: lobby, stats, records.', 'In the Esc menu press P (Y / Triangle on a controller) for the Party window: host or join friends, see everyone\'s night and your records.'),
        text('Echo Altar: spells, bows, perks, trials.', 'Every city has an Echo Altar. Stand on its base to open it.'),
        header('The pages of this menu'),
        text('2. Gameplay - the night, difficulty'),
        text('3. View & Effects - field of view, blood, fog, flashes'),
        text('4. Controls - keys, controller icons, the dragon glider'),
        text('5. Other Mods - QuickLoot, Character Menu'),
        header('What the other menus in this list change'),
        text('True Directional Movement - camera, lock-on, moving direction'),
        text('TrueHUD - enemy health bars, floating damage'),
        text('TK Dodge - the dodge roll'),
        text('SkyParkour - climbing and vaulting'),
        text('Experience - how leveling works'),
        text('Frozen Electrocuted Combustion - death effects (burned, frozen, shocked bodies)'),
        text('Embers XD - fire and spell lighting'),
        text('Realistic Water Two - water'),
        text('Floating Subtitles - subtitles over speakers'),
        text('Follower Stats - your followers\' stats'),
        text('Local Map Upgrade / Fantasy Paper Maps - the map'),
        text('SkyUI - the menus themselves'),
    ], 'position': 0},
    {'pageDisplayName': '2. Gameplay', 'cursorFillMode': 'topToBottom', 'content': [
        header('The night'),
        toggle('bShelter:General', 'Townsfolk go indoors', 'From the moment the night\'s countdown starts, everyone out in a city\'s streets goes inside until dawn, so the dead can\'t hurt them.'),
        stepper('iMeleeSpeedStep:General', 'Melee attack speed', 'How fast swords, axes, maces and fists swing.', [f'{1 + i / 10:.1f}x' for i in range(11)]),
        stepper('iDrawSpeedStep:General', 'Bow and crossbow draw speed', 'How fast bows draw and crossbows reload.', [f'{1 + i / 10:.1f}x' for i in range(11)]),
        slider('fCorpseSeconds:General', 'Bodies fade after', 'Seconds a fallen undead lies there before it crumbles.', 3, 60, 1, '{0} s'),
        header('Difficulty'),
        toggle('bFollowSkyrim:Difficulty', 'Follow Skyrim\'s difficulty', 'Skyrim\'s difficulty (Settings > Gameplay) also sets how tough the night\'s dead are and how hard they hit: from Novice to Legendary, 0.6x to 1.8x their health and 0.5x to 2x their blows.'),
        slider('fUndeadHealth:Difficulty', 'Undead health', 'On top of Skyrim\'s difficulty: how much health the night\'s dead have.', 0.25, 3.0, 0.05, '{2}x'),
        slider('fUndeadDamage:Difficulty', 'Undead damage', 'On top of Skyrim\'s difficulty: how hard the night\'s dead hit you.', 0.25, 3.0, 0.05, '{2}x'),
    ]},
    {'pageDisplayName': '3. View & Effects', 'cursorFillMode': 'topToBottom', 'content': [
        header('View'),
        stepper('iFovFirstStep:General', 'First-person field of view', 'How wide you see in first person (degrees). Left / right to step it.', [str(60 + 5 * i) for i in range(15)]),
        stepper('iFovThirdStep:General', 'Third-person field of view', 'How wide you see in third person (degrees). Left / right to step it.', [str(60 + 5 * i) for i in range(15)]),
        slider('fBrightness:General', 'Brightness', 'How bright the world is (1.00 = as Skyrim made it).', 0.5, 2.0, 0.05, '{2}'),
        slider('fAdsSensitivity:General', 'ADS sensitivity', 'How fast you look around while aiming down sights, against Skyrim\'s own look sensitivity (1.00 = the same).', 0.1, 2.0, 0.05, '{2}'),
        header('Effects'),
        toggle('bBlood:Effects', 'Blood', 'Headshot kills burst the head in blood, and bodies bleed. Off: kills stay clean.'),
        slider('fFogStrength:Effects', 'Night fog', 'How thick the night\'s fog grows toward the last wave (and on a Blood Fog night). 0 = none.', 0.0, 1.5, 0.05, '{2}x'),
        toggle('bScreenShake:Effects', 'Screen shake', 'The camera shakes with the undead rising, big impacts and the mini-boss\'s arrival.'),
        toggle('bReduceFlashes:Effects', 'Reduce flashes', 'Softer lightning and fewer white flashes (headshots, the sky at wave starts).'),
    ]},
    {'pageDisplayName': '4. Controls', 'cursorFillMode': 'topToBottom', 'content': [
        header('Controller'),
        toggle('bPlayStationIcons:Controls', 'PlayStation button icons', 'Show the PlayStation buttons (Cross, Circle, Square, Triangle, L1/R1, L2/R2) in the mod\'s prompts instead of the Xbox ones. Skyrim sees any controller as an Xbox pad, so this is your choice.'),
        header('Echo Altar keys'),
        key('iShrineRoll:Keys', 'Roll the Coffer', 'Spin the shrine for a spell (or take the one it offers).'),
        key('iShrineReforge:Keys', 'Reforge', 'Reforge the spell in your hand.'),
        key('iShrineBless:Keys', 'Take a perk', 'Buy the chosen perk.'),
        key('iShrineShout:Keys', 'Learn the Thu\'um', 'Learn a freed city\'s shout.'),
        key('iShrineLeave:Keys', 'Leave the shrine', 'Close the shrine\'s menu.'),
        key('iShrineBond:Keys', 'Bond the dragon', 'Bind your glider\'s dragon closer: its next bond.'),
        key('iShrineBow:Keys', 'Call a bow', 'Spin the altar\'s bow wheel for an elemental bow (or take the one it offers).'),
        text('Or move with W/S (D-pad) and select with Enter (A / Cross).'),
        header('Dragon glider'),
        toggle('bBankReverse:Dragon', 'Reverse the dragon\'s lean', 'Off: the dragon leans into its turns like a real flyer (turning left, its left wing dips). On: it leans the other way (turning left, its right wing dips).'),
        toggle('bInvertDive:Dragon', 'Invert dive / pull up', 'Off: forward (W / left stick up) dives and back pulls up. On: forward pulls up and back dives, like an aircraft stick.'),
    ]},
    {'pageDisplayName': '6. Scout', 'cursorFillMode': 'topToBottom', 'content': [
        header('Laying out spawn points'),
        toggle('bScout:Scout', 'Scout mode', 'For marking where the dead climb out in each city (F10 turns it on and off too). On: you fly where you look and fast, nothing can hurt you, no undead rise, and you get a Hunting Bow and arrows - every arrow that comes to rest is saved as a spawn point for the city you are in. Off: back to normal.'),
    ]},
    {'pageDisplayName': '5. Other Mods', 'cursorFillMode': 'topToBottom', 'content': [
        text('These mods have no menu of their own.', 'Changes here are written to their files when the game starts: they count from the next launch.'),
        header('QuickLoot - looting bodies and chests'),
        toggle('bCloseInCombat:QuickLoot', 'Close in combat', 'Close the loot window when a fight starts.'),
        toggle('bCloseOnEmpty:QuickLoot', 'Close when empty', 'Close the loot window once the container is empty.'),
        slider('fScale:QuickLoot', 'Size', 'The loot window\'s size (%).', 50, 150, 5, '{0}%'),
        slider('fHorizontal:QuickLoot', 'Left / right', 'Moves the loot window by this share of the screen\'s width (%).', -50, 50, 1, '{0}%'),
        slider('fVertical:QuickLoot', 'Up / down', 'Moves the loot window by this share of the screen\'s height (%).', -50, 50, 1, '{0}%'),
        header('Character Menu - the character sheet'),
        key('iOpenMenu:CharacterMenu', 'Open the character sheet', 'The key that opens Character Menu SE.'),
        toggle('bBlur:CharacterMenu', 'Blur behind the sheet', 'Blur the world behind the character sheet.'),
    ]},
]
DEFAULTS = {
    'General': {'fFovFirst': 130.0, 'fFovThird': 120.0, 'iFovFirstStep': 14, 'iFovThirdStep': 12, 'fBrightness': 1.5, 'fAdsSensitivity': 1.0, 'bShelter': 1, 'fCorpseSeconds': 10.0, 'iMeleeSpeedStep': 3, 'iDrawSpeedStep': 0},
    'Controls': {'bPlayStationIcons': 0},
    'Dragon': {'bBankReverse': 0, 'bInvertDive': 0},
    'Scout': {'bScout': 0},
    'Effects': {'bBlood': 1, 'fFogStrength': 1.0, 'bScreenShake': 1, 'bReduceFlashes': 0},
    'Difficulty': {'bFollowSkyrim': 1, 'fUndeadHealth': 1.0, 'fUndeadDamage': 1.0},
    'Keys': {'iShrineRoll': 2, 'iShrineReforge': 3, 'iShrineBless': 4, 'iShrineShout': 5, 'iShrineLeave': 14, 'iShrineBond': 6, 'iShrineBow': 48},
    'QuickLoot': {'bCloseInCombat': 0, 'bCloseOnEmpty': 1, 'fScale': 100.0, 'fHorizontal': 20.0, 'fVertical': 0.0},
    'CharacterMenu': {'iOpenMenu': 49, 'bBlur': 1},
}
# Experience: XP a kill gives (a quarter of what the creatures give in the wilds)
XP = {'zombie': 1, 'hollow2': 1, 'hollow3': 1, 'ash1': 1, 'ash2': 1, 'runner': 1, 'marksman': 2, 'necro': 2, 'hexer_rime': 2,
      'hexer_storm': 2, 'wailer': 2, 'death_hound': 1, 'shadow': 4, 'reaper': 4, 'brute': 5, 'boss': 20, 'boss2': 20, 'boss3': 20,
      'hf_goblin': 1, 'hf_goblin_archer': 1, 'hf_centipede': 1, 'hf_arachling': 1, 'hf_goblin_shaman': 2, 'hf_harpy': 2, 'hf_imp_fire': 2,
      'hf_imp_shock': 2, 'hf_ooze': 2, 'hf_troll': 3, 'hf_ogre': 4, 'hf_minotaur': 5, 'hf_ettin': 5,
      'boss4': 20, 'herald': 25, 'skeleton': 0, 'king': 6, 'knight': 6, 'lich': 6, 'bloater': 6}


def main():
    cfg = OUT / 'MCM' / 'Config' / 'CurseOfAlduin'   # (MCM Helper names a mod's folder after its plugin)
    cfg.mkdir(parents=True, exist_ok=True)
    pages = [{k: v for k, v in p.items() if k != 'position'} for p in PAGES]
    (cfg / 'config.json').write_text(json.dumps({'modName': 'CurseOfAlduin', 'displayName': '! The Curse of Alduin', 'minMcmVersion': 9,
                                                 'cursorFillMode': 'topToBottom', 'pages': pages}, indent=2))
    ini = []
    for sec, vals in DEFAULTS.items():
        ini.append(f'[{sec}]')
        ini += [f'{k} = {v}' for k, v in vals.items()]
        ini.append('')
    (cfg / 'settings.ini').write_text('\n'.join(ini))
    (OUT / 'Scripts').mkdir(parents=True, exist_ok=True)
    shutil.copy(ROOT / 'assets' / 'mcm' / 'Scripts' / 'CurseOfAlduin_MCM.pex', OUT / 'Scripts' / 'CurseOfAlduin_MCM.pex')
    rows = json.load(open(ROOT / 'sheets' / 'enemies.json'))['rows']
    by = {}
    for r in rows:
        if r['plugin'] != 'CurseOfAlduin.esp' and r['id'] in XP:
            by.setdefault(r['plugin'], []).append(f"; {r['label']} ({r['id']})\n{int(r['localId'], 16):08X} = {XP[r['id']]}")
    exp = OUT / 'SKSE' / 'Plugins' / 'Experience' / 'Actors'
    exp.mkdir(parents=True, exist_ok=True)
    (exp / 'CurseOfAlduin.ini').write_text('; The Curse of Alduin: the night\'s dead give a quarter of the usual XP (tools/mcm.py)\n' +
                                           '\n'.join(f'[{p}]\n' + '\n'.join(v) + '\n' for p, v in by.items()))
    print('mcm: wrote', cfg, 'and', exp / 'CurseOfAlduin.ini')


main()
