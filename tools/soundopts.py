"""(Noah, 2026-11-25: "add a sound effect tab for each weapon and the weapon's abilities so that we can properly audio engineer
all of the weapons and spells ... give me a preview of what it sounds like and your recommended options")

The sound workshop's data: every weapon's own sounds (draw, swing, heavy swing, hit) and every ability's (its cast, its impact,
what lingers), each with five options - the first what the game plays now, the second my recommendation - built only from
sounds already in Noah's install (Skyrim, Dawnguard, Dragonborn, Apocalypse, Mysticism: their SNDR records, read by the
device's sndr.py into build/sound/sndr.json) and our own (assets/sound/un).

    python tools/soundopts.py            -> docs/workshop/sounds.json and build/sound/want.txt (the files to cut)
"""
import json, re, pathlib, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import abilityopts as AOP

SN = json.load(open(ROOT / 'build/sound/sndr.json'))
BY = {s['ed']: s for s in SN['sndr'] if s['files']}
_IDX = {0: 'Skyrim.esm', 1: 'Update.esm', 2: 'Dawnguard.esm', 4: 'Dragonborn.esm'}


def _blasts():
    """the mod's named blasts (Generated.h kBlasts) -> the SNDRs their explosions play (sound1, sound2)"""
    txt = (ROOT / 'src/Generated.h').read_text()
    m = re.search(r'kBlasts\{ \{(.*?)\} \};', txt).group(1)
    S = {(s['src'], s['fid']): s for s in SN['sndr']}
    E = {(e['src'], e['fid']): e for e in SN['expl']}
    out = {}
    for name, fid in re.findall(r'\{ "(\w+)", (0x[0-9A-F]+) \}', m):
        f = int(fid, 16); e = E.get((_IDX.get(f >> 24), f & 0xFFFFFF))
        if not e: continue
        L = []
        for k in ('s1', 's2'):
            sid = e[k]
            if not sid: continue
            ms = e['m'] + [e['src']]
            if (sid >> 24) >= len(ms): continue
            s = S.get((ms[sid >> 24], sid & 0xFFFFFF))
            if s and s['files']: L.append(s['ed'])
        out[name] = L
    return out


BLAST = _blasts()
BLAST_NAME = {'fireball': "Skyrim's Fireball blast", 'frost': "Skyrim's frost blast (an icicle breaking)", 'runeshock': "Skyrim's Lightning Rune blast",
              'light': "Skyrim's small illusion blast (a firebolt's thud)", 'lightmassive': "Skyrim's mass illusion blast (the Firestorm's roar)",
              'poisongas': "Dragonborn's poison rune (the Serpent Stone's burst)", 'icevolley': "Skyrim's Ice Spike volley", 'shockmass': "Skyrim's mass shock blast",
              'bloodplague': "Dawnguard's blood plague (no sound of its own)"}

# ---------------------------------------------------------------- our own sounds (assets/sound/un), as 'coa:<stem>'
COA = {p.stem for p in (ROOT / 'assets/sound/un').glob('*.wav')}

