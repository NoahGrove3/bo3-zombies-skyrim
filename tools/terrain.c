/* The main menu's far ranges (tools/menumountains.py loads this through ctypes):
 *   erode()  - hydraulic erosion by rain droplets: gullies, ridges and fans carved into the height-field, and a flow
 *              map (where the water ran) for the rock and snow streaks;
 *   render() - a per-pixel ray-marcher over the height-field with a procedural spruce forest standing on it, soft
 *              shadows from the moon, snow where it would lie, sky light, and the night air between us and the peaks.
 * Build: gcc -O3 -fopenmp -shared -fPIC -o terrain.so terrain.c -lm */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float smooth(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
static inline uint32_t hash2(int x, int y, uint32_t s) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static inline float h01(int x, int y, uint32_t s) { return (hash2(x, y, s) & 0xFFFFFF) / 16777216.0f; }
static float vnoise(float x, float y, uint32_t s) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = h01(ix, iy, s), b = h01(ix + 1, iy, s), c = h01(ix, iy + 1, s), d = h01(ix + 1, iy + 1, s);
    return mixf(mixf(a, b, fx), mixf(c, d, fx), fy);
}
static float fbm(float x, float y, int oct, uint32_t s) {
    float v = 0, a = 0.5f, t = 0;
    for (int i = 0; i < oct; ++i) { v += a * vnoise(x, y, s + i * 31); t += a; x *= 2.03f; y *= 2.03f; a *= 0.5f; }
    return v / t;
}

/* ---------------------------------------------------------------------------------------------- erosion */
static inline void grad_h(const float* h, int n, float x, float y, float* hh, float* gx, float* gy) {
    int ix = (int)x, iy = (int)y;
    float fx = x - ix, fy = y - iy;
    float a = h[iy * n + ix], b = h[iy * n + ix + 1], c = h[(iy + 1) * n + ix], d = h[(iy + 1) * n + ix + 1];
    *gx = (b - a) * (1 - fy) + (d - c) * fy;
    *gy = (c - a) * (1 - fx) + (d - b) * fx;
    *hh = a * (1 - fx) * (1 - fy) + b * fx * (1 - fy) + c * (1 - fx) * fy + d * fx * fy;
}

void erode(float* h, float* flow, int n, int drops, unsigned seed, float scale) {
    /* h in cells (height / cell size); classic droplet erosion (after Hans Beyer / S. Lague) with a radius-3 brush */
    const float inertia = 0.10f, capK = 2.0f, minCap = 0.01f, depK = 0.30f, eroK = 0.05f, evap = 0.02f, grav = 4.0f;
    const int life = 64, R = 3;
    float bw[49]; int bx[49], by[49], nb = 0; float bsum = 0;
    for (int j = -R; j <= R; ++j)
        for (int i = -R; i <= R; ++i) {
            float d = sqrtf((float)(i * i + j * j));
            if (d <= R) { bx[nb] = i; by[nb] = j; bw[nb] = 1 - d / R; bsum += bw[nb]; ++nb; }
        }
    for (int i = 0; i < nb; ++i) bw[i] /= bsum;
    uint32_t st = seed;
    for (int k = 0; k < drops; ++k) {
        st = st * 1664525u + 1013904223u; float x = 1 + (st >> 8) / 16777216.0f * (n - 3);
        st = st * 1664525u + 1013904223u; float y = 1 + (st >> 8) / 16777216.0f * (n - 3);
        float dx = 0, dy = 0, sp = 1, water = 1, sed = 0;
        for (int t = 0; t < life; ++t) {
            int ix = (int)x, iy = (int)y;
            float fx = x - ix, fy = y - iy, hh, gx, gy;
            grad_h(h, n, x, y, &hh, &gx, &gy);
            dx = dx * inertia - gx * (1 - inertia);
            dy = dy * inertia - gy * (1 - inertia);
            float len = sqrtf(dx * dx + dy * dy);
            if (len < 1e-6f) break;
            dx /= len; dy /= len;
            float nx = x + dx, ny = y + dy;
            if (nx < 1 || ny < 1 || nx >= n - 2 || ny >= n - 2) break;
            float nh, ngx, ngy;
            grad_h(h, n, nx, ny, &nh, &ngx, &ngy);
            float dh = nh - hh;
            float cap = fmaxf(-dh * sp * water * capK, minCap);
            if (sed > cap || dh > 0) {
                float dep = dh > 0 ? fminf(dh, sed) : (sed - cap) * depK;
                sed -= dep;
                h[iy * n + ix] += dep * (1 - fx) * (1 - fy);
                h[iy * n + ix + 1] += dep * fx * (1 - fy);
                h[(iy + 1) * n + ix] += dep * (1 - fx) * fy;
                h[(iy + 1) * n + ix + 1] += dep * fx * fy;
            } else {
                float er = fminf((cap - sed) * eroK, -dh);
                for (int b = 0; b < nb; ++b) {
                    int cx = ix + bx[b], cy = iy + by[b];
                    if (cx < 0 || cy < 0 || cx >= n || cy >= n) continue;
                    float w = er * bw[b], cur = h[cy * n + cx];
                    float take = cur < w ? cur : w;
                    h[cy * n + cx] -= take;
                    sed += take;
                }
            }
            flow[iy * n + ix] += water * sp * scale;
            sp = fminf(sqrtf(fmaxf(sp * sp - dh * grav, 0)), 3.0f);
            water *= 1 - evap;
            x = nx; y = ny;
        }
    }
}

