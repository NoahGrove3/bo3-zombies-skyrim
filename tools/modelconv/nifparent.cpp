// nifparent <nif>: every shape's parent node and its transform to the parent
#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int, char** argv) {
	NifFile nif; nif.Load(argv[1]);
	for (auto* s : nif.GetShapes()) {
		auto* p = nif.GetParentNode(s);
		auto t = s->GetTransformToParent();
		printf("shape '%s' parent '%s' t(%.2f %.2f %.2f) s %.3f\n", s->name.get().c_str(), p ? p->name.get().c_str() : "-", t.translation.x, t.translation.y, t.translation.z, t.scale);
	}
}