# ---------------------------------------------------------------- impact pools by theme: (sound(s), name, what it is)
# a sound is an SNDR editor id, 'coa:<stem>', or a list of [ref, gain, delay] layers
IMPACT = {
    'fire': [('WB_DestructionFire_Sound_Impact_Fwoosh', 'Fwoosh', "Apocalypse's fire impact: a fat, quick whoosh of flame"),
             ('MAGDestructionFireballExplosion', 'Fireball', "Skyrim's Fireball: the full roar"),
             ('WB_DestructionWizardfire_Sound_Explosion_Emberblast', 'Ember blast', "Apocalypse's Wizardfire: a burst with crackling embers after"),
             ('MAGFireboltImpactSD', 'Firebolt thud', "Skyrim's Firebolt: short and dry"),
             ('WB_DestructionFire_Sound_Impact_Immolation', 'Immolation', "Apocalypse's Immolation: flames catching and roaring up"),
             ('NPCAtronachFlameDeathExplosion3D', 'Atronach burst', "a Flame Atronach bursting: a deep fiery pop"),
             ('WB_DestructionFire_Sound_Explosion_FlashFireball', 'Flash fireball', "Apocalypse's Flash Fireball: a sharp crack of fire"),
             ('MAGFlamesImpactSD', 'Flames', "Skyrim's Flames: a soft lick of fire")],
    'frost': [('WB_DestructionFrost_Sound_Explosion_Shatter', 'Shatter', "Apocalypse's frost shatter: ice bursting into shards"),
              ('MAGIcicleImpactBreakSD', 'Icicle break', "Skyrim's Ice Spike breaking (what the game plays now)"),
              ('WB_DestructionFrost_Sound_Impact_Crystalize', 'Crystalize', "Apocalypse: ice forming fast round the one struck"),
              ('FXExplosionIceSD', 'Ice burst', "Skyrim's ice explosion: a cold whump with glassy bits"),
              ('WB_DestructionFrost_Sound_Explosion_ShatteringCrystal', 'Shattering crystal', "Apocalypse: a high crystal ring as it breaks"),
              ('WB_DestructionFrost_Sound_Explosion_Cryoblast', 'Cryoblast', "Apocalypse's Cryoblast: a big frozen boom"),
              ('MAGFrostBiteImpactSD', 'Frostbite', "Skyrim's Frostbite: a hiss of cold"),
              ('NPCAtronachFrostAttackPowerImpactSD', 'Atronach slam', "a Frost Atronach's power blow: heavy ice on stone")],
    'shock': [('WB_DestructionShock_Sound_Explosion_LightningStrike_2C', 'Lightning strike', "Apocalypse: a real lightning crack and its roll"),
              ('MAGShockImpactSD', 'Spark hit', "Skyrim's Sparks/Lightning Bolt impact: a short electric snap"),
              ('WB_DestructionShock_Sound_Explosion_ShockNova', 'Shock nova', "Apocalypse's Shock Nova: a ring of crackling discharge"),
              ('WB_DestructionShock_Sound_Explosion_Thunderball', 'Thunderball', "Apocalypse: a ball of thunder bursting, heavy and round"),
              ('VOCShoutImpactStormCallNearSD', 'Storm Call', "the Storm Call shout's lightning landing near: huge"),
              ('WB_DestructionShock_Sound_Explosion_FingersOfTheMountain', 'Fingers of the Mountain', "Apocalypse: a sky-splitting bolt"),
              ('MAGRuneImpactSD', 'Rune', "Skyrim's rune going off: a crisp zap"),
              ('NPCSpiderHandExplosionShockNear', 'Shock pop', "Dragonborn: a small shock explosion")],
    'poison': [('MAG_PoisonImpactSFX', 'Venom splash', "Mysticism's poison impact: wet and acidic"),
               ('MAG_PoisonExplosionSFX', 'Poison burst', "Mysticism's poison explosion: a gassy pop with a hiss"),
               ('MAGPowerSerpentStoneExplosion', 'Serpent Stone', "Dragonborn's poison rune (what the game plays now)"),
               ('TRPHarvestBloomReleasePoison', 'Spore bloom', "a poison bloom trap puffing out its spores"),
               ('MAG_PoisonRitualExplosionSFX', 'Ritual burst', "Mysticism: a big, sickly bubbling burst"),
               ('NPCSpiderHandExplosionPoisonNear', 'Venom pop', "Dragonborn: a small poison explosion"),
               ('WB_AlterationChaos_Sound_Impact_Buzz', 'Buzz', "Apocalypse: a buzzing, insect-like crackle")],
    'sun': [('FXExplosionAurielsSunNear', "Auriel's sun", "Dawnguard: Auriel's Bow's sun blast, near"),
            ('MAGAurielShieldBlastA', "Auriel's shield", "Dawnguard: a bright, clean holy burst"),
            ('MAGRestorationTurnUndeadFire2DSD', 'Turn Undead', "Skyrim's Turn Undead: a ringing holy chord"),
            ('WB_AlterationAlt_Sound_Release_IgniteLight', 'Ignite light', "Apocalypse: light flaring up"),
            ('MAGDestructionStendarrsAuraFire2D', "Stendarr's Aura", "Dawnguard: a warm holy swell"),
            ('WB_RestorationHeal_Sound_Release_MassiveTwinkles', 'Twinkles', "Apocalypse: a shower of bright chimes"),
            ('FXExplosionAurielBowHarkon', "Auriel's Bow (Harkon)", "Dawnguard: the huge sun strike that ends Harkon"),
            ('WB_Restoration_Sound_Explosion_Report', 'Holy report', "Apocalypse: a short bright bang")],
    'blood': [('MAGAlterationVampireDrainOnHit', 'Vampiric drain', "Dawnguard's Vampiric Drain: a wet suck of life"),
              (['coa:flesh_hit_1', ['WB_RestorationBlood_Sound_Release_OrcishHealing', 0.6, 0.05]], 'Blood and gore', "our wet flesh hit with Apocalypse's blood magic under it"),
              ('WB_RestorationBlood_Sound_Release_Big', 'Blood rite', "Apocalypse's blood magic: a deep, thick pulse"),
              ('MAGAlterationVampireDrainRelease', 'Drain release', "Dawnguard: the drain let loose"),
              ('WB_RestorationTap_Sound_Release', 'Life tap', "Apocalypse: life tapped out of the body"),
              ('WB_RestorationCurse_Sound_Explosion_DoubleDischarge', 'Curse discharge', "Apocalypse: a dark double crack"),
              ('coa:flesh_death', 'The body gives way', "ours: the sound of an undead dying")],
    'earth': [('WB_AlterationEarth_Sound_Explosion_TalonsOfNirn', 'Talons of Nirn', "Apocalypse: rock erupting out of the ground"),
              ('MAGPowerSerpentStoneImpact', 'Stone impact', "Dragonborn: a stone thump"),
              ('WB_AlterationEarth_Sound_Impact', 'Earth impact', "Apocalypse: a heavy clod of earth"),
              ('WB_AlterationBaleful_Sound_Impact_Rumble', 'Rumble', "Apocalypse: a low rumble through the ground"),
              ('WPNImpactCrGiantClubVsDirt', "Giant's club", "a giant's club hitting the dirt: a huge thud"),
              ('WB_AlterationBaleful_Sound_Release_ArmorCrush', 'Armour crush', "Apocalypse: metal crumpling"),
              ('TRPRockfallReleaseSD', 'Rockfall', "a rockfall trap letting go"),
              ('WB_RestorationUndead_Sound_Explosion_DustToDust', 'Dust to dust', "Apocalypse: a dry burst of dust")],
    'wind': [('VOCShoutImpactPushSD', 'Unrelenting Force', "the Fus Ro Dah shout hitting: a wall of air"),
             ('WB_DestructionFrost_Sound_Explosion_HighWinds', 'High winds', "Apocalypse: a gale tearing through"),
             ('WB_AlterationTelekinesis_Sound_Explosion_Bwoom', 'Bwoom', "Apocalypse: a deep push of force"),
             ('VOCShoutImpactPushImpactDragonSD', 'Force on a dragon', "Unrelenting Force on something huge"),
             ('VOCShoutFXSprintFireSD', 'Whirlwind Sprint', "the Whirlwind Sprint shout: a rush of wind"),
             ('WB_Restoration_Sound_Explosion_LowSwish', 'Low swish', "Apocalypse: a soft low sweep of air"),
             ('VOCShoutImpactDisarm', 'Disarm', "the Disarm shout's impact: a sharp gust")],
    'spirit': [('WB_ConjurationGhosts_Sound_Impact_Wail', 'Ghost wail', "Apocalypse: a ghost's wail on the hit"),
               ('WB_ConjurationSpirit_Sound_Explosion_Whoom', 'Spirit whoom', "Apocalypse: a hollow spirit burst"),
               ('WB_ConjurationCommand_Sound_Explosion_EtherealCall', 'Ethereal call', "Apocalypse: a ringing call from beyond"),
               ('MAGConjureImpactSD', 'Conjure', "Skyrim's conjuring impact: a soft magical thump"),
               ('WB_RestorationNecro_Sound_Explosion_Undead', 'Necro burst', "Apocalypse: a dark necromantic burst"),
               ('WB_ConjurationGhosts_Sound_Explosion_Splash_1', 'Ghost splash', "Apocalypse: spirits splashing out"),
               ('QSTAlduinDeathSoulsASD', 'Souls', "the souls pouring out at Alduin's end"),
               ('WB_ConjurationBanish_Sound_Release_Hiss', 'Banish hiss', "Apocalypse: a hiss of banishing")],
    'shadow': [('WB_IllusionNightmare_Sound_Release', 'Nightmare', "Apocalypse's Nightmare: a dark, uneasy swell"),
               ('WB_IllusionShadow_Sound_Explosion_Vanish', 'Vanish', "Apocalypse: shadow folding in on itself"),
               ('WB_Illusion_Sound_Explosion_Subsonic', 'Subsonic', "Apocalypse: a deep throb felt more than heard"),
               ('WB_IllusionNightmare_Sound_Explosion_PaleMoon_Howl', 'Pale Moon howl', "Apocalypse: a distant, eerie howl"),
               ('MAGPowerMorasGraspImpactSD', "Mora's grasp", "Dragonborn: Hermaeus Mora's grasp landing"),
               ('WB_IllusionNightmare_Sound_Explosion_EnergyBall', 'Dream ball', "Apocalypse: a soft burst of dream energy"),
               ('WB_IllusionShadow_Sound_Explosion_Woing', 'Woing', "Apocalypse: a warped, bending shadow sound"),
               ('MAGIllusionReleaseAimedSD', 'Illusion', "Skyrim's illusion release")],
    'arcane': [('WB_DestructionCantrip_Sound_Explosion_ArcaneBurst', 'Arcane burst', "Apocalypse: a bright crackling burst of pure magic"),
               ('WB_AlterationChaos_Sound_Explosion_Plasma', 'Plasma', "Apocalypse: a hot, humming plasma pop"),
               ('WB_AlterationTelekinesis_Sound_Explosion_GlassJaw', 'Glass jaw', "Apocalypse: a crack of force like breaking glass"),
               ('MAGAetherialShieldImpact', 'Aetherial', "Dawnguard: the Aetherial Shield's ringing hit"),
               ('WB_Restoration_Sound_Explosion_SubsonicEnergy', 'Subsonic energy', "Apocalypse: a low pulse of energy"),
               ('MAGGenericProjectileImpactSD', 'Spell hit', "Skyrim's generic spell impact"),
               ('WB_ConjurationDetonate_Sound_Explosion_Blast', 'Detonate', "Apocalypse: a magical blast"),
               ('WB_DestructionGold_Sound_Release_MagicMissile', 'Magic missile', "Apocalypse: a zippy missile of magic")],
}
CAST = {   # a release: what the hand sounds like letting it go
    'fire': [('WB_DestructionFire_Sound_Release_Emberbolt', 'Emberbolt', "Apocalypse: an ember bolt leaving the hand"),
             ('MAGFireboltFire2DSD', 'Firebolt cast', "Skyrim's Firebolt leaving the hand"),
             ('WB_DestructionFire_Sound_Release_2', 'Fire release', "Apocalypse: a short throaty flame release"),
             ('MAGFirebolt03FireSD', 'Firebolt (heavy)', "Skyrim's heavier Firebolt cast"),
             ('WB_DestructionFire_Sound_Release_Meltdown', 'Meltdown', "Apocalypse: a big rolling fire release")],
    'frost': [('WB_DestructionFrost_Sound_Release_Fracture', 'Fracture', "Apocalypse: a cold cracking release"),
              ('MAGIcicleFireSD', 'Ice Spike cast', "Skyrim's Ice Spike leaving the hand"),
              ('MAGFrostbiteFire2DSD', 'Frostbite cast', "Skyrim's Frostbite"),
              ('WB_DestructionFrost_Release_1_Sound_Tiny', 'Tiny frost', "Apocalypse: a small icy flick")],
    'shock': [('WB_DestructionShock_Sound_Release_RiftBolt', 'Rift bolt', "Apocalypse: a crackling bolt let fly"),
              ('MAGShockFFFireSD', 'Lightning cast', "Skyrim's Lightning Bolt cast"),
              ('WB_DestructionShock_Sound_Release_JoyBuzzer_1', 'Joy buzzer', "Apocalypse: a quick buzzing zap"),
              ('WB_DestructionShock_Sound_Release_Lockdown', 'Lockdown', "Apocalypse: a heavy electric release")],
    'sun': [('MAGLightFireSD', 'Light cast', "Skyrim's Candlelight cast: a soft bright tone"),
            ('WB_AlterationAlt_Sound_Release_IgniteLight', 'Ignite light', "Apocalypse: light flaring up"),
            ('MAGRestorationFFFireSD', 'Healing cast', "Skyrim's restoration release: warm"),
            ('MAGRestorationTurnUndeadFire2DSD', 'Turn Undead cast', "a ringing holy chord")],
    'arcane': [('WB_DestructionGold_Sound_Release_MagicMissile', 'Magic missile', "Apocalypse: a zippy missile of magic"),
               ('MAGAlterationReleaseSD', 'Alteration cast', "Skyrim's alteration release: glassy"),
               ('WB_AlterationMetamagic_Sound_Release', 'Metamagic', "Apocalypse: a shimmering release"),
               ('MAGConjureFireSD', 'Conjure cast', "Skyrim's conjuring cast")],
}
LOOP = {   # what lingers: fog, pools, storms (played under the impact, then faded)
    'fire': [('FXFireMedium01LPSD', 'Fire burning', "a fire burning: crackle and roar"), ('MAGFlamesFireLPMSD', 'Flames spell', "Skyrim's Flames held"), ('FXFireEmbers02LPSD', 'Embers', "embers ticking and popping")],
    'frost': [('MAGDestructionBlizzard2DLPM', 'Blizzard', "Skyrim's Blizzard spell: a howling storm"), ('AMBWindSnowBlowingBLPSD', 'Blowing snow', "snow blowing in the wind"), ('MAGFrostBiteFireLPMSD', 'Frostbite held', "Skyrim's Frostbite stream")],
    'shock': [('MAGShockConcentrationFireLPMSD', 'Lightning held', "Skyrim's Sparks held: a continuous crackle"), ('MAGShockWallNodeLP', 'Wall of storms', "Skyrim's Wall of Storms buzzing"), ('MAGCloakShockLPSD', 'Lightning cloak', "Skyrim's Lightning Cloak")],
    'poison': [('MAG_PoisonFireLoopSFX', 'Venom stream', "Mysticism: a hissing stream of venom"), ('AMBSteamLP', 'Hissing gas', "steam hissing"), ('AMBWaterTundraVolcanicPoolsLPSD', 'Bubbling pool', "volcanic pools bubbling")],
    'shadow': [('MAGIllusionFireLPM', 'Illusion held', "Skyrim's illusion held: a murmuring hum"), ('AMBSteamCrackSoulCairnWhispers2DLPSD', 'Soul Cairn whispers', "Dawnguard: the Soul Cairn's whispers"), ('AMBrWindBedSoulCairnLP', 'Soul Cairn wind', "Dawnguard: the Soul Cairn's wind")],
    'spirit': [('AMBSteamCrackSoulCairnWhispers2DLPSD', 'Whispers', "Dawnguard: the Soul Cairn's whispers"), ('NPCWispConsciousLP', 'Wisp', "a wisp's hum"), ('MAGIllusionFireLPM', 'Illusion held', "Skyrim's illusion held")],
    'blood': [('MAGNightCloakBatsHitLPM', 'Bats', "Dawnguard: the Vampire Lord's bats"), ('AMBSteamCrackSoulCairnWhispers2DLPSD', 'Whispers', "Dawnguard: whispers")],
    'sun': [('MAGDestructionStendarrsAura2DLP', "Stendarr's Aura", "Dawnguard: a holy hum"), ('MAGRestorationCircleOfProtection2DLPSD', 'Circle of Protection', "Skyrim's holy circle"), ('MAGLightReadyLPSD', 'Light held', "a soft bright hum")],
    'wind': [('MAGWhirlwindLP', 'Whirlwind', "Dragonborn: a whirlwind spinning"), ('AMBWindSnowBlowingBLPSD', 'Gale', "a gale blowing"), ('AMBrWindMountainHeavyLPSD', 'Mountain wind', "heavy mountain wind")],
    'arcane': [('MAGAlterationCastLPMSD', 'Magic held', "Skyrim's alteration held"), ('AMBTelMithrynFloatMagicLPSD', 'Floating magic', "Dragonborn: Tel Mithryn's floating magic"), ('MAGMysticismSoulTrapActiveLPSD', 'Soul trap', "a soul trap humming")],
    'earth': [('AMBRumbleBLPSD', 'Rumble', "a low ground rumble"), ('AMBRumbleALPSD', 'Deep rumble', "a deeper rumble")],
}

