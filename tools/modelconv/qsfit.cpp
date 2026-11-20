// qs <in.nif> <out.nif> <scale> <shiftY>: every top-level piece scaled about the origin and moved along +Y
#include "NifFile.hpp"
#include <cstdio>
#include <cstdlib>
using namespace nifly;
int main(int c,char**v){NifFile n; if(n.Load(v[1])){puts("load fail");return 1;}
 const float k=atof(v[3]), dy=atof(v[4]);
 auto* root=n.GetRootNode(); int m=0;
 for(auto* o: n.GetChildren<NiAVObject>(root,false)){
   auto t=o->GetTransformToParent(); t.translation = t.translation*k + Vector3(0,dy,0); t.scale*=k; o->SetTransformToParent(t); ++m; }
 n.Save(v[2]); printf("moved %d pieces\n",m);}
