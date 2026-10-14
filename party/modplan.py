"""Plan the party's new mods: where each archive's files go (FOMODs resolved with our picks), for un_mods.ps1 to carry
out natively on Windows (its own tar.exe reads zip, 7z and rar at full disk speed).

    python3 modplan.py <downloads dir> <bsdtar> <out plan.json>

Plan entries: {name, archive, target ('mods' = every player needs it; 'graphics' = textures, optional), ops: [[src, dst,
is_dir]]}; src is a path inside the archive (case as the archive has it), dst a path under Data.
"""
import json, pathlib, re, subprocess, sys, xml.etree.ElementTree as ET

DL, BSDTAR, OUT = pathlib.Path(sys.argv[1]), sys.argv[2], pathlib.Path(sys.argv[3])

# (name, archive glob, target, kind, args)   map: {archive prefix: Data prefix}   fomod: pick/drop/recommended
RECIPES = [
    ('SkyUI', 'SkyUI-12604-6-11-1778020881.zip', 'mods', 'map', {'': ''}),
    ('MCM Helper', 'MCM Helper*.7z', 'mods', 'map', {'Data/': '', 'ESL/': '', 'BSA/': '', 'SkyrimSE/': ''}),
    ('Behavior Data Injector', 'Behavior Data Injector*.7z', 'mods', 'map', {'Behavior Data Injector/': ''}),
    ('Spell Perk Item Distributor', 'Spell Perk Item Distributor*.7z', 'mods', 'map', {'': ''}),
    ('Papyrus Extender', 'Papyrus Extender*.7z', 'mods', 'map', {"powerofthree's Papyrus Extender FOMOD Installer/Required/": '', "powerofthree's Papyrus Extender FOMOD Installer/AE1170/": ''}),
    ('Better Jumping NG', 'Better Jumping NG*.zip', 'mods', 'map', {'Data/': ''}),
    ('Character Menu SE', 'Character Menu SE*.zip', 'mods', 'map', {'': ''}),
    ('QuickLoot RE', 'QuickLootRE*.zip', 'mods', 'map', {'': ''}),
    ('Experience', 'Experience 17751*.7z', 'mods', 'map', {'00 Required/': '', '01 Plugin/AE1170/': ''}),
    ('SkyParkour', 'SkyParkour*.7z', 'mods', 'map', {'': ''}),
    ('TK Dodge Distance', 'TK Dodge - Distance*.zip', 'mods', 'map', {'TKDodgeFartherSteps/': ''}),
    ('Leviathan Two-Handed High Stance', 'Leviathan Animations - Two-Handed*.7z', 'mods', 'map', {'Leviathan Animations - Two-Handed High Stance/': ''}),
    ('Phantom Horse', 'Phantom Horse*.zip', 'mods', 'map', {'Phantom Horse/Base/': '', 'Phantom Horse/50 Slightly fast/': ''}),
    ('Sigma Magic', 'Sigma - magic*.7z', 'mods', 'map', {'Sigma - magic/': ''}),
    ('Goetia Conditional Shouts', 'Goetia Animations - Conditional Shouts*.7z', 'mods', 'map', {'Goetia Animations - Conditional Shouts/': ''}),
    ('Thundering Shouts', 'Thundering Shouts*.rar', 'mods', 'map', {'': ''}),
    ('Thunderchild', 'Thunderchild*.zip', 'mods', 'map', {'': ''}),
    ('The Restless Dead', 'The Restless Dead*.7z', 'mods', 'fomod', {'pick': ['Customize Installation', 'Default'], 'drop': ['Skyrim Unbound', 'Creation Club - Fishing', 'Cannibal Draugr on Solstheim',
        'Hand Placed Enemies (Main Version)', 'Hand Placed Enemies (Light Version)', "Konahrik's Accoutrements", 'Legacy of the Dragonborn', 'Mortal Enemies', 'Skeleton Replacer HD',
        'Unofficial Skyrim Special Edition Patch', 'Weapon Armor Clothing and Clutter Fixes']}),
    ('Bone Colossus', 'Bone Colossus*.rar', 'mods', 'map', {'bonecolossus/Data/': ''}),
    ('Old Gods of the Hunt', 'Old Gods of the Hunt*.rar', 'mods', 'map', {'old god/data/': ''}),
    ('Giants Overhaul', 'Giants Overhaul*.rar', 'mods', 'map', {'Giants Overhaul (SE)/Data/': ''}),
    ('Foglings', 'foglings*.rar', 'mods', 'map', {'mihail foglings/Data/': ''}),
    ('Diverse Dragons Collection', 'Diverse Dragons Collection*.zip', 'mods', 'map', {'': ''}),
    ('Particle Lights - Undead Creatures', 'Particle Lights for ENB SE - Undead*.7z', 'mods', 'fomod', {'pick': ['Default'], 'drop': ['Cannibal Draugr']}),
    ('High Fantasy Pack', 'High Fantasy Pack (se-ae) v2.1-56523-2-1-1721817230.rar', 'mods', 'map', {'High Fantasy Pack (SE)/data/': ''}),
    ('Sentinel', 'Sentinel - An Equipment Overhaul*.7z', 'mods', 'fomod', {'recommended': True}),
    ('Sentinel - Required Resources', 'Sentinel - Required Resources-100985-3-2-0-1740901115.7z', 'mods', 'map', {'': ''}),
    ('COR - Character Overhaul', 'COR MAIN*.7z', 'mods', 'map', {'': ''}),
    # looks only: the optional Graphics part
    ('Lux', 'Lux (main)*.rar', 'graphics', 'fomod', {'pick': ['Vanilla', 'Default vanilla models', 'No Grass In Caves', 'USSEP latest version']}),
    ("Riton's Medieval Solitude", "Riton*Medieval Solitude*.rar", 'graphics', 'map', {"Riton's Medieval Solitude 2k/": ''}),
    ('Stony AF Markarth', 'Stony AF Markarth*.zip', 'graphics', 'map', {'': ''}),
    ('Venerable Nordic Temples', 'Venerable Nordic Temples*.zip', 'graphics', 'map', {'': ''}),
    ('Skyland AIO', 'Skyland AIO-34179-4-32-1700281282 (1).7z', 'graphics', 'fomod', {'pick': ['Start', 'Yes', 'Snowy Stone Roads', 'Tan', 'Whiterun', 'Winterhold', 'Riften', 'Markarth', 'Solitude',
        'High Hrothgar', 'Windhelm', 'Sovngarde', 'Skyhaven Temple', 'Grey', 'RW2', 'Skyking Galaxy (Favorite)', 'Finish'], 'drop': ['Windhelm Donor Wall'], 'exactly': {'Dirt Roads': 'No'}}),
]
SKIP = re.compile(r'(^|/)(fomod|meta\.ini|readme[^/]*|[^/]*\.(jpg|jpeg|png|pdb|url|html|txt|md)|source/scripts|patchhub)(/|$)', re.I)


