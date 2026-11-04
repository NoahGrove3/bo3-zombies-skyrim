// shapescale <in.nif> <out.nif> <name prefix>=<scale> ...: the named shapes scaled about their own place (Noah: the Light Orb
// "still tiny when viewing it" - the inventory fits a model's whole bound in view, and the sun's faint outer flash and halo
// made that bound four times its bright core; drawn in close, the core fills the view)
#include "NifFile.hpp"
#include <cstdio>
#include <string>
#include <vector>
using namespace nifly;
int main(int argc, char** argv) {
	if (argc < 4) { fprintf(stderr, "usage: shapescale in out name=scale...\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "load failed\n"); return 1; }
	int n = 0;
	for (int i = 3; i < argc; ++i) {
		std::string a(argv[i]);
		auto eq = a.find('=');
		std::string pre = a.substr(0, eq);
		float sc = std::stof(a.substr(eq + 1));
		for (auto* s : nif.GetShapes()) {
			if (s->name.get().rfind(pre, 0) != 0) continue;
			auto t = s->GetTransformToParent();
			t.scale *= sc;
			s->SetTransformToParent(t);
			++n;
		}
	}
	nif.Save(argv[2]);
	printf("%d shapes scaled\n", n);
}