# what each ability plays now: the named blasts (Gun.cpp kLook, Tablet.cpp, Classes.cpp), or none
NOW_AB = {
    'vahlok_hit': ['lightmassive'], 'vahlok_heavy': ['lightmassive'], 'nahkriin_hit': ['light'], 'nahkriin_heavy': ['light'],
    'volsung_light': ['light'], 'volsung_heavy': ['lightmassive'], 'zahkriisos_hit': ['frost'], 'zahkriisos_heavy': ['frost'],
    'otar_hit': ['runeshock'], 'otar_heavy': ['runeshock'], 'rahgot_hit': ['poisongas'], 'rahgot_heavy': ['poisongas'],
    'krosis_hit': ['light'], 'krosis_heavy': ['light'], 'hevnoraak_hit': ['runeshock'], 'hevnoraak_heavy': ['runeshock'],
    'vokun_hit': ['light'], 'vokun_heavy': ['light'], 'morokei_hit': ['lightmassive', 'runeshock'], 'morokei_heavy': ['lightmassive'],
    'lanternwick_heavy': ['frost'], 'eclipse_hit': ['light', 'frost'], 'eclipse_heavy': ['frost', 'lightmassive'],
    'wanderstar_cast': ['light'], 'wanderstar_fifth': ['light'], 'javelin_land': ['fireball'], 'javelin_bounce': ['fireball'], 'javelin_throw': ['frost'],
    'slayer_heavy': ['fireball'], 'staff_heavy': ['lightmassive'], 'scythe_heavy': ['fireball'], 'rapier_heavy': ['fireball'], 'bane_heavy': ['lightmassive'],
    'ffire_land': ['fireball'], 'ffrost_land': ['frost'], 'fshock_land': ['runeshock'], 'facid_land': ['poisongas'], 'fholy_land': ['light'], 'fvoid_land': ['lightmassive'],
    'ffire_power': ['fireball'], 'ffrost_power': ['icevolley'], 'fshock_power': ['runeshock'], 'fvoid_power': ['lightmassive'], 'mutagen_flasks': ['fireball'],
    'wand_hit': ['fireball', 'frost', 'runeshock'], 'soulcaller_wolves': ['lightmassive'],
}
NOW_CAST = {'wand_light': ('Skyrim\'s Firebolt, Lightning Bolt and Ice Spike releases in turn', ['MAGFireboltFire2DSD', 'MAGShockFFFireSD', 'MAGIcicleFireSD']),
            'wand_heavy': ('Skyrim\'s Firebolt, Lightning Bolt and Ice Spike releases in turn', ['MAGFireboltFire2DSD', 'MAGShockFFFireSD', 'MAGIcicleFireSD']),
            'wanderstar_cast': ('Skyrim\'s small illusion blast at the tip', ['MAGFireboltImpactSD'])}