def listing(arch):
    r = subprocess.run([BSDTAR, '-tf', str(arch)], capture_output=True, text=True, errors='replace')
    return [l for l in r.stdout.splitlines() if l]


def resolver(names):
    low = {}
    for n in names:
        parts = n.rstrip('/').split('/')
        for i in range(1, len(parts) + 1):
            low.setdefault('/'.join(parts[:i]).lower(), '/'.join(parts[:i]))
    return lambda p: low.get(p.strip('/').replace('\\', '/').lower())


def fomod_ops(arch, names, args):
    cfgname = next(n for n in names if n.lower().endswith('fomod/moduleconfig.xml'))
    base = cfgname[:-len('fomod/ModuleConfig.xml')]
    xml = subprocess.run([BSDTAR, '-xOf', str(arch), cfgname], capture_output=True).stdout
    root = ET.fromstring(xml.decode('utf-16') if xml[:2] in (b'\xff\xfe', b'\xfe\xff') else xml.decode('utf-8-sig', 'replace'))
    pick, drop, rec, exactly = set(args.get('pick', [])), set(args.get('drop', [])), args.get('recommended', False), args.get('exactly', {})
    files, flags = [], {}

    def take(node):
        for f in list(node):
            if f.tag in ('file', 'folder'):
                files.append((f.get('source'), f.get('destination', '') or '', f.tag == 'folder', int(f.get('priority', '0'))))

    def cond_ok(dep):
        if dep is None:
            return True
        res = []
        for d in list(dep):
            if d.tag == 'flagDependency':
                res.append(flags.get(d.get('flag'), '') == (d.get('value') or ''))
            elif d.tag == 'dependencies':
                res.append(cond_ok(d))
            elif d.tag == 'fileDependency':
                res.append(False)     # (a mod we don't have)
            else:
                res.append(True)
        return all(res) if dep.get('operator', 'And') == 'And' else any(res)

    req = root.find('requiredInstallFiles')
    if req is not None:
        take(req)
    chosen_names = []
    for step in root.iter('installStep'):
        v = step.find('visible')
        if v is not None and not cond_ok(v):
            continue
        for grp in step.findall('./optionalFileGroups/group'):
            gtype, plugins = grp.get('type'), grp.findall('./plugins/plugin')
            def ptype(pl):
                t = pl.find('./typeDescriptor/type')
                if t is not None:
                    return t.get('name')
                d = pl.find('./typeDescriptor/dependencyType')
                if d is not None:
                    for pat in d.findall('./patterns/pattern'):
                        if cond_ok(pat.find('dependencies')):
                            return pat.find('type').get('name')
                    dt = d.find('defaultType')
                    return dt.get('name') if dt is not None else ''
                return ''
            if grp.get('name') in exactly:
                chosen = [p for p in plugins if p.get('name') == exactly[grp.get('name')]]
            else:
                chosen = [p for p in plugins if p.get('name') not in drop and (p.get('name') in pick or ptype(p) == 'Required' or (rec and ptype(p) == 'Recommended'))]
            if gtype == 'SelectAll':
                chosen = plugins
            if gtype in ('SelectExactlyOne', 'SelectAtLeastOne') and not chosen:
                cand = [p for p in plugins if p.get('name') not in drop and ptype(p) not in ('NotUsable',)]
                chosen = cand[:1]
            if gtype in ('SelectExactlyOne', 'SelectAtMostOne') and len(chosen) > 1:
                named = [p for p in chosen if p.get('name') in pick]
                chosen = named[:1] or chosen[:1]
            for pl in chosen:
                fl = pl.find('files')
                if fl is not None:
                    take(fl)
                for fg in pl.iter('flag'):
                    flags[fg.get('name')] = (fg.text or '').strip()
                chosen_names.append(f"{grp.get('name')}: {pl.get('name')}")
    cfi = root.find('conditionalFileInstalls')
    if cfi is not None:
        for pat in cfi.iter('pattern'):
            if cond_ok(pat.find('dependencies')):
                fl = pat.find('files')
                if fl is not None:
                    take(fl)
    real = resolver(names)
    ops = []
    for src, dst, folder, prio in sorted(files, key=lambda x: x[3]):
        s = real(base + src.replace('\\', '/'))
        if not s:
            print('   !! missing in archive:', src)
            continue
        d = dst.replace('\\', '/').strip('/')
        if not folder and (not d or d.endswith('/') or '.' not in pathlib.PurePosixPath(d).name):
            d = (d + '/' if d else '') + pathlib.PurePosixPath(s).name
        ops.append([s, d, folder])
    return ops, chosen_names


def main():
    plan = []
    for name, pat, target, kind, args in RECIPES:
        arch = sorted(DL.glob(pat))
        if not arch:
            print('-- missing archive:', name, pat)
            continue
        arch = arch[0]
        names = listing(arch)
        if kind == 'map':
            real = resolver(names)
            ops = []
            for src, dst in args.items():
                s = real(src) if src else ''
                if src and not s:
                    print('   !! missing prefix', name, src)
                    continue
                ops.append([s, dst.strip('/'), True])
            chosen = []
        else:
            ops, chosen = fomod_ops(arch, names, args)
        plan.append({'name': name, 'archive': arch.name, 'target': target, 'ops': ops})
        print(f'== {name}: {len(ops)} ops' + (f' | {len(chosen)} options' if chosen else ''))
    OUT.write_text(json.dumps({'skip': SKIP.pattern, 'mods': plan}, indent=1))
    print('wrote', OUT)


main()
