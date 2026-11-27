// orientfix <in.nif> <out.nif>
// (Noah, 2026-11-26: the scythes "inside out, so I can see the inside of the textures, and at some angles I could see through
// the scythe") every shape's triangles wound one way: each connected piece (its corners welded by position) walked from one
// triangle to its neighbours, each neighbour turned to agree with the one before it, then the whole piece turned to face out
// (its signed volume positive, as Skyrim's own weapons are); normals and tangents worked out again. A shape still left with
// open edges (where a blade or pole was cut from its weapon) is drawn double-sided, so nothing shows through the cut.
#include "NifFile.hpp"
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <tuple>
using namespace nifly;

int main(int argc, char** argv)
{
	if (argc < 3) { fprintf(stderr, "usage: orientfix in.nif out.nif\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "load failed\n"); return 1; }
	for (auto* s : nif.GetShapes()) {
		const auto* vs = nif.GetVertsForShape(s);
		std::vector<Triangle> tr;
		s->GetTriangles(tr);
		if (!vs || tr.empty()) continue;
		// corners welded by position
		std::map<std::tuple<long, long, long>, int> weld;
		std::vector<int> id(vs->size());
		for (size_t i = 0; i < vs->size(); ++i) {
			const auto p = (*vs)[i];
			const auto k = std::make_tuple(std::lround(p.x * 100), std::lround(p.y * 100), std::lround(p.z * 100));
			auto it = weld.find(k);
			if (it == weld.end()) { const int n = static_cast<int>(weld.size()); weld[k] = n; id[i] = n; }
			else id[i] = it->second;
		}
		auto corner = [&](const Triangle& t, int k) { return id[k == 0 ? t.p1 : k == 1 ? t.p2 : t.p3]; };
		// each edge: the triangles on it
		std::map<std::pair<int, int>, std::vector<int>> edges;
		for (int f = 0; f < static_cast<int>(tr.size()); ++f)
			for (int k = 0; k < 3; ++k) {
				const int a = corner(tr[f], k), b = corner(tr[f], (k + 1) % 3);
				edges[{ std::min(a, b), std::max(a, b) }].push_back(f);
			}
		// does triangle f run a->b?
		auto runs = [&](int f, int a, int b) {
			for (int k = 0; k < 3; ++k) if (corner(tr[f], k) == a && corner(tr[f], (k + 1) % 3) == b) return true;
			return false;
		};
		std::vector<int> piece(tr.size(), -1);
		std::vector<bool> flip(tr.size(), false);
		int pieces = 0, turned = 0, open = 0;
		for (int f0 = 0; f0 < static_cast<int>(tr.size()); ++f0) {
			if (piece[f0] >= 0) continue;
			std::deque<int> q{ f0 };
			piece[f0] = pieces;
			std::vector<int> members;
			while (!q.empty()) {
				const int f = q.front(); q.pop_front();
				members.push_back(f);
				for (int k = 0; k < 3; ++k) {
					int a = corner(tr[f], k), b = corner(tr[f], (k + 1) % 3);
					if (flip[f]) std::swap(a, b);   // (f as it will be wound)
					const auto& on = edges[{ std::min(a, b), std::max(a, b) }];
					if (on.size() != 2) continue;   // (open, or more than two faces: left as they are)
					const int g = on[0] == f ? on[1] : on[0];
					if (piece[g] >= 0) continue;
					piece[g] = pieces;
					flip[g] = runs(g, a, b);   // (a neighbour agrees when it runs the shared edge the other way)
					q.push_back(g);
				}
			}
			// the piece facing out: its faces, weighed by their size, turned away from the piece's own middle (its signed volume
			// about that middle positive - an open piece too, a pole cut from its axe, where the volume about the origin means nothing)
			Vector3 mid;
			double area = 0;
			for (int f : members) {
				const Vector3 a = (*vs)[tr[f].p1], b = (*vs)[tr[f].p2], c = (*vs)[tr[f].p3];
				const double w = (b - a).cross(c - a).length();
				mid += (a + b + c) * static_cast<float>(w / 3.0);
				area += w;
			}
			if (area > 0) mid = mid / static_cast<float>(area);
			double vol = 0;
			for (int f : members) {
				Triangle t = tr[f];
				if (flip[f]) std::swap(t.p2, t.p3);
				const Vector3 a = (*vs)[t.p1] - mid, b = (*vs)[t.p2] - mid, c = (*vs)[t.p3] - mid;
				vol += a.dot(b.cross(c));
			}
			if (vol < 0) for (int f : members) flip[f] = !flip[f];
			++pieces;
		}
		for (int f = 0; f < static_cast<int>(tr.size()); ++f) if (flip[f]) { std::swap(tr[f].p2, tr[f].p3); ++turned; }
		for (auto& [k, on] : edges) if (on.size() == 1) ++open;
		s->SetTriangles(tr);
		nif.CalcNormalsForShape(s, true, true);
		nif.CalcTangentsForShape(s);
		if (open > 0) if (auto* sh = nif.GetShader(s)) sh->SetDoubleSided(true);
		printf("%s: %d pieces, %d of %zu triangles turned, %d open edges%s\n", s->name.get().c_str(), pieces, turned, tr.size(), open, open ? " (double-sided)" : "");
	}
	if (nif.Save(argv[2])) { fprintf(stderr, "save failed\n"); return 1; }
	return 0;
}