/* ---------------------------------------------------------------------------------------------- the scene */
typedef struct {
    const float *h, *flow, *ao;
    int n;
    float X0, X1, Z0, Z1, hmax;
    float camY, F;
    int W, H, horizon;
    float L[3], Lc[3], sky[3], haze[3], fogD, treeline, snowline, bump;
} Scene;

static inline float hf(const Scene* s, const float* m, float x, float z) {
    float u = (x - s->X0) / (s->X1 - s->X0) * (s->n - 1), v = (z - s->Z0) / (s->Z1 - s->Z0) * (s->n - 1);
    u = clampf(u, 0, s->n - 1.001f);
    v = clampf(v, 0, s->n - 1.001f);
    int iu = (int)u, iv = (int)v; float fu = u - iu, fv = v - iv;
    const float* r0 = m + iv * s->n + iu; const float* r1 = r0 + s->n;
    return (r0[0] * (1 - fu) + r0[1] * fu) * (1 - fv) + (r1[0] * (1 - fu) + r1[1] * fu) * fv;
}
static inline float ground(const Scene* s, float x, float z) { return hf(s, s->h, x, z); }

/* the forest: one spruce per jittered 8-unit cell where the ground suits it (below the treeline, not too steep) */
#define TCELL 8.0f
static float forest_mask(const Scene* s, float x, float z, float g) {
    float tl = s->treeline + 260 * (fbm(x * 0.0009f, z * 0.0009f, 3, 401) - 0.5f);
    float m = smooth(tl, tl - 120, g) * smooth(0.0f, 30.0f, g);
    float gx = ground(s, x + 12, z) - ground(s, x - 12, z), gz = ground(s, x, z + 12) - ground(s, x, z - 12);
    float slope = sqrtf(gx * gx + gz * gz) / 24.0f;
    m *= smooth(1.1f, 0.6f, slope);
    m *= smooth(0.22f, 0.40f, fbm(x * 0.0022f, z * 0.0022f, 4, 409));   /* groves and clearings */
    return m;
}
/* height of the canopy at (x,z) above the ground (0 = none); also the tree's centre and size for its normal */
static float canopy(const Scene* s, float x, float z, float g, float* cx, float* cz, float* th, float* tr, uint32_t* id) {
    int ci = (int)floorf(x / TCELL), cj = (int)floorf(z / TCELL);
    float best = 0;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            int a = ci + i, b = cj + j;
            float px = (a + 0.15f + 0.7f * h01(a, b, 11)) * TCELL, pz = (b + 0.15f + 0.7f * h01(a, b, 12)) * TCELL;
            float m = forest_mask(s, px, pz, g);
            if (m <= 0.02f || h01(a, b, 13) > m * 0.92f) continue;
            float H = 13 + 19 * h01(a, b, 14) * h01(a, b, 15) + 6 * m;
            float R = H * (0.20f + 0.07f * h01(a, b, 16));
            float d = sqrtf((x - px) * (x - px) + (z - pz) * (z - pz));
            if (d >= R) continue;
            /* a spruce: a narrow cone, ragged at its edge (the branch tiers) */
            float tier = 0.82f + 0.18f * fabsf(sinf((1 - d / R) * 9.0f + h01(a, b, 17) * 6.28f));
            float v = H * (1 - d / R) * tier + (d < R * 0.12f ? H * 0.06f : 0);
            if (v > best) { best = v; *cx = px; *cz = pz; *th = H; *tr = R; *id = hash2(a, b, 19); }
        }
    return best;
}
static inline float surface(const Scene* s, float x, float z, int trees) {
    float g = ground(s, x, z);
    if (!trees || g > s->treeline + 200) return g;
    float cx, cz, th, tr; uint32_t id;
    return g + canopy(s, x, z, g, &cx, &cz, &th, &tr, &id);
}

