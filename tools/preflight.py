"""Preflight: lay every sheet's rows and columns over each other.
Errors: a column missing or empty in a row, a reference ("sheet:id" or "quest:/sound:/system:" prefixed)
that does not resolve. Warnings: cells listed in a row's `verify` (not yet checked in the running game).
Exit 1 on any error."""
import json, sys, pathlib, re
root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).parent.parent / 'sheets')
sheets = {json.load(open(p))['sheet']: json.load(open(p)) for p in sorted(root.glob('*.json'))}
ids = {s: {r['id'] for r in d['rows']} for s, d in sheets.items()}
alias = {'quest': 'quests', 'sound': 'sounds', 'system': 'systems'}
errors, warns = [], []
ref_re = re.compile(r'^([a-z_]+):([A-Za-z0-9_]+)(\..+)?$')
def check_ref(where, v):
    m = ref_re.match(v)
    if not m: return
    s, i = alias.get(m.group(1), m.group(1)), m.group(2)
    if s not in sheets: return  # not a sheet reference (e.g. "event:..." kinds)
    if i not in ids[s]: errors.append(f'{where}: reference {v} does not resolve')
for s, d in sheets.items():
    cols = d['columns']
    for r in d['rows']:
        for c in cols:
            if c not in r: errors.append(f'{s}.{r.get("id")}.{c}: missing'); continue
            v = r[c]
            if v is None or v == '' : errors.append(f'{s}.{r["id"]}.{c}: empty')
            vals = v if isinstance(v, list) else [v]
            for x in vals:
                if isinstance(x, str): check_ref(f'{s}.{r["id"]}.{c}', x)
        for c in r:
            if c not in cols: errors.append(f'{s}.{r["id"]}.{c}: not a declared column')
        for c in r.get('verify', []):
            if c not in cols: errors.append(f'{s}.{r["id"]}: verify names unknown column {c}')
            else: warns.append(f'{s}.{r["id"]}.{c}')
cells = sum(len(d['rows']) * len(d['columns']) for d in sheets.values())
print(f'preflight: {len(sheets)} sheets, {sum(len(d["rows"]) for d in sheets.values())} rows, {cells} cells')
for e in errors: print('  ERROR', e)
print(f'  {len(warns)} cells still to verify in game:', ', '.join(warns) if warns else 'none')
sys.exit(1 if errors else 0)
