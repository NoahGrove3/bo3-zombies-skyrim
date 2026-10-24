// xunwrap <in.obj> <out.obj> <resolution> [padding]: charts and packs a UV atlas (xatlas); writes v vt vn f (vn: smooth, welded)
#include "xatlas.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
int main(int argc, char** argv)
{
	std::ifstream f(argv[1]); std::string l; std::vector<float> P; std::vector<uint32_t> F;
	while (std::getline(f, l)) {
		if (l.size() > 2 && l[0] == 'v' && l[1] == ' ') { std::istringstream s(l.substr(2)); float x, y, z; s >> x >> y >> z; P.insert(P.end(), { x, y, z }); }
		else if (l.size() > 2 && l[0] == 'f' && l[1] == ' ') { std::istringstream s(l.substr(2)); std::string t; for (int k = 0; k < 3; k++) { s >> t; F.push_back((uint32_t)std::atoi(t.c_str()) - 1); } }
	}
	const size_t nv = P.size() / 3;
	std::vector<float> N(P.size(), 0.0f);
	for (size_t t = 0; t < F.size(); t += 3) {
		const float* a = &P[F[t] * 3]; const float* b = &P[F[t + 1] * 3]; const float* c = &P[F[t + 2] * 3];
		float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
		float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
		for (int k = 0; k < 3; k++) for (int j = 0; j < 3; j++) N[F[t + k] * 3 + j] += n[j];
	}
	for (size_t v = 0; v < nv; v++) { float* n = &N[v * 3]; float L = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]); if (L > 0) for (int j = 0; j < 3; j++) n[j] /= L; }
	xatlas::Atlas* at = xatlas::Create();
	xatlas::MeshDecl md; md.vertexCount = (uint32_t)nv; md.vertexPositionData = P.data(); md.vertexPositionStride = 12;
	md.vertexNormalData = N.data(); md.vertexNormalStride = 12;
	md.indexCount = (uint32_t)F.size(); md.indexData = F.data(); md.indexFormat = xatlas::IndexFormat::UInt32;
	if (xatlas::AddMesh(at, md) != xatlas::AddMeshError::Success) { fprintf(stderr, "addmesh failed\n"); return 1; }
	xatlas::ChartOptions co; co.maxIterations = 2;
	xatlas::PackOptions po; po.resolution = (uint32_t)std::atoi(argv[3]); po.padding = argc > 4 ? std::atoi(argv[4]) : 4; po.bilinear = true; po.blockAlign = true; po.bruteForce = false;
	xatlas::Generate(at, co, po);
	printf("charts %u atlases %u size %ux%u util %.2f\n", at->chartCount, at->atlasCount, at->width, at->height, at->utilization ? at->utilization[0] : 0.f);
	const xatlas::Mesh& m = at->meshes[0];
	FILE* o = fopen(argv[2], "w");
	for (uint32_t i = 0; i < m.vertexCount; i++) { const auto& v = m.vertexArray[i]; const float* p = &P[v.xref * 3]; fprintf(o, "v %.5f %.5f %.5f\n", p[0], p[1], p[2]); }
	for (uint32_t i = 0; i < m.vertexCount; i++) { const auto& v = m.vertexArray[i]; fprintf(o, "vt %.6f %.6f\n", v.uv[0] / at->width, 1.0f - v.uv[1] / at->height); }
	for (uint32_t i = 0; i < m.vertexCount; i++) { const auto& v = m.vertexArray[i]; const float* n = &N[v.xref * 3]; fprintf(o, "vn %.5f %.5f %.5f\n", n[0], n[1], n[2]); }
	for (uint32_t t = 0; t < m.indexCount; t += 3) fprintf(o, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", m.indexArray[t] + 1, m.indexArray[t] + 1, m.indexArray[t] + 1, m.indexArray[t + 1] + 1, m.indexArray[t + 1] + 1, m.indexArray[t + 1] + 1, m.indexArray[t + 2] + 1, m.indexArray[t + 2] + 1, m.indexArray[t + 2] + 1);
	fclose(o);
	xatlas::Destroy(at);
}
