import json, math, re
import numpy as np
N = json.load(open('nav.json')); R = json.load(open('refs.json'))
W = R['wrld']; inv = {v: k for k, v in W.items()}
HUB = {'WhiterunWorld': (24900, -7250), 'SolitudeWorld': (-61400, 104750), 'WindhelmWorld': (130800, 36100), 'MarkarthWorld': (-174100, 5900), 'RiftenWorld': (173400, -97000),
       'Riverwood': (21941, -44792), 'Falkreath': (-30296, -86284), 'Morthal': (-38640, 66734), 'Dawnstar': (30808, 106138), 'Winterhold': (109208, 102864)}
def tris(k):
    out = []
    for m in N[k]:
        V = np.array(m['v']).reshape(-1, 3)
        for a, b, c in m['t']: out.append((V[a], V[b], V[c]))
    return out
def build(T):
    cell = 256.0; grid = {}
    for i, (a, b, c) in enumerate(T):
        x0, x1 = min(a[0], b[0], c[0]), max(a[0], b[0], c[0]); y0, y1 = min(a[1], b[1], c[1]), max(a[1], b[1], c[1])
        for gx in range(int(x0 // cell), int(x1 // cell) + 1):
            for gy in range(int(y0 // cell), int(y1 // cell) + 1): grid.setdefault((gx, gy), []).append(i)
    return grid, cell
def height(T, G, x, y):
    grid, cell = G
    for i in grid.get((int(x // cell), int(y // cell)), []):
        a, b, c = T[i]
        d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(d) < 1e-6: continue
        l1 = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / d
        l2 = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / d
        l3 = 1 - l1 - l2
        if l1 >= -1e-3 and l2 >= -1e-3 and l3 >= -1e-3: return l1 * a[2] + l2 * b[2] + l3 * c[2]
    return None
import sys
K=float(sys.argv[1]) if len(sys.argv)>1 else 1.0
FOOT = [(x*K, y*K) for x in np.linspace(-330, 330, 9) for y in np.linspace(-280, 140, 7)]  # stand + its approach in front (-y)
def refs_near(wkey, x, y, r):
    return [q for q in R['refs'] if q[0] == wkey and abs(q[3] - x) < r and abs(q[4] - y) < r]
res = {}
for k, (hx, hy) in HUB.items():
    T = tris(k); G = build(T)
    wkey = inv.get(k, inv['Tamriel'])
    near = [q for q in R['refs'] if q[0] == wkey and math.hypot(q[3] - hx, q[4] - hy) < 3500]
    doors = [(q[3], q[4]) for q in near if re.search('Door|Gate', q[2])]
    best = []
    for dx in range(-2400, 2401, 120):
        for dy in range(-2400, 2401, 120):
            cx, cy = hx + dx, hy + dy
            if math.hypot(dx, dy) > 2400: continue
            hc = height(T, G, cx, cy)
            if hc is None: continue
            for yaw in range(0, 360, 30):
                a = math.radians(yaw); zs = []
                ok = True
                for fx, fy in FOOT:
                    x = cx + fx * math.cos(a) + fy * math.sin(a); y = cy - fx * math.sin(a) + fy * math.cos(a)
                    h = height(T, G, x, y)
                    if h is None: ok = False; break
                    zs.append(h)
                if not ok: continue
                spread = max(zs) - min(zs)
                if spread > 60: continue
                dd = min([math.hypot(cx - x, cy - y) for x, y in doors] or [9999])
                if dd < 300: continue
                score = math.hypot(dx, dy) * 0.4 + spread * 6 - min(dd, 900) * 0.2
                best.append((score, cx, cy, round(hc), yaw, round(spread), round(dd)))
    best.sort()
    # keep distinct spots
    pick = []
    for b in best:
        if all(math.hypot(b[1] - p[1], b[2] - p[2]) > 500 for p in pick): pick.append(b)
        if len(pick) >= 4: break
    res[k] = pick
    print(k, len(best), pick)
json.dump(res, open('spots_%s.json'%K, 'w'))