static void normal_at(const Scene* s, float x, float z, float e, float* N) {
    float hx = ground(s, x + e, z) - ground(s, x - e, z), hz = ground(s, x, z + e) - ground(s, x, z - e);
    float nx = -hx / (2 * e), nz = -hz / (2 * e);
    /* fine relief the height-field is too coarse to hold: rock buttresses and ribs */
    float b1 = fbm(x * 0.045f, z * 0.045f, 4, 77) - 0.5f, b2 = fbm(x * 0.045f + 3.1f, z * 0.045f + 7.7f, 4, 77) - 0.5f;
    nx += b1 * s->bump; nz += b2 * s->bump;
    float l = sqrtf(nx * nx + 1 + nz * nz);
    N[0] = nx / l; N[1] = 1 / l; N[2] = nz / l;
}

static float shadow(const Scene* s, float x, float y, float z) {
    float t = 6, res = 1;
    for (int i = 0; i < 160 && t < 16000; ++i) {
        float px = x + s->L[0] * t, py = y + s->L[1] * t, pz = z + s->L[2] * t;
        if (py > s->hmax + 50) break;
        if (pz < s->Z0 || pz > s->Z1) break;
        float g = surface(s, px, pz, t < 160);
        float d = py - g;
        if (d < 0) return 0;
        res = fminf(res, 9.0f * d / t);
        t += fmaxf(2.5f, d * 0.35f) + t * 0.01f;
    }
    return clampf(res, 0, 1);
}

static void shade(const Scene* s, float x, float y, float z, int isTree, float tcx, float tcz, float tth, float ttr, uint32_t tid, float* out) {
    float N[3], g = ground(s, x, z);
    float fl = hf(s, s->flow, x, z), ao = hf(s, s->ao, x, z);
    float alb[3];
    float snow;
    if (isTree) {
        float dx = x - tcx, dz = z - tcz, d = sqrtf(dx * dx + dz * dz) + 1e-3f;
        float k = tth / ttr;
        N[0] = dx / d * k; N[1] = 1; N[2] = dz / d * k;
        float l = sqrtf(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]); N[0] /= l; N[1] /= l; N[2] /= l;
        float r = (tid & 255) / 255.0f;
        alb[0] = 0.020f + 0.012f * r; alb[1] = 0.034f + 0.014f * r; alb[2] = 0.030f + 0.010f * r;
        /* snow caught on the branch tiers, more of it higher up */
        float tierline = fabsf(sinf((y - g) * 0.9f + r * 6.0f));
        snow = smooth(0.80f, 0.98f, tierline) * (0.10f + 0.30f * smooth(300, 800, g)) * smooth(-0.2f, 0.5f, N[1]);
        ao *= 0.55f + 0.45f * clampf((y - g) / tth, 0, 1);
    } else {
        float e = 6.0f + z * 0.0009f;
        normal_at(s, x, z, e, N);
        /* rock: dark, banded, varied; snow: above a ragged snowline where the slope lets it lie, and down the gullies */
        float strata = 0.5f + 0.5f * sinf(y * 0.06f + 4 * fbm(x * 0.004f, z * 0.004f, 3, 91));
        float var = fbm(x * 0.01f, z * 0.01f, 5, 93);
        float rk = 0.025f + 0.035f * var + 0.015f * strata;
        alb[0] = rk * 1.02f; alb[1] = rk * 0.98f; alb[2] = rk * 0.96f;
        float sl = s->snowline + 380 * (fbm(x * 0.0012f, z * 0.0012f, 4, 95) - 0.5f);
        float slopeOK = smooth(0.50f, 0.74f, N[1]);
        float gully = clampf(fl * 1.0f, 0, 1);
        snow = smooth(sl - 80, sl + 160, g) * clampf(slopeOK + gully * 0.55f * smooth(0.3f, 0.6f, N[1]), 0, 1);
        snow = fmaxf(snow, smooth(sl + 900, sl + 1400, g) * smooth(0.35f, 0.6f, N[1]));
        snow *= 0.75f + 0.25f * fbm(x * 0.03f, z * 0.03f, 3, 97);
        /* the wind strips the crests and the ribs bare; outcrops break through the snowfields */
        snow *= 1 - 0.5f * smooth(1.14f, 1.30f, ao) * smooth(0.85f, 0.6f, N[1]);
        float crop = 0.75f * fbm(x * 0.018f, z * 0.018f, 5, 99) + 0.75f * (1 - N[1]);
        snow *= 1 - 0.9f * smooth(0.62f, 0.70f, crop);
        snow = smooth(0.30f, 0.55f, snow);   /* (snow lies or it doesn't: no grey half-snow) */
        /* below the treeline: the forest floor under snow, dark ground showing through */
        float low = smooth(s->treeline + 100, s->treeline - 150, g);
        snow = fmaxf(snow, low * 0.22f * smooth(0.7f, 0.9f, N[1]));
        alb[0] = mixf(alb[0], 0.05f, low * 0.5f); alb[1] = mixf(alb[1], 0.06f, low * 0.5f); alb[2] = mixf(alb[2], 0.06f, low * 0.5f);
    }
    float sc[3] = { 0.78f, 0.82f, 0.90f };
    for (int c = 0; c < 3; ++c) alb[c] = mixf(alb[c], sc[c], clampf(snow, 0, 1));
    float ndl = N[0] * s->L[0] + N[1] * s->L[1] + N[2] * s->L[2];
    float sh = ndl > 0 ? shadow(s, x, y + 1.5f, z) : 0;
    float wrap = clampf((ndl + 0.12f) / 1.12f, 0, 1);       /* (snow scatters a little light past the terminator) */
    float diff = mixf(clampf(ndl, 0, 1), wrap, snow * 0.6f) * sh;
    float skyv = (0.55f + 0.45f * N[1]) * clampf(ao, 0.25f, 1.1f);
    for (int c = 0; c < 3; ++c)
        out[c] = alb[c] * (s->Lc[c] * diff + s->sky[c] * skyv);
}

