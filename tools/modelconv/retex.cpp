// retex <in.nif> <out.nif> <from-dir> <to-dir>: every texture path in a_from (case aside) moved to a_to - a model worn with
// (only the colour maps: normal maps stay the mod's own)
// textures of our own (the class armour's accent colours, tools/classarmor.py)
#include "NifFile.hpp"
#include <algorithm>
#include <cstdio>
#include <string>
using namespace nifly;
static std::string low(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }
int main(int argc, char** argv)
{
	if (argc < 5) { fprintf(stderr, "usage: retex in out from to\n"); return 1; }
	NifFile n;
	if (n.Load(argv[1])) { fprintf(stderr, "load failed %s\n", argv[1]); return 1; }
	const std::string from = low(argv[3]), to = argv[4];
	int k = 0;
	for (auto* s : n.GetShapes())
		for (auto& r : n.GetTexturePathRefs(s)) {
			std::string& p = r.get();
			const std::string lp = low(p);
			if (lp.rfind(from, 0) == 0 && !(lp.size() > 6 && lp.compare(lp.size() - 6, 6, "_n.dds") == 0)) { p = to + p.substr(from.size()); ++k; }
		}
	if (n.Save(argv[2])) { fprintf(stderr, "save failed\n"); return 1; }
	printf("%s: %d paths\n", argv[2], k);
	return 0;
}
