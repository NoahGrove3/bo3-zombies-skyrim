// capephys <cape.nif> <collision-source.nif> <out.nif> <xml path in game> [cols] [rows]
// (Noah: "make the cape flow as I'm moving around ... similar physics to the hair, instead of just being a straight sheet")
// Gives a cape that hangs stiff on the skirt bones a grid of cloth bones for FSMP (HDT-SMP) to swing:
//  - the cape's hanging part (whatever was weighted to SkirtBBone01-03) is reweighted onto a cols x rows grid of bones
//    (CAPE_C<c>_R<r>), each column a chain hanging from the upper back (NPC Spine2), so it keeps hanging the same way
//    even with physics off;
//  - the body's collision proxies (hidden meshes) are copied from the collision-source nif so the cape slides off the
//    legs instead of through them;
//  - the nif names its physics file ("HDT Skinned Mesh Physics Object"); tools/capephys.py writes that xml.
// Prints the grid (bone name, position) for capephys.py. The output is built from the player's own copies of the meshes,
// so it is deployed, never committed.
#include "NifFile.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using namespace nifly;

int main(int argc, char** argv)
{
	if (argc < 5) { fprintf(stderr, "usage: capephys <cape.nif> <collision.nif> <out.nif> <xmlpath> [cols] [rows]\n"); return 1; }
	const int C = argc > 5 ? std::stoi(argv[5]) : 5, R = argc > 6 ? std::stoi(argv[6]) : 5;
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "cape load failed\n"); return 1; }
	NiShape* cape = nullptr;
	for (auto* s : nif.GetShapes()) if (s->IsSkinned() && (!cape || nif.GetVertsForShape(s)->size() > nif.GetVertsForShape(cape)->size())) cape = s;
	if (!cape) { fprintf(stderr, "no skinned shape\n"); return 1; }
	const std::string capeName = cape->name.get();
	const auto verts = *nif.GetVertsForShape(cape);
	const size_t N = verts.size();
	std::vector<std::string> bones;
	nif.GetShapeBoneList(cape, bones);
	const size_t B0 = bones.size();
	// per vertex weights: [vertex][bone] (old bones)
	std::vector<std::map<int, float>> w(N);
	std::vector<MatTransform> skinToBone(B0);
	for (uint32_t b = 0; b < B0; ++b) {
		std::unordered_map<uint16_t, float> m;
		nif.GetShapeBoneWeights(cape, b, m);
		for (auto& [v, x] : m) if (x > 0) w[v][b] = x;
		nif.GetShapeTransformSkinToBone(cape, b, skinToBone[b]);
	}
	MatTransform globalToSkin;
	nif.CalcShapeTransformGlobalToSkin(cape, globalToSkin);
	const MatTransform skinToGlobal = globalToSkin.InverseTransform();
	auto isSkirt = [&](int b) { return bones[b].rfind("Skirt", 0) == 0; };
	// the hanging part, in the body's (global) space
	std::vector<Vector3> P(N);
	std::vector<float> ws(N, 0);
	float xmin = 1e9, xmax = -1e9, ztop = -1e9, zbot = 1e9;
	for (size_t v = 0; v < N; ++v) {
		P[v] = skinToGlobal.ApplyTransform(verts[v]);
		for (auto& [b, x] : w[v]) if (isSkirt(b)) ws[v] += x;
		if (ws[v] > 0.01f) xmin = std::min(xmin, P[v].x), xmax = std::max(xmax, P[v].x), zbot = std::min(zbot, P[v].z);
		if (ws[v] > 0.5f) ztop = std::max(ztop, P[v].z);
	}
	if (xmin > xmax) { fprintf(stderr, "no skirt-weighted vertices\n"); return 1; }
	printf("cape '%s': %zu verts, hanging part x %.1f..%.1f, z %.1f (top) .. %.1f (hem)\n", capeName.c_str(), N, xmin, xmax, ztop, zbot);
	// the grid: column c at x, row r at height z; y = the cape's own surface there (average of the nearest vertices)
	const float colW = (xmax - xmin) / (C - 1), rowH = (ztop - zbot) / R;
	auto gx = [&](int c) { return xmin + colW * c; };
	auto gz = [&](int r) { return ztop - rowH * r; };
	std::vector<Vector3> G(C * R);
	for (int r = 0; r < R; ++r)
		for (int c = 0; c < C; ++c) {
			float sy = 0, sw = 0;
			for (size_t v = 0; v < N; ++v) {
				if (ws[v] < 0.01f) continue;
				const float dx = (P[v].x - gx(c)) / colW, dz = (P[v].z - gz(r)) / rowH, d2 = dx * dx + dz * dz;
				const float k = std::exp(-d2 * 2.0f);
				sy += P[v].y * k, sw += k;
			}
			// (a little inside the cloth, so the bone sits in it rather than behind it)
			G[r * C + c] = Vector3(gx(c), sw > 0 ? sy / sw + 0.5f : -10.0f, gz(r));
		}
	// the bones: each column a chain under NPC Spine2
	const std::string kSpine = "NPC Spine2 [Spn2]";
	auto* spine = nif.FindBlockByName<NiNode>(kSpine);
	MatTransform spineW;
	if (!spine || !nif.GetNodeTransformToGlobal(kSpine, spineW)) { fprintf(stderr, "no %s node\n", kSpine.c_str()); return 1; }
	std::vector<std::string> gname(C * R);
	std::vector<int> gid(C * R);
	for (int c = 0; c < C; ++c) {
		NiNode* parent = spine;
		MatTransform parentW = spineW;
		for (int r = 0; r < R; ++r) {
			const int i = r * C + c;
			gname[i] = "CAPE_C" + std::to_string(c) + "_R" + std::to_string(r);
			MatTransform world;
			world.translation = G[i];  // (axis-aligned, like the skeleton's own skirt bones' physics frames)
			const MatTransform local = parentW.InverseTransform().ComposeTransforms(world);
			NiNode* node = nif.AddNode(gname[i], local, parent);
			gid[i] = nif.GetBlockID(node);
			parent = node, parentW = world;
			printf("bone %s %.2f %.2f %.2f\n", gname[i].c_str(), G[i].x, G[i].y, G[i].z);
		}
	}
	// new weights: the skirt share of each vertex goes to the grid (bilinear between the 4 bones round it: it hangs from
	// the row above and blends into the next; across, between its two columns)
	std::vector<std::map<int, float>> nw(N);  // bone index in the new list
	for (size_t v = 0; v < N; ++v) {
		for (auto& [b, x] : w[v]) if (!isSkirt(b)) nw[v][b] += x;
		if (ws[v] <= 0) continue;
		const float fx = std::clamp((P[v].x - xmin) / colW, 0.0f, float(C - 1));
		const float fz = std::clamp((ztop - P[v].z) / rowH, 0.0f, float(R - 1));
		const int c0 = std::min(int(fx), C - 2), r0 = std::min(int(fz), R - 1);
		const float tx = fx - c0, tz = r0 < R - 1 ? fz - r0 : 0.0f;
		const int r1 = std::min(r0 + 1, R - 1);
		const float k[4] = { (1 - tx) * (1 - tz), tx * (1 - tz), (1 - tx) * tz, tx * tz };
		const int idx[4] = { r0 * C + c0, r0 * C + c0 + 1, r1 * C + c0, r1 * C + c0 + 1 };
		for (int j = 0; j < 4; ++j) if (k[j] > 0) nw[v][int(B0) + idx[j]] += ws[v] * k[j];
	}
	// at most 4 bones a vertex, renormalised
	for (auto& m : nw) {
		std::vector<std::pair<float, int>> s;
		for (auto& [b, x] : m) s.push_back({ x, b });
		std::sort(s.rbegin(), s.rend());
		if (s.size() > 4) s.resize(4);
		float t = 0;
		for (auto& p : s) t += p.first;
		m.clear();
		for (auto& p : s) if (p.first > 0) m[p.second] = p.first / t;
	}
	// the skin: old bones (skirt bones kept in the list, now weightless) + the grid
	std::vector<int> ids;
	nif.GetShapeBoneIDList(cape, ids);
	for (int i = 0; i < C * R; ++i) ids.push_back(gid[i]);
	nif.SetShapeBoneIDList(cape, ids);
	const uint32_t BN = uint32_t(ids.size());
	for (uint32_t b = 0; b < BN; ++b) {
		MatTransform s2b;
		if (b < B0) s2b = skinToBone[b];
		else {
			MatTransform world;
			world.translation = G[b - B0];
			s2b = world.InverseTransform().ComposeTransforms(skinToGlobal);
		}
		nif.SetShapeTransformSkinToBone(cape, b, s2b);
		std::unordered_map<uint16_t, float> m;
		for (size_t v = 0; v < N; ++v) if (auto it = nw[v].find(int(b)); it != nw[v].end()) m[uint16_t(v)] = it->second;
		nif.SetShapeBoneWeights(capeName, b, m);
		Vector3 cc;
		float n = 0;
		for (auto& [v, x] : m) cc += s2b.ApplyTransform(verts[v]), n += 1;
		if (n > 0) cc /= n;
		float rad = 0;
		for (auto& [v, x] : m) rad = std::max(rad, (s2b.ApplyTransform(verts[v]) - cc).length());
		BoundingSphere bs(cc, rad + 1.0f);
		nif.SetShapeBoneBounds(capeName, b, bs);
	}
	for (size_t v = 0; v < N; ++v) {
		std::vector<uint8_t> bi;
		std::vector<float> bw;
		for (auto& [b, x] : nw[v]) bi.push_back(uint8_t(b)), bw.push_back(x);
		nif.SetShapeVertWeights(capeName, uint16_t(v), bi, bw);
	}
	nif.UpdateSkinPartitions(cape);
	// the body's collision proxies
	NifFile col;
	if (col.Load(argv[2])) { fprintf(stderr, "collision source load failed\n"); return 1; }
	for (auto* s : col.GetShapes()) {
		const std::string n = s->name.get();
		if (n.rfind("CC_Col", 0) != 0 && n != "CC_BodyCollision") continue;
		if (!nif.CloneShape(s, n, &col)) { fprintf(stderr, "clone %s failed\n", n.c_str()); return 1; }
		printf("collision %s\n", n.c_str());
	}
	// the physics file
	auto extra = std::make_unique<NiStringExtraData>();
	extra->name.get() = "HDT Skinned Mesh Physics Object";
	extra->stringData.get() = argv[4];
	nif.AssignExtraData(nif.GetRootNode(), std::move(extra));
	if (nif.Save(argv[3])) { fprintf(stderr, "save failed\n"); return 1; }
	printf("wrote %s\n", argv[3]);
}
