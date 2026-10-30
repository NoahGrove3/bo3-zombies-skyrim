// meshdump <nif> <shape|*> : v x y z | t a b c  (skin space), plus head s2b
#include "NifFile.hpp"
#include <cstdio>
using namespace nifly;
int main(int c,char**v){NifFile n; n.Load(v[1]); std::string want=v[2];
for(auto*s:n.GetShapes()){ if(want!="*" && s->name.get()!=want) continue; auto vs=*n.GetVertsForShape(s); std::vector<Triangle> t; s->GetTriangles(t);
 std::vector<std::string> bl; n.GetShapeBoneList(s,bl); for(size_t i=0;i<bl.size();++i) if(bl[i]=="NPC Head [Head]"){MatTransform m; n.GetShapeTransformSkinToBone(s,uint32_t(i),m); printf("s2b %f %f %f %f %f %f %f %f %f %f %f %f %f\n",m.translation.x,m.translation.y,m.translation.z,m.rotation[0][0],m.rotation[0][1],m.rotation[0][2],m.rotation[1][0],m.rotation[1][1],m.rotation[1][2],m.rotation[2][0],m.rotation[2][1],m.rotation[2][2],m.scale);}
 printf("shape %s\n",s->name.get().c_str()); for(auto&p:vs) printf("v %f %f %f\n",p.x,p.y,p.z); for(auto&tr:t) printf("t %d %d %d\n",tr.p1,tr.p2,tr.p3);}}