# the cards with a cast of their own (a bolt from the hand), and with something that lingers
BOLT = {'wand_light', 'wand_heavy', 'staff_light', 'slayer_light', 'rapier_light', 'scythe_light', 'wanderstar_cast', 'javelin_throw', 'soulcaller_beam', 'bane_light'}
LINGER = {'nahkriin_heavy', 'vokun_heavy', 'rahgot_hit', 'lanternwick_heavy', 'facid_land', 'ffire_land', 'ffrost_land', 'fshock_land', 'fholy_land',
          'mutagen_elixir', 'bloodmoon_heavy', 'scythe_heavy', 'rapier_heavy', 'bane_heavy', 'cinderclaws_heavy'}
# my pick per card, where it isn't the theme's first: an index into its impact pool, and why
REC = {
    'vahlok_hit': (2, 'A ringing call for the shackle closing, not a fireball: the hit stays crisp under it.'),
    'vahlok_heavy': (6, 'The chains haul the souls in: the souls\' rush sells the pull.'),
    'nahkriin_hit': (7, 'A blinding is quiet: a hiss, not a boom, so it doesn\'t drown the blade.'),
    'nahkriin_heavy': (1, 'A hollow burst as the fog rolls out, the whispers left under it.'),
    'volsung_light': (5, 'A light gust: soft enough to play every light swing.'),
    'volsung_heavy': (0, 'The heavy cone is Fus Ro Dah in a sword: the shout\'s own wall of air.'),
    'otar_hit': (1, 'A short snap for every hit: the big crack is saved for the heavy.'),
    'otar_heavy': (0, 'The arcs all round you: a real lightning crack.'),
    'stormtalons_third': (6, 'A crisp zap on the 3rd hit, quick enough for fast claws.'),
    'stormtalons_heavy': (2, 'The stun ring: the Shock Nova\'s ring of discharge.'),
    'zahkriisos_hit': (2, 'The freeze: ice forming round the one struck.'),
    'zahkriisos_heavy': (0, 'The frozen shatter: ice bursting into shards.'),
    'lanternwick_light': (6, 'A hiss of cold on each light blow.'),
    'lanternwick_heavy': (5, 'The blizzard\'s start: a big frozen boom, the storm left to howl.'),
    'rahgot_hit': (0, 'Wet and acidic, small enough for every stab.'),
    'rahgot_heavy': (4, 'Every poisoned one bursts: a big sickly bubbling burst.'),
    'krosis_hit': (0, 'The drain: a wet suck of life on each hit.'),
    'krosis_heavy': (2, 'The wail that weakens: a deep, thick pulse.'),
    'hevnoraak_hit': (5, 'Armour shattering: metal crumpling.'),
    'hevnoraak_heavy': (0, 'The shockwave through the ground: rock erupting.'),
    'riverstone_heavy': (0, 'The ground split in a line: rock erupting.'),
    'riverstone_hit': (2, 'A heavy clod of earth as they\'re knocked back.'),
    'wyrmtooth_heavy': (3, 'A low rumble through the ground.'),
    'vokun_hit': (5, 'Sleep: a soft burst of dream energy.'),
    'vokun_heavy': (0, 'The nightmare fog: a dark, uneasy swell.'),
    'morokei_hit': (0, 'A bright crackle of pure magic where it lands.'),
    'morokei_heavy': (4, 'A magicka shockwave: a low pulse of energy.'),
    'gravebell_fifth': (2, 'The toll: a ringing holy chord.'),
    'eclipse_hit': (1, 'A clean holy burst (sun) or ice forming (frost), by turns.'),
    'eclipse_heavy': (0, 'Ice then sun: the shatter, then Auriel\'s blast.'),
    'frostmourn_hit': (6, 'A hiss of cold on each thrust.'),
    'frostmourn_heavy': (0, 'The ring of ice: a shatter.'),
    'silverscythe_fourth': (2, 'Moonlight flashing: a ringing call.'),
    'bonereaper_kill': (4, 'The dead raised: a dark necromantic burst.'),
    'talons_hit': (1, 'Bleeding: our wet hit with blood magic under it.'),
    'wyrmtooth_hit': (1, 'Bleeding: our wet hit with blood magic under it.'),
    'bloodmoon_hit': (0, 'Each hit drains: the wet suck of life.'),
    'bloodmoon_heavy': (2, 'The great drain: a deep pulse.'),
    'cinderclaws_hit': (3, 'A short dry thud of fire on each swipe: fast claws need short sounds.'),
    'cinderclaws_heavy': (2, 'A burst with crackling embers after.'),
    'wyrmspine_pike_hit': (3, 'A short dry thud of fire as it pierces.'),
    'wyrmspine_pike_heavy': (4, 'The bone spike: flames catching and roaring up.'),
    'javelin_land': (1, 'The javelin lands like a fireball: the full roar.'),
    'javelin_bounce': (0, 'Each bounce: a quick fat whoosh of fire.'),
    'slayer_light': (0, 'Each golden fireball: a quick whoosh of flame.'),
    'slayer_heavy': (1, 'The fireballs out of the sky: the full roar.'),
    'bane_light': (6, 'Alduin\'s meteor: a sharp crack of fire.'),
    'bane_heavy': (1, 'The white wind: a gale tearing through.'),
    'scythe_light': (0, 'The shadowflame bolt: a fat whoosh.'),
    'scythe_heavy': (4, 'The tornado: flames catching and roaring up.'),
    'rapier_light': (0, 'A fat whoosh of flame.'),
    'rapier_heavy': (4, 'The tornado: flames catching and roaring up.'),
    'rapier_line': (0, 'The line of ice: a shatter.'),
    'staff_light': (3, 'The sunball: a light flaring up.'),
    'staff_heavy': (6, 'The sunbeam out of the sky: Auriel\'s strike.'),
    'wanderstar_cast': (5, 'Star magic: a shower of bright chimes.'),
    'wanderstar_fifth': (0, 'The golden fling: Auriel\'s sun blast.'),
    'mutagen_flasks': (0, 'A fat whoosh of flame.'),
    'mutagen_elixir': (5, 'The elixir: a shower of bright chimes.'),
    'wand_light': (0, 'Each Carian bolt: a bright crackle of pure magic.'),
    'wand_hit': (0, 'A bright crackle of pure magic.'),
    'wand_heavy': (1, 'A great bolt: a hot, humming plasma pop.'),
    'soulcaller_beam': (3, 'The soul beam: a ringing aetherial hit.'),
    'soulcaller_wolves': (6, 'A wolf arrives: a magical blast.'),
    'summon_arrive': (6, 'A summon arrives: a magical blast.'),
    'fvoid_land': (4, 'The void pulls in: a low pulse.'),
    'fvoid_power': (2, 'Then it pushes out: a crack of force.'),
    'ffire_land': (1, 'The flask breaks into fire: the full roar.'),
    'ffire_power': (5, 'The firestorm: a deep fiery pop.'),
    'ffrost_land': (3, 'A cold whump with glassy bits.'),
    'ffrost_power': (5, 'A big frozen boom.'),
    'fshock_land': (2, 'A ring of crackling discharge.'),
    'fshock_power': (4, 'Storm Call\'s strike: huge.'),
    'facid_land': (1, 'A gassy pop with a hiss.'),
    'fholy_land': (1, 'A bright, clean holy burst.'),
}
# a card whose ability really sounds like another theme than its look (eclipse sun+frost: its sun first)
THEME_OF = {'eclipse_hit': 'sun', 'bane_heavy': 'wind', 'gravebell_fifth': 'sun', 'soulcaller_beam': 'arcane'}


