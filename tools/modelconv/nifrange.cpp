// nifrange <nif> : every shape's vertices in the model's space; along x, y and z the 2nd and 98th percentile (a stray
// vertex or a far-off scrap of mesh doesn't count) - a model's real reach, for holding it on display
#include "NifFile.hpp"
#include <algorithm>
#include <cstdio>
#include <vector>
using namespace nifly;
int main(int argc, char** argv) {
	NifFile nif; if (nif.Load(argv[1])) { printf("error\n"); return 1; }
	std::vector<float> X, Y, Z;
	for (auto* s : nif.GetShapes()) {
		auto* vs = nif.GetVertsForShape(s); if (!vs) continue;
		if (s->flags & 1) continue;   // (a hidden shape: not seen in the game)
		bool hid = false;
		for (auto* par = nif.GetParentNode(s); par; par = nif.GetParentNode(par)) if (par->flags & 1) hid = true;
		if (hid) continue;
		if (argc > 2) fprintf(stderr, "  %s\n", s->name.get().c_str());
		// (its own transform, then every parent's up to the root)
		MatTransform m = s->GetTransformToParent();
		for (auto* par = nif.GetParentNode(s); par && par != nif.GetRootNode(); par = nif.GetParentNode(par)) m = par->GetTransformToParent().ComposeTransforms(m);
		for (auto& p : *vs) { auto q = m.ApplyTransform(p); X.push_back(q.x), Y.push_back(q.y), Z.push_back(q.z); if (argc > 3) fprintf(stderr, "P %f %f %f\n", q.x, q.y, q.z); }
	}
	auto pr = [](std::vector<float>& v, double f) { std::sort(v.begin(), v.end()); return v[size_t(f * (v.size() - 1))]; };
	printf("%.2f %.2f %.2f %.2f %.2f %.2f\n", pr(X, .02), pr(X, .98), pr(Y, .02), pr(Y, .98), pr(Z, .02), pr(Z, .98));
}
