// simplify <in.obj> <out.obj> <target triangles> [aggressiveness=7]
// Quadric-error mesh simplification (Garland-Heckbert, iterative threshold variant after Sven Forstmann's
// "Fast Quadric Mesh Simplification"). Keeps one UV per vertex (objexp writes vt index == v index).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct V3 { double x = 0, y = 0, z = 0; };
static V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static V3 operator*(V3 a, double s) { return { a.x * s, a.y * s, a.z * s }; }
static V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 norm(V3 a) { double l = std::sqrt(dot(a, a)); return l > 0 ? a * (1 / l) : a; }

struct Q {
	double m[10] = {};
	Q() {}
	Q(double a, double b, double c, double d) { m[0] = a * a; m[1] = a * b; m[2] = a * c; m[3] = a * d; m[4] = b * b; m[5] = b * c; m[6] = b * d; m[7] = c * c; m[8] = c * d; m[9] = d * d; }
	Q operator+(const Q& o) const { Q r; for (int i = 0; i < 10; ++i) r.m[i] = m[i] + o.m[i]; return r; }
	double det(int a11, int a12, int a13, int a21, int a22, int a23, int a31, int a32, int a33) const {
		return m[a11] * m[a22] * m[a33] + m[a13] * m[a21] * m[a32] + m[a12] * m[a23] * m[a31] - m[a13] * m[a22] * m[a31] - m[a11] * m[a23] * m[a32] - m[a12] * m[a21] * m[a33]; }
};
struct Tri { int v[3]; double err[4]; bool deleted = false, dirty = false; V3 n; };
struct Vert { V3 p; double u = 0, v = 0; int tstart = 0, tcount = 0; Q q; bool border = false; };
struct Ref { int tid, tvertex; };
static std::vector<Tri> T; static std::vector<Vert> Vs; static std::vector<Ref> R;