def _layers(s):
    if isinstance(s, str): return [[s, 1.0, 0.0]]
    return [[x, 1.0, 0.0] if isinstance(x, str) else [x[0], x[1], x[2]] for x in s]


def _ok(s):
    return all((r.startswith('coa:') and r[4:] in COA) or r in BY for r, _, _ in _layers(s))


def _opt(i, s, name, desc, **kw):
    return dict({'id': f'a{i}', 'name': name, 'desc': desc, 'snd': _layers(s)}, **kw)


def ability_slots(card):
    kind, theme = AOP.KIND[card]
    theme = THEME_OF.get(card, theme)
    pool = [p for p in IMPACT[theme] if _ok(p[0])]
    slots = []
    if card in BOLT:
        ct = theme if theme in CAST else ('fire' if theme in ('blood',) else 'arcane')
        cp = [p for p in CAST[ct] if _ok(p[0])]
        if card in NOW_CAST:
            t, L = NOW_CAST[card]; now = _opt(1, [[x, 1.0, i * 0.0] for i, x in enumerate(L[:1])], 'As it is now', t, now=True, cycle=L)
        else:
            now = _opt(1, [], 'As it is now', 'No cast sound of its own: you hear the swing or the staff.', now=True)
        rest = cp[:4]
        opts = [now] + [_opt(i + 2, s, n, d) for i, (s, n, d) in enumerate(rest)]
        slots.append({'id': 'cast', 'label': 'Cast', 'opts': opts, 'rec': 'a2', 'why': 'A release of its own so you hear it leave the hand.', 't': 'cast'})
    # the impact
    nb = NOW_AB.get(card)
    if nb:
        snd = [[r, 1.0, 0.0] for b in nb[:1] for r in BLAST.get(b, [])[:1]]
        now = _opt(1, snd, 'As it is now', ' then '.join(BLAST_NAME.get(b, b) for b in nb) + '.', now=True)
    else:
        now = _opt(1, [], 'As it is now', 'No sound of its own: only the weapon\'s hit.', now=True)
    ri, why = REC.get(card, (0, 'The truest sound for what it does.'))
    ri = min(ri, len(pool) - 1)
    order = [ri] + [i for i in range(len(pool)) if i != ri]
    # never offer what plays now a second time
    nowref = {r for r, _, _ in now['snd']}
    picks = [pool[i] for i in order if not (len(_layers(pool[i][0])) == 1 and _layers(pool[i][0])[0][0] in nowref)][:4]
    opts = [now] + [_opt(i + 2, s, n, d) for i, (s, n, d) in enumerate(picks)]
    slots.append({'id': 'impact', 'label': 'Impact' if card not in ('vahlok_heavy',) else 'Impact', 'opts': opts, 'rec': 'a2', 'why': why, 't': 'impact'})
    if card in LINGER:
        lp = [p for p in LOOP.get(theme, LOOP['arcane']) if _ok(p[0])]
        opts = [_opt(1, [], 'As it is now', 'Nothing lingers: it goes quiet after the impact.', now=True)] + [_opt(i + 2, s, n, d, loop=True) for i, (s, n, d) in enumerate(lp[:4])]
        slots.append({'id': 'linger', 'label': 'While it lingers', 'opts': opts, 'rec': 'a2', 'why': 'What it leaves behind keeps sounding, faded out as it fades.', 't': 'linger'})
    return slots


