// shapename <in.nif> <out.nif> <old prefix> <new name> : the shape whose name starts with the prefix renamed (modelconv names
// its shapes "<group>:<n>"; Skyrim wants a weapon's scabbard to be the one shape named exactly "Scb" - the piece it leaves on
// the hip when the weapon is drawn)
#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int argc, char** argv)
{
	if (argc < 5) { fprintf(stderr, "shapename <in.nif> <out.nif> <old prefix> <new name>\n"); return 1; }
	NifFile nif;
	if (nif.Load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
	const std::string pre = argv[3];
	int n = 0;
	for (auto* s : nif.GetShapes())
		if (s->name.get().rfind(pre, 0) == 0) NifFile::RenameShape(s, argv[4]), ++n;
	if (!n) { fprintf(stderr, "no shape named %s...\n", pre.c_str()); return 1; }
	if (nif.Save(argv[2])) { fprintf(stderr, "cannot save %s\n", argv[2]); return 1; }
	printf("%s: %d shape(s) %s... -> %s\n", argv[2], n, pre.c_str(), argv[4]);
	return 0;
}