static double vertErr(const Q& q, V3 p) {
	const double* m = q.m;
	return m[0] * p.x * p.x + 2 * m[1] * p.x * p.y + 2 * m[2] * p.x * p.z + 2 * m[3] * p.x + m[4] * p.y * p.y + 2 * m[5] * p.y * p.z + 2 * m[6] * p.y + m[7] * p.z * p.z + 2 * m[8] * p.z + m[9];
}
static double edgeErr(int i, int j, V3& out) {
	Q q = Vs[i].q + Vs[j].q;
	bool border = Vs[i].border && Vs[j].border;
	double det = q.det(0, 1, 2, 1, 4, 5, 2, 5, 7);
	if (det != 0 && !border) {
		out.x = -1 / det * q.det(1, 2, 3, 4, 5, 6, 5, 7, 8);
		out.y = 1 / det * q.det(0, 2, 3, 1, 5, 6, 2, 7, 8);
		out.z = -1 / det * q.det(0, 1, 3, 1, 4, 6, 2, 5, 8);
		return vertErr(q, out);
	}
	V3 p1 = Vs[i].p, p2 = Vs[j].p, p3 = (p1 + p2) * 0.5;
	double e1 = vertErr(q, p1), e2 = vertErr(q, p2), e3 = vertErr(q, p3);
	double e = std::min(e1, std::min(e2, e3));
	out = e == e1 ? p1 : e == e2 ? p2 : p3;
	return e;
}
static bool flipped(V3 p, int i0, int i1, Vert& v0, std::vector<int>& deleted) {
	for (int k = 0; k < v0.tcount; ++k) {
		Tri& t = T[R[v0.tstart + k].tid];
		if (t.deleted) continue;
		int s = R[v0.tstart + k].tvertex, id1 = t.v[(s + 1) % 3], id2 = t.v[(s + 2) % 3];
		if (id1 == i1 || id2 == i1) { deleted[k] = 1; continue; }
		V3 d1 = norm(Vs[id1].p - p), d2 = norm(Vs[id2].p - p);
		if (std::fabs(dot(d1, d2)) > 0.999) return true;
		V3 n = norm(cross(d1, d2));
		deleted[k] = 0;
		if (dot(n, t.n) < 0.2) return true;
	}
	return false;
}
static void updateTris(int i0, Vert& v, std::vector<int>& deleted, int& dcount) {
	V3 p;
	for (int k = 0; k < v.tcount; ++k) {
		Ref& r = R[v.tstart + k]; Tri& t = T[r.tid];
		if (t.deleted) continue;
		if (deleted[k]) { t.deleted = true; ++dcount; continue; }
		t.v[r.tvertex] = i0; t.dirty = true;
		t.err[0] = edgeErr(t.v[0], t.v[1], p); t.err[1] = edgeErr(t.v[1], t.v[2], p); t.err[2] = edgeErr(t.v[2], t.v[0], p);
		t.err[3] = std::min(t.err[0], std::min(t.err[1], t.err[2]));
		R.push_back(r);
	}
}
static void updateMesh(int iter) {
	if (iter > 0) { int dst = 0; for (auto& t : T) if (!t.deleted) T[dst++] = t; T.resize(dst); }
	for (auto& v : Vs) v.tstart = 0, v.tcount = 0;
	for (auto& t : T) for (int j = 0; j < 3; ++j) Vs[t.v[j]].tcount++;
	int ts = 0; for (auto& v : Vs) { v.tstart = ts; ts += v.tcount; v.tcount = 0; }
	R.resize(T.size() * 3);
	for (size_t i = 0; i < T.size(); ++i) for (int j = 0; j < 3; ++j) { Vert& v = Vs[T[i].v[j]]; R[v.tstart + v.tcount] = { int(i), j }; v.tcount++; }
	if (iter == 0) {
		std::vector<int> vcount, vids;
		for (auto& v : Vs) v.border = false;
		for (auto& v : Vs) {
			vcount.clear(); vids.clear();
			for (int j = 0; j < v.tcount; ++j) {
				Tri& t = T[R[v.tstart + j].tid];
				for (int k = 0; k < 3; ++k) {
					size_t ofs = 0; int id = t.v[k];
					while (ofs < vcount.size()) { if (vids[ofs] == id) break; ++ofs; }
					if (ofs == vcount.size()) { vcount.push_back(1); vids.push_back(id); } else vcount[ofs]++;
				}
			}
			for (size_t j = 0; j < vcount.size(); ++j) if (vcount[j] == 1) Vs[vids[j]].border = true;
		}
		for (auto& v : Vs) v.q = Q();
		for (auto& t : T) {
			V3 p[3]; for (int j = 0; j < 3; ++j) p[j] = Vs[t.v[j]].p;
			V3 n = norm(cross(p[1] - p[0], p[2] - p[0])); t.n = n;
			for (int j = 0; j < 3; ++j) Vs[t.v[j]].q = Vs[t.v[j]].q + Q(n.x, n.y, n.z, -dot(n, p[0]));
		}
		for (auto& t : T) { V3 p; for (int j = 0; j < 3; ++j) t.err[j] = edgeErr(t.v[j], t.v[(j + 1) % 3], p); t.err[3] = std::min(t.err[0], std::min(t.err[1], t.err[2])); }
	}
}
int main(int argc, char** argv) {
	if (argc < 4) { fprintf(stderr, "simplify in.obj out.obj target [aggr]\n"); return 1; }
	int target = atoi(argv[3]); double aggr = argc > 4 ? atof(argv[4]) : 7;
	std::ifstream in(argv[1]); std::string l; std::vector<std::pair<double, double>> uv;
	while (std::getline(in, l)) {
		if (l.size() < 2) continue;
		if (l[0] == 'v' && l[1] == ' ') { Vert v; sscanf(l.c_str() + 2, "%lf %lf %lf", &v.p.x, &v.p.y, &v.p.z); Vs.push_back(v); }
		else if (l[0] == 'v' && l[1] == 't') { double a, b; sscanf(l.c_str() + 3, "%lf %lf", &a, &b); uv.push_back({ a, b }); }
		else if (l[0] == 'f' && l[1] == ' ') {
			Tri t; std::istringstream ss(l.substr(2)); std::string tok;
			for (int j = 0; j < 3 && ss >> tok; ++j) t.v[j] = atoi(tok.c_str()) - 1;
			T.push_back(t);
		}
	}
	for (size_t i = 0; i < Vs.size() && i < uv.size(); ++i) Vs[i].u = uv[i].first, Vs[i].v = uv[i].second;
	int start = T.size(), deletedTris = 0;
	std::vector<int> del0, del1;
	for (int iter = 0; iter < 100; ++iter) {
		if (start - deletedTris <= target) break;
		if (iter % 5 == 0) updateMesh(iter);
		for (auto& t : T) t.dirty = false;
		double threshold = 0.000000001 * std::pow(double(iter + 3), aggr);
		for (auto& t : T) {
			if (t.err[3] > threshold || t.deleted || t.dirty) continue;
			for (int j = 0; j < 3; ++j) {
				if (t.err[j] >= threshold) continue;
				int i0 = t.v[j], i1 = t.v[(j + 1) % 3];
				Vert &v0 = Vs[i0], &v1 = Vs[i1];
				if (v0.border != v1.border) continue;
				V3 p; edgeErr(i0, i1, p);
				del0.assign(v0.tcount, 0); del1.assign(v1.tcount, 0);
				if (flipped(p, i0, i1, v0, del0) || flipped(p, i1, i0, v1, del1)) continue;
				// keep the uv of the nearer endpoint
				double d0 = dot(p - v0.p, p - v0.p), d1 = dot(p - v1.p, p - v1.p);
				if (d1 < d0) v0.u = v1.u, v0.v = v1.v;
				v0.p = p; v0.q = v1.q + v0.q;
				int tstart = R.size();
				updateTris(i0, v0, del0, deletedTris);
				updateTris(i0, v1, del1, deletedTris);
				int tcount = R.size() - tstart;
				if (tcount <= v0.tcount) { if (tcount) memcpy(&R[v0.tstart], &R[tstart], tcount * sizeof(Ref)); }
				else v0.tstart = tstart;
				v0.tcount = tcount;
				break;
			}
			if (start - deletedTris <= target) break;
		}
	}
	// compact
	std::vector<int> remap(Vs.size(), -1); int nv = 0;
	std::ofstream out(argv[2]); out << "o mesh\nusemtl mesh\n";
	for (auto& t : T) if (!t.deleted) for (int j = 0; j < 3; ++j) if (remap[t.v[j]] < 0) remap[t.v[j]] = nv++;
	std::vector<int> order(nv); for (size_t i = 0; i < Vs.size(); ++i) if (remap[i] >= 0) order[remap[i]] = i;
	char buf[128];
	for (int i : order) { snprintf(buf, sizeof buf, "v %.6f %.6f %.6f\n", Vs[i].p.x, Vs[i].p.y, Vs[i].p.z); out << buf; }
	for (int i : order) { snprintf(buf, sizeof buf, "vt %.6f %.6f\n", Vs[i].u, Vs[i].v); out << buf; }
	int nt = 0;
	for (auto& t : T) if (!t.deleted) { snprintf(buf, sizeof buf, "f %d/%d %d/%d %d/%d\n", remap[t.v[0]] + 1, remap[t.v[0]] + 1, remap[t.v[1]] + 1, remap[t.v[1]] + 1, remap[t.v[2]] + 1, remap[t.v[2]] + 1); out << buf; ++nt; }
	printf("simplified %d -> %d triangles, %d verts\n", start, nt, nv);
}