# ---------------------------------------------------------------- the weapons themselves
# Skyrim's own sounds by weapon type (what plays now: the animation's sound events for that type)
TYPES = {
    'sword': ('one-handed sword', 'WPNBlade1HandDrawSD', 'WPNSwingBladeMediumSD', 'WPNImpactBladeVsFleshSD'),
    'dagger': ('dagger', 'WPNBlade1HandSmallDrawSD', 'WPNSwingBladeMediumSD', 'WPNImpactBladeVsFleshSD'),
    'greatsword': ('greatsword', 'WPNBlade2HandDrawSD', 'WPNSwing2Hand', 'WPNImpactBlade2HandVsFleshSD'),
    'battleaxe': ('battleaxe', 'WPNAxe2HandDrawSD', 'WPNSwing2Hand', 'WPNImpactAxeLargeVsFleshSD'),
    'warhammer': ('warhammer', 'WPNBlunt2HandDraw', 'WPNSwing2Hand', 'WPNImpactBlunt2HandVsFlesh'),
    'mace': ('mace', 'WPNMace1HandDrawSD', 'WPNSwingBlunt1Hand', 'WPNImpactBluntVsFleshSD'),
    'staff': ('staff', 'WPNStaffHandDrawSD', None, None),
}
WEAPONS = {   # ABIL item id -> (Skyrim type it's built on, what it is, its element)
    'vahlok': ('warhammer', 'warhammer', 'spirit'), 'nahkriin': ('sword', 'sword', 'spirit'), 'volsung': ('sword', 'sword', 'wind'),
    'zahkriisos': ('greatsword', 'greatsword', 'frost'), 'otar': ('sword', 'sword', 'shock'), 'rahgot': ('dagger', 'dagger', 'poison'),
    'krosis': ('sword', 'sword', 'blood'), 'hevnoraak': ('battleaxe', 'battleaxe', 'earth'), 'vokun': ('battleaxe', 'scythe', 'shadow'),
    'morokei': ('warhammer', 'warhammer', 'arcane'),
    'stormtalons': ('dagger', 'claws', 'shock'), 'gravebell': ('mace', 'mace', 'sun'), 'talons': ('dagger', 'claws', 'blood'),
    'riverstone': ('battleaxe', 'halberd', 'earth'), 'wyrmspine_pike': ('greatsword', 'pike', 'fire'), 'lanternwick': ('battleaxe', 'quarterstaff', 'frost'),
    'cinderclaws': ('dagger', 'claws', 'fire'), 'eclipse': ('greatsword', 'spear', 'sun'), 'wanderstar': ('staff', 'wand', 'sun'),
    'frostmourn': ('dagger', 'rapier', 'frost'), 'silverscythe': ('battleaxe', 'scythe', 'spirit'), 'bonereaper': ('battleaxe', 'scythe', 'spirit'),
    'wyrmtooth': ('battleaxe', 'scythe', 'blood'), 'bloodmoon': ('battleaxe', 'scythe', 'blood'),
    'javelin': ('sword', 'javelin', 'fire'), 'soulcaller': ('staff', 'staff', 'arcane'), 'mutagen': ('staff', 'staff', 'sun'),
    'slayer': ('greatsword', 'greatsword', 'fire'), 'bane': ('greatsword', 'greatsword', 'wind'),
    'scythe': ('battleaxe', 'scythe', 'shadow'), 'wand': ('staff', 'wand', 'arcane'), 'staff': ('battleaxe', 'quarterstaff', 'sun'), 'rapier': ('sword', 'rapier', 'fire'),
}
SHIMMER = {   # a soft layer of its element under the draw and the heavy swing
    'fire': 'WB_DestructionFire_Sound_Release_1', 'frost': 'WB_DestructionFrost_Release_1_Sound_Tiny', 'shock': 'WB_DestructionShock_Sound_Release_JoyBuzzer_2',
    'poison': 'MAG_PoisonReleaseSFX', 'sun': 'MAGLightFireSD', 'blood': 'WB_RestorationBlood_Sound_Release_OrcishHealing', 'earth': 'WB_AlterationEarth_Sound_Release',
    'wind': 'VOCShoutFXSprintFireSD', 'spirit': 'WB_ConjurationBanish_Sound_Release_Hiss', 'shadow': 'WB_IllusionShadow_Sound_Release_Buff', 'arcane': 'MAGAlterationReleaseSD'}
