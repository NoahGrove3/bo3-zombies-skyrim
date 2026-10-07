#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int argc, char** argv) {
	NifFile nif; nif.Load(argv[1]);
	auto* root = nif.GetRootNode();
	printf("root '%s' type %s children %u\n", root->name.get().c_str(), root->GetBlockName(), root->childRefs.GetSize());
	for (auto* s : nif.GetShapes()) {
		auto b = s->GetBounds();
		printf("shape %s %s bounds c(%.1f %.1f %.1f) r %.1f", s->name.get().c_str(), s->GetBlockName(), b.center.x, b.center.y, b.center.z, b.radius);
		if (auto* ts = dynamic_cast<BSTriShape*>(s)) printf(" vdesc %016llX verts %u tris %u", (unsigned long long)ts->vertexDesc.GetFlags(), ts->GetNumVertices(), ts->GetNumTriangles());
		if (auto* sh = dynamic_cast<BSLightingShaderProperty*>(nif.GetShader(s))) printf(" sf1 %08X sf2 %08X type %u alpha %.2f", sh->shaderFlags1, sh->shaderFlags2, sh->GetShaderType(), sh->alpha);
		printf(" skinned %d\n", s->IsSkinned());
	}
}