void render(const float* h, const float* flow, const float* ao, int n, float X0, float X1, float Z0, float Z1, float hmax,
            float camY, float F, int W, int H, int horizon, int y0, int y1, int ss, const float* P, float* out /* W*H*5: rgb, a, depth */) {
    Scene s = { h, flow, ao, n, X0, X1, Z0, Z1, hmax, camY, F, W, H, horizon,
                { P[0], P[1], P[2] }, { P[3], P[4], P[5] }, { P[6], P[7], P[8] }, { P[9], P[10], P[11] }, P[12], P[13], P[14], P[15] };
    #pragma omp parallel for schedule(dynamic, 1)
    for (int py = y0; py < y1; ++py) {
        for (int px = 0; px < W; ++px) {
            float acc[3] = { 0, 0, 0 }, cov = 0, dep = 1e9f;
            for (int sj = 0; sj < ss; ++sj)
                for (int si = 0; si < ss; ++si) {
                    float fx = px + (si + 0.5f) / ss, fy = py + (sj + 0.5f) / ss;
                    float dx = (fx - W * 0.5f) / F, dy = (horizon - fy) / F;
                    float z = Z0, prevz = Z0, prevd = 1e9f;
                    int hit = 0;
                    while (z < Z1) {
                        float x = dx * z, y = camY + dy * z;
                        if (x < X0 || x > X1) break;
                        if (dy >= 0 && y > hmax + 40) break;
                        if (y < 0) break;                                  /* under the lake's surface: the water shows */
                        float g = ground(&s, x, z);
                        float d = y - g;
                        if (d < 45 && g < s.treeline + 200) d = y - surface(&s, x, z, 1);
                        if (d < 0) { hit = 1; break; }
                        prevz = z; prevd = d;
                        z += fmaxf(z * 0.00035f, d * 0.30f);
                    }
                    if (!hit) continue;
                    /* refine the crossing */
                    float a = prevz, b = z;
                    for (int it = 0; it < 7; ++it) {
                        float m = 0.5f * (a + b), x = dx * m, y = camY + dy * m;
                        if (y - surface(&s, x, m, 1) < 0) b = m; else a = m;
                    }
                    z = b;
                    float x = dx * z, y = camY + dy * z;
                    float g = ground(&s, x, z);
                    float tcx = 0, tcz = 0, tth = 0, ttr = 1; uint32_t tid = 0;
                    int isTree = 0;
                    if (g < s.treeline + 200) {
                        float c = canopy(&s, x, z, g, &tcx, &tcz, &tth, &ttr, &tid);
                        isTree = c > 0.3f && y > g + 0.3f;
                    }
                    float col[3];
                    shade(&s, x, y, z, isTree, tcx, tcz, tth, ttr, tid, col);
                    /* the night air: light lost and the haze's own glow gained, with distance */
                    float f = 1 - expf(-(z - Z0 * 0.5f) / s.fogD);
                    float lowair = expf(-fmaxf(y, 0) / 900.0f);      /* thicker low down */
                    f = clampf(f * (0.55f + 0.65f * lowair), 0, 1);
                    for (int c = 0; c < 3; ++c) acc[c] += col[c] * (1 - f) + s.haze[c] * f;
                    cov += 1; if (z < dep) dep = z;
                    (void)prevd;
                }
            float* o = out + ((size_t)py * W + px) * 5;
            float inv = cov > 0 ? 1.0f / cov : 0;
            o[0] = acc[0] * inv; o[1] = acc[1] * inv; o[2] = acc[2] * inv;
            o[3] = cov / (ss * ss); o[4] = dep;
        }
    }
}