EL_NAME = {'fire': 'fire', 'frost': 'frost', 'shock': 'lightning', 'poison': 'venom', 'sun': 'light', 'blood': 'blood magic', 'earth': 'stone', 'wind': 'wind',
           'spirit': 'spirit', 'shadow': 'shadow', 'arcane': 'arcane magic'}
HEAVY_TYPES = {'greatsword', 'battleaxe', 'warhammer'}


def weapon_slots(wid):
    base, what, el = WEAPONS[wid]
    tn, draw, swing, hit = TYPES[base]
    sh = SHIMMER[el]; en = EL_NAME[el]
    slots = []
    # the draw
    D = [_opt(1, draw, 'As it is now', f"Skyrim's {tn} draw.", now=True),
         _opt(2, [[draw, 1.0, 0], [sh, 0.45, 0.08]], f'Draw with {en}', f"Skyrim's {tn} draw with a soft breath of {en} as it comes out: you hear what it is."),
         _opt(3, 'WPNBlade1HandMiraakDrawSD', "Miraak's blade", "Dragonborn: Miraak's sword drawn - eldritch, wet and hissing."),
         _opt(4, [[draw, 1.0, 0], ['WPNLeftHandDraw', 0.8, 0.05]], 'Draw with a spell hand', f"the {tn} draw with a spell hand readied with it."),
         _opt(5, 'WPNBlade2HandDrawSD' if draw != 'WPNBlade2HandDrawSD' else 'WPNAxe2HandDrawSD', 'A heavier draw', 'a bigger weapon\'s draw: weightier.')]
    slots.append({'id': 'draw', 'label': 'Draw', 'opts': [o for o in D if _ok([r for r, _, _ in o['snd']] or 'WPNLeftHandDraw')], 'rec': 'a2', 'why': f'Every relic and wonder is magic: a breath of {en} as it comes out says so before it swings.', 't': 'draw'})
    if swing:
        big = base in HEAVY_TYPES
        S = [_opt(1, swing, 'As it is now', f"Skyrim's {tn} swing.", now=True),
             _opt(2, [[swing, 1.0, 0], ['coa:sp_cold_whoosh', 0.35, 0.02]], 'Swing with a whoosh', f"Skyrim's swing with our cold whoosh under it: the air torn, heard over a crowd."),
             _opt(3, 'WPNSwingMiraakBlade', "Miraak's swing", "Dragonborn: Miraak's blade cutting the air - sharper, stranger."),
             _opt(4, 'WPNSwingBladeAxeSD' if not big else 'WPNSwingBladeMediumSD', 'Axe swing' if not big else 'Lighter swing', "Skyrim's axe swing: chunkier." if not big else "a lighter blade swing: quicker."),
             _opt(5, 'WPNSwing2Hand' if not big else 'WPNSwingBladeAxeSD', 'Two-handed swing' if not big else 'Axe swing', "Skyrim's two-handed swing: a big sweep." if not big else "Skyrim's axe swing.")]
        slots.append({'id': 'swing', 'label': 'Swing', 'opts': S, 'rec': 'a2', 'why': 'Skyrim\'s swings get lost under a horde; a little whoosh under them carries.', 't': 'swing'})
        H = [_opt(1, swing, 'As it is now', f"the same swing as a light attack.", now=True),
             _opt(2, [[swing, 1.0, 0], [sh, 0.7, 0.04], ['coa:sp_cold_whoosh', 0.3, 0.0]], f'Heavy with {en}', f'the swing with a surge of {en}: you hear the power attack coming before it lands.'),
             _opt(3, [['WPNSwing2Hand', 1.0, 0], ['VOCShoutFXSprintFireSD', 0.4, 0.0]], 'Wind-up gust', 'a two-handed sweep with a rush of wind.'),
             _opt(4, [[swing, 1.0, 0], ['coa:champ_telegraph', 0.35, 0.0]], 'Telegraph', 'the swing with our champion\'s telegraph under it: a dark rising swell.'),
             _opt(5, 'WPNSwingMiraakBlade', "Miraak's swing", "Dragonborn: Miraak's blade.")]
        slots.append({'id': 'heavy', 'label': 'Heavy swing', 'opts': [o for o in H if _ok([r for r, _, _ in o['snd']])], 'rec': 'a2', 'why': 'Light and heavy sound the same now; the heavy should promise its ability.', 't': 'heavy'})
        bl = 'WPNImpactBlade2HandVsFleshDraugr' if big and base != 'warhammer' else 'WPNImpactBladeVsFleshDraugr'
        if base in ('warhammer', 'mace'): bl = 'WPNImpactBlunt2HandVsFlesh' if base == 'warhammer' else 'WPNImpactBluntVsFleshSD'
        X = [_opt(1, hit, 'As it is now', f"Skyrim's {tn} hitting flesh.", now=True),
             _opt(2, [[bl, 1.0, 0], ['coa:flesh_hit_0', 0.55, 0.0]], 'Undead flesh, wet', "Skyrim's own sound for this weapon hitting draugr (dry dead flesh), with our wet hit under it: every hit lands."),
             _opt(3, bl, 'Undead flesh', "Skyrim's own sound for this weapon hitting draugr."),
             _opt(4, [[hit, 1.0, 0], ['coa:flesh_hit_2', 0.6, 0.0]], 'Wet', f"Skyrim's {tn} hit with our wet flesh under it."),
             _opt(5, 'WPNImpactBladeMiraakVsFleshSD' if base not in ('warhammer', 'mace') else 'WPNImpactCrGiantClubVsFlesh', "Miraak's blade" if base not in ('warhammer', 'mace') else "A giant's club",
                  "Dragonborn: Miraak's blade hitting - meaty and strange." if base not in ('warhammer', 'mace') else "a giant's club hitting flesh: enormous.")]
        if base in ('warhammer', 'mace'): X[1]['desc'] = X[1]['desc'].replace('hitting draugr (dry dead flesh)', 'hitting flesh hard'); X[2]['desc'] = "Skyrim's heavier blunt hit."
        slots.append({'id': 'hit', 'label': 'Hit', 'opts': [o for o in X if _ok([r for r, _, _ in o['snd']])], 'rec': 'a2', 'why': 'The dead are undead: Skyrim\'s draugr hit is the truest, and our wet layer makes each one land.', 't': 'hit'})
    else:   # wands and staffs cast: their sounds are their abilities'
        slots[0]['why'] = f'A breath of {en} as it comes out.'
    return slots


