// settex <nif> <shape> <diffuse> <normal>: a shape's texture paths set (paths with spaces, which modelconv's job lines can't carry)
#include "NifFile.hpp"
using namespace nifly;
int main(int c, char** v) {
	if (c < 5) return 2;
	NifFile n; if (n.Load(v[1])) return 1;
	int done = 0;
	for (auto* s : n.GetShapes()) {
		const std::string nm0 = s->name.get(), want = v[2]; if (nm0 != want && nm0.rfind(want + ":", 0) != 0) continue;
		std::string d = v[3], nm = v[4];
		n.SetTextureSlot(s, d, 0); n.SetTextureSlot(s, nm, 1); ++done;
	}
	if (!done) return 3;
	return n.Save(v[1]);
}
