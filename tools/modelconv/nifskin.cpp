// nifskin <nif>: a skinned creature's body as JSON - its bones (the node tree, each bone's place), and each skinned shape's
// vertices, uvs, triangles, textures, its bones' skin-to-bone transforms and each vertex's four strongest bone weights - for the
// workshop's ability pictures, where the night's own dead stand and react (tools/zombiepreview.py)
#include "NifFile.hpp"
#include <cstdio>
#include <algorithm>
#include <map>
using namespace nifly;

static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o; }
static std::string f(float v) { char b[32]; snprintf(b, 32, "%.5g", v); return b; }
static std::string xf(const MatTransform& m) {
	std::string r = "{\"t\":[" + f(m.translation.x) + "," + f(m.translation.y) + "," + f(m.translation.z) + "],\"r\":[";
	for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r += f(m.rotation[i][j]) + (i == 2 && j == 2 ? "" : ",");
	return r + "],\"s\":" + f(m.scale) + "}";
}

int main(int argc, char** argv) {
	NifFile nif;
	if (argc < 2 || nif.Load(argv[1]) != 0) { fprintf(stderr, "can't load\n"); return 1; }
	std::string out = "{\"nodes\":[";
	bool first = true;
	for (auto* n : nif.GetNodes()) {
		auto* p = nif.GetParentNode(n);
		out += std::string(first ? "" : ",") + "{\"name\":\"" + esc(n->name.get()) + "\",\"parent\":\"" + (p ? esc(p->name.get()) : "") + "\",\"x\":" + xf(n->GetTransformToParent()) + "}";
		first = false;
	}
	out += "],\"shapes\":[";
	first = true;
	for (auto* s : nif.GetShapes()) {
		std::vector<Vector3> V; std::vector<Vector2> UV; std::vector<Triangle> T;
		nif.GetVertsForShape(s, V); nif.GetUvsForShape(s, UV); s->GetTriangles(T);
		if (V.empty() || T.empty()) continue;
		std::string tex, nrm; nif.GetTextureSlot(s, tex, 0); nif.GetTextureSlot(s, nrm, 1);
		std::vector<std::string> bones; nif.GetShapeBoneList(s, bones);
		std::vector<std::vector<std::pair<float, int>>> W(V.size());
		std::string s2b = "[";
		for (uint32_t b = 0; b < bones.size(); ++b) {
			std::unordered_map<uint16_t, float> w; nif.GetShapeBoneWeights(s, b, w);
			for (auto& [vi, wt] : w) if (vi < W.size() && wt > 0) W[vi].push_back({ wt, (int)b });
			MatTransform m; nif.GetShapeTransformSkinToBone(s, b, m);
			s2b += (b ? "," : "") + xf(m);
		}
		s2b += "]";
		auto* ap = nif.GetAlphaProperty(s);
		const int aflags = ap ? ap->flags : 0, athr = ap ? ap->threshold : 0;
		out += std::string(first ? "" : ",") + "{\"name\":\"" + esc(s->name.get()) + "\",\"tex\":\"" + esc(tex) + "\",\"nrm\":\"" + esc(nrm) + "\",\"alpha\":" + std::to_string(aflags) + ",\"athr\":" + std::to_string(athr) + ",\"x\":" + xf(s->GetTransformToParent()) + ",\"bones\":[";
		for (size_t b = 0; b < bones.size(); ++b) out += (b ? ",\"" : "\"") + esc(bones[b]) + "\"";
		out += "],\"s2b\":" + s2b + ",\"v\":[";
		for (size_t i = 0; i < V.size(); ++i) out += (i ? "," : "") + f(V[i].x) + "," + f(V[i].y) + "," + f(V[i].z);
		out += "],\"uv\":[";
		for (size_t i = 0; i < UV.size(); ++i) out += (i ? "," : "") + f(UV[i].u) + "," + f(UV[i].v);
		out += "],\"t\":[";
		for (size_t i = 0; i < T.size(); ++i) out += (i ? "," : "") + std::to_string(T[i].p1) + "," + std::to_string(T[i].p2) + "," + std::to_string(T[i].p3);
		out += "],\"w\":[";   // per vertex: 4 bone indices, then 4 weights (summing to 1)
		for (size_t i = 0; i < V.size(); ++i) {
			auto& L = W[i];
			std::sort(L.begin(), L.end(), [](auto& a, auto& b) { return a.first > b.first; });
			L.resize(4, { 0.0f, 0 });
			float sum = L[0].first + L[1].first + L[2].first + L[3].first; if (sum <= 0) { L[0].first = 1; sum = 1; }
			out += (i ? "," : "");
			for (int k = 0; k < 4; ++k) out += std::to_string(L[k].second) + ",";
			for (int k = 0; k < 4; ++k) out += f(L[k].first / sum) + (k < 3 ? "," : "");
		}
		out += "]}";
		first = false;
	}
	out += "]}";
	fwrite(out.data(), 1, out.size(), stdout);
	return 0;
}
