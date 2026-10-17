// meshrender <mesh.bin> <tex.rgb|-> <texW> <texH> <out.ppm> <W> <H> ex ey ez lx ly lz [fov]
// A quick textured preview: z-buffered triangles, perspective camera, per-pixel uv and smooth normals, sun + sky light.
// mesh.bin: int32 nv, int32 nt, then nv * (x y z nx ny nz u v) float32, nt * 3 int32 (tools/dragonbuild.py writes it).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <string>
struct V3 { float x, y, z; };
static V3 sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static V3 norm(V3 a) { float l = std::sqrt(dot(a, a)) + 1e-12f; return { a.x / l, a.y / l, a.z / l }; }
int main(int argc, char** argv)
{
	if (argc < 14) { fprintf(stderr, "usage\n"); return 1; }
	FILE* f = fopen(argv[1], "rb");
	int nv, nt;
	fread(&nv, 4, 1, f); fread(&nt, 4, 1, f);
	std::vector<float> vd(nv * 8);
	std::vector<int> td(nt * 3);
	fread(vd.data(), 4, vd.size(), f); fread(td.data(), 4, td.size(), f);
	fclose(f);
	int tw = atoi(argv[3]), th = atoi(argv[4]);
	std::vector<unsigned char> tex;
	if (std::string(argv[2]) != "-") { tex.resize(tw * th * 3); FILE* t = fopen(argv[2], "rb"); fread(tex.data(), 1, tex.size(), t); fclose(t); }
	int W = atoi(argv[6]), H = atoi(argv[7]);
	V3 eye{ (float)atof(argv[8]), (float)atof(argv[9]), (float)atof(argv[10]) }, look{ (float)atof(argv[11]), (float)atof(argv[12]), (float)atof(argv[13]) };
	float fov = argc > 14 ? atof(argv[14]) : 40.0f;
	V3 fw = norm(sub(look, eye)), rt = norm(cross(fw, { 0, 0, 1 })), up = cross(rt, fw);
	float fl = (W / 2) / std::tan(fov * 3.14159f / 360);
	std::vector<float> zb(W * H, 1e30f);
	std::vector<float> img(W * H * 3);
	for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
		float k = (float)y / H;
		img[(y * W + x) * 3 + 0] = 70 + 60 * (1 - k); img[(y * W + x) * 3 + 1] = 85 + 70 * (1 - k); img[(y * W + x) * 3 + 2] = 105 + 90 * (1 - k);
	}
	V3 sun = norm({ 0.35f, -0.45f, 0.82f });
	std::vector<float> sx(nv), sy(nv), sz(nv);
	for (int i = 0; i < nv; ++i) {
		V3 p = sub({ vd[i * 8], vd[i * 8 + 1], vd[i * 8 + 2] }, eye);
		float z = dot(p, fw);
		sz[i] = z;
		sx[i] = W / 2 + dot(p, rt) / std::max(z, 1e-3f) * fl;
		sy[i] = H / 2 - dot(p, up) / std::max(z, 1e-3f) * fl;
	}
	for (int t = 0; t < nt; ++t) {
		int a = td[t * 3], b = td[t * 3 + 1], c = td[t * 3 + 2];
		if (sz[a] < 5 || sz[b] < 5 || sz[c] < 5) continue;
		float d = (sy[b] - sy[c]) * (sx[a] - sx[c]) + (sx[c] - sx[b]) * (sy[a] - sy[c]);
		if (std::fabs(d) < 1e-9f) continue;
		int x0 = std::max(0, (int)std::floor(std::min({ sx[a], sx[b], sx[c] }))), x1 = std::min(W - 1, (int)std::ceil(std::max({ sx[a], sx[b], sx[c] })));
		int y0 = std::max(0, (int)std::floor(std::min({ sy[a], sy[b], sy[c] }))), y1 = std::min(H - 1, (int)std::ceil(std::max({ sy[a], sy[b], sy[c] })));
		for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
			float px = x + 0.5f, py = y + 0.5f;
			float w0 = ((sy[b] - sy[c]) * (px - sx[c]) + (sx[c] - sx[b]) * (py - sy[c])) / d;
			float w1 = ((sy[c] - sy[a]) * (px - sx[c]) + (sx[a] - sx[c]) * (py - sy[c])) / d;
			float w2 = 1 - w0 - w1;
			if (w0 < 0 || w1 < 0 || w2 < 0) continue;
			// perspective-correct
			float iz = w0 / sz[a] + w1 / sz[b] + w2 / sz[c];
			float z = 1 / iz;
			if (z >= zb[y * W + x]) continue;
			zb[y * W + x] = z;
			float q0 = w0 / sz[a] * z, q1 = w1 / sz[b] * z, q2 = w2 / sz[c] * z;
			auto at = [&](int k) { return q0 * vd[a * 8 + k] + q1 * vd[b * 8 + k] + q2 * vd[c * 8 + k]; };
			V3 n = norm({ at(3), at(4), at(5) });
			V3 pos{ at(0), at(1), at(2) };
			V3 toEye = norm(sub(eye, pos));
			if (dot(n, toEye) < 0) n = { -n.x, -n.y, -n.z };
			float lam = std::max(0.0f, dot(n, sun));
			float sky = 0.5f + 0.5f * n.z;
			float rim = std::pow(1 - std::max(0.0f, dot(n, toEye)), 3.0f);
			float sh = 0.25f + 0.85f * lam + 0.25f * sky + 0.25f * rim;
			float r = 200, g = 200, bl = 205;
			if (!tex.empty()) {
				float u = at(6), v = at(7);
				u -= std::floor(u), v -= std::floor(v);
				int tx = std::min(tw - 1, (int)(u * tw)), ty = std::min(th - 1, (int)(v * th));
				r = tex[(ty * tw + tx) * 3], g = tex[(ty * tw + tx) * 3 + 1], bl = tex[(ty * tw + tx) * 3 + 2];
			}
			img[(y * W + x) * 3] = std::min(255.0f, r * sh);
			img[(y * W + x) * 3 + 1] = std::min(255.0f, g * sh);
			img[(y * W + x) * 3 + 2] = std::min(255.0f, bl * sh);
		}
	}
	FILE* o = fopen(argv[5], "wb");
	fprintf(o, "P6 %d %d 255\n", W, H);
	for (float v : img) fputc((int)v, o);
	fclose(o);
}
