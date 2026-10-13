"""Element emblems for the Coffer's spells (the HUD's spell dock and the shrine's roll): the same forged medallion as
the old spell emblems (tools/spellicons.py), one per element, the element's sigil burning in its colour.

    python tools/elementicons.py [out dir]     writes el_<element>.png (256x256) and prints their colours
"""
import pathlib, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import spellicons

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'assets' / 'ui')
# element: (colour, accent, sigil)
ELEMENTS = {
    'fire': ('FF6A1E', 'FFD27A', 'fire'),
    'frost': ('6FC8FF', 'E6F6FF', 'ice'),
    'shock': ('8C7CFF', 'E8E2FF', 'shock'),
    'poison': ('6FD24A', 'D8FFB0', 'thorn'),
    'disease': ('9AA84A', 'E2E8A0', 'soul'),
    'sun': ('FFC83C', 'FFF4C8', 'dawn'),
    'blood': ('D0202E', 'FF9A9A', 'blood'),
    'arcane': ('B45CFF', 'F0D8FF', 'arcane'),
}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for el, (c, a, sig) in ELEMENTS.items():
        spellicons.medallion(c, a, sig).save(OUT / f'el_{el}.png')
        print('el_' + el, c)


if __name__ == '__main__':
    main()
