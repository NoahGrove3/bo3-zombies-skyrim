"""python3 tools/dragonpreview.py <dir of f00.txt..> <out.png> : a contact sheet of the dragon glider's loop (flat-shaded, two views)"""
import sys, glob, numpy as np
from PIL import Image, ImageDraw
d = sys.argv[1]
F = None
frames = []
for fn in sorted(glob.glob(d + '/f*.txt')):
    V = []; T = []
    for l in open(fn):
        if l[0] == 'v': V.append([float(x) for x in l.split()[1:4]])
        elif l[0] == 'f': T.append([int(x) for x in l.split()[1:4]])
    if T: F = np.array(T)
    frames.append(np.array(V))
def render(V, yaw, pitch, W=360, H=240):
    c = np.array([0, -1600, 150.0])
    P = V - c
    cy, sy = np.cos(yaw), np.sin(yaw); cp, sp = np.cos(pitch), np.sin(pitch)
    x = P[:, 0] * cy - P[:, 1] * sy; y = P[:, 0] * sy + P[:, 1] * cy; z = P[:, 2]
    y2 = y * cp - z * sp; z2 = y * sp + z * cp
    s = W / 2000.0
    sx = W / 2 + x * s; syy = H / 2 - z2 * s
    tri = F
    n = np.cross(V[tri[:, 1]] - V[tri[:, 0]], V[tri[:, 2]] - V[tri[:, 0]])
    n /= np.linalg.norm(n, axis=1)[:, None] + 1e-9
    L = np.array([0.3, 0.4, 0.86]); sh = np.abs(n @ L) * 0.75 + 0.2
    dep = y2[tri].mean(1); order = np.argsort(-dep)
    im = Image.new('RGB', (W, H), (25, 30, 40)); dr = ImageDraw.Draw(im)
    for i in order:
        t = tri[i]; g = int(255 * sh[i])
        dr.polygon([(sx[k], syy[k]) for k in t], fill=(g, g, int(g * 1.05)))
    return im
sheet = Image.new('RGB', (360 * 6, 240 * 8))
for i, V in enumerate(frames[:24]):
    a = render(V, np.radians(180 + 35), np.radians(-25))   # from behind, above, to one side
    b = render(V, np.radians(90), np.radians(-5))          # from the side
    sheet.paste(a, ((i % 6) * 360, (i // 6) * 480)); sheet.paste(b, ((i % 6) * 360, (i // 6) * 480 + 240))
sheet.save(sys.argv[2])
