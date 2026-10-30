// le2se <in.nif> <out.nif>: an original-Skyrim (LE) mesh made a Special Edition one, as SSE NIF Optimizer does
// (NiTriShape -> BSTriShape, skin partitions, tangents, shader flags), via nifly's OptimizeFor.
#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int argc, char** argv)
{
	if (argc < 3) { fprintf(stderr, "usage: le2se <in> <out>\n"); return 1; }
	NifFile n;
	if (n.Load(argv[1])) { fprintf(stderr, "load failed %s\n", argv[1]); return 1; }
	const auto v = n.GetHeader().GetVersion();
	if (v.IsSSE()) { if (n.Save(argv[2])) return 1; printf("already SE: %s\n", argv[1]); return 0; }
	OptOptions o;
	o.targetVersion = NiVersion::getSSE();
	const auto r = n.OptimizeFor(o);
	if (r.versionMismatch) { fprintf(stderr, "version not convertible %s\n", argv[1]); return 1; }
	if (n.Save(argv[2])) { fprintf(stderr, "save failed\n"); return 1; }
	printf("converted %s\n", argv[1]);
}