_MP = ROOT / 'build/sound/mp3/map.json'
MP3 = json.load(open(_MP)) if _MP.exists() else {}   # (the device's cut: each game file -> its small mp3, build/sound/mp3)


def build():
    out = {'weapons': {}, 'abilities': {}, 'blastNames': BLAST_NAME}
    for wid in WEAPONS: out['weapons'][wid] = weapon_slots(wid)
    for card in AOP.KIND: out['abilities'][card] = ability_slots(card)
    # every sound used, and its files (up to 2 takes)
    used = set()
    for group in (out['weapons'], out['abilities']):
        for slots in group.values():
            for sl in slots:
                for o in sl['opts']:
                    for r, _, _ in o['snd']: used.add(r)
                    for r in o.get('cycle', []): used.add(r)
    files = {}
    for r in sorted(used):
        if r.startswith('coa:'): files[r] = {'src': 'ours', 'files': ['coa/' + r[4:] + '.wav'], 'mp3': ['coa_' + r[4:] + '.mp3']}; continue
        s = BY[r]; files[r] = {'mp3': [MP3[f] for f in s['files'][:2] if f in MP3], 'src': s['src'].replace('.esm', '').replace('.esp', '').replace('Apocalypse - Magic of Skyrim', 'Apocalypse').replace('MysticismMagic', 'Mysticism'), 'files': s['files'][:2]}
    out['sounds'] = files
    return out


if __name__ == '__main__':
    o = build()
    (ROOT / 'docs/workshop/sounds.json').write_text(json.dumps(o, indent=1))
    want = sorted({f for v in o['sounds'].values() if v['src'] != 'ours' for f in v['files']})
    (ROOT / 'build/sound/want.txt').write_text('\n'.join(want) + '\n')
    n = sum(len(s['opts']) for g in (o['weapons'], o['abilities']) for v in g.values() for s in v)
    print('sounds.json:', len(o['weapons']), 'weapons,', len(o['abilities']), 'abilities,', n, 'options,', len(o['sounds']), 'sounds,', len(want), 'game files')


def sound_entry(r):
    """one sound's entry for the doc (its small mp3s and where it's from), for the other tabs' sounds too"""
    if r.startswith('coa:'): return {'src': 'ours', 'files': ['coa/' + r[4:] + '.wav'], 'mp3': ['coa_' + r[4:] + '.mp3']}
    s = BY[r]
    return {'mp3': [MP3[f] for f in s['files'][:2] if f in MP3], 'src': s['src'].replace('.esm', '').replace('.esp', '').replace('Apocalypse - Magic of Skyrim', 'Apocalypse').replace('MysticismMagic', 'Mysticism'), 'files': s['files'][:2]}
