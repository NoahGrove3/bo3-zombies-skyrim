// rootmul <factor> <in.nif> <out.nif>: (Noah, 2026-11-24: "make the scythes 25% larger") the model's root node scaled by factor -
// the whole model, its parts' places with it, and any effect art hung under the root in game (src/WeaponFx.cpp) with it
#include "NifFile.hpp"
#include <cstdio>
#include <cstdlib>
using namespace nifly;
int main(int argc, char** argv)
{
	if (argc < 4) { fprintf(stderr, "rootmul <factor> <in.nif> <out.nif>\n"); return 2; }
	NifFile nif;
	if (nif.Load(argv[2])) { fprintf(stderr, "cannot load %s\n", argv[2]); return 1; }
	auto* root = nif.GetRootNode();
	if (!root) { fprintf(stderr, "no root\n"); return 1; }
	root->transform.scale *= static_cast<float>(atof(argv[1]));
	if (nif.Save(argv[3])) { fprintf(stderr, "cannot save %s\n", argv[3]); return 1; }
	printf("%s: root scale %.3f\n", argv[3], root->transform.scale);
	return 0;
}
