// nifpca <nif> : every shape's vertices in the model's space; their middle, their long axis (and the next two), and
// their reach along each - how to hold a model whichever way its maker pointed it
#include "NifFile.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace nifly;
static void jacobi(double a[3][3], double v[3][3], double d[3]) {
	for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) v[i][j] = i == j;
	for (int it = 0; it < 60; ++it) {
		int p = 0, q = 1; double m = std::fabs(a[0][1]);
		if (std::fabs(a[0][2]) > m) p = 0, q = 2, m = std::fabs(a[0][2]);
		if (std::fabs(a[1][2]) > m) p = 1, q = 2, m = std::fabs(a[1][2]);
		if (m < 1e-12) break;
		double th = 0.5 * std::atan2(2 * a[p][q], a[q][q] - a[p][p]), c = std::cos(th), s = std::sin(th);
		for (int k = 0; k < 3; ++k) { double akp = a[k][p], akq = a[k][q]; a[k][p] = c * akp - s * akq; a[k][q] = s * akp + c * akq; }
		for (int k = 0; k < 3; ++k) { double apk = a[p][k], aqk = a[q][k]; a[p][k] = c * apk - s * aqk; a[q][k] = s * apk + c * aqk; }
		for (int k = 0; k < 3; ++k) { double vkp = v[k][p], vkq = v[k][q]; v[k][p] = c * vkp - s * vkq; v[k][q] = s * vkp + c * vkq; }
	}
	for (int i = 0; i < 3; ++i) d[i] = a[i][i];
}
int main(int argc, char** argv) {
	NifFile nif; if (nif.Load(argv[1])) { printf("error\n"); return 1; }
	std::vector<Vector3> P;
	for (auto* s : nif.GetShapes()) {
		auto* vs = nif.GetVertsForShape(s); if (!vs) continue;
		MatTransform m; nif.GetNodeTransformToGlobal(s->name.get(), m);
		for (auto& p : *vs) P.push_back(m.ApplyTransform(p));
	}
	if (P.empty()) { printf("none\n"); return 1; }
	double mu[3] = {0, 0, 0};
	for (auto& p : P) mu[0] += p.x, mu[1] += p.y, mu[2] += p.z;
	for (double& x : mu) x /= P.size();
	double C[3][3] = {};
	for (auto& p : P) { double d[3] = {p.x - mu[0], p.y - mu[1], p.z - mu[2]}; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) C[i][j] += d[i] * d[j]; }
	double V[3][3], D[3]; jacobi(C, V, D);
	int o[3] = {0, 1, 2};
	for (int i = 0; i < 3; ++i) for (int j = i + 1; j < 3; ++j) if (D[o[j]] > D[o[i]]) std::swap(o[i], o[j]);
	// the box's middle along each axis (not the vertices' mean: a heavy head would pull it)
	printf("verts %zu\n", P.size());
	for (int k = 0; k < 3; ++k) {
		double ax[3] = {V[0][o[k]], V[1][o[k]], V[2][o[k]]}, lo = 1e9, hi = -1e9;
		for (auto& p : P) { double t = p.x * ax[0] + p.y * ax[1] + p.z * ax[2]; lo = std::min(lo, t), hi = std::max(hi, t); }
		printf("axis%d %.4f %.4f %.4f lo %.2f hi %.2f\n", k, ax[0], ax[1], ax[2], lo, hi);
	}
	double lo[3] = {1e9, 1e9, 1e9}, hi[3] = {-1e9, -1e9, -1e9};
	for (auto& p : P) { double q[3] = {p.x, p.y, p.z}; for (int i = 0; i < 3; ++i) lo[i] = std::min(lo[i], q[i]), hi[i] = std::max(hi[i], q[i]); }
	printf("box %.2f %.2f %.2f .. %.2f %.2f %.2f\n", lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
}
