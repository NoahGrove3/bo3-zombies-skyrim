// tipnode <in.nif> <out.nif> : add a "MagicEffectsNode" (Skyrim's staff casting-art node) at the staff's head - the end of
// its long axis whose cross-section is widest - so a staff cast's charge shows at the tip. Prints the extents.
#include "NifFile.hpp"
#include <cstdio>
#include <cmath>
using namespace nifly;
int main(int, char** argv) {
	NifFile nif; if (nif.Load(argv[1])) return 1;
	std::vector<Vector3> all;
	for (auto* s : nif.GetShapes()) { const auto* v = nif.GetVertsForShape(s); if (v) all.insert(all.end(), v->begin(), v->end()); }
	Vector3 lo{1e9,1e9,1e9}, hi{-1e9,-1e9,-1e9};
	for (auto& p : all) { lo.x=std::min(lo.x,p.x); lo.y=std::min(lo.y,p.y); lo.z=std::min(lo.z,p.z); hi.x=std::max(hi.x,p.x); hi.y=std::max(hi.y,p.y); hi.z=std::max(hi.z,p.z); }
	printf("verts %zu  x %.1f..%.1f  y %.1f..%.1f  z %.1f..%.1f\n", all.size(), lo.x, hi.x, lo.y, hi.y, lo.z, hi.z);
	float ext[3] = { hi.x-lo.x, hi.y-lo.y, hi.z-lo.z }; int ax = 0; for (int k=1;k<3;++k) if (ext[k]>ext[ax]) ax=k;
	auto c = [&](const Vector3& p, int k){ return k==0?p.x:k==1?p.y:p.z; };
	float a0 = c(lo,ax), a1 = c(hi,ax), band = 0.12f*(a1-a0);
	auto spread = [&](bool top){ float mn[3]={1e9,1e9,1e9}, mx[3]={-1e9,-1e9,-1e9}; int n=0; Vector3 sum{};
		for (auto& p: all){ float t=c(p,ax); if (top? t>a1-band : t<a0+band){ for(int k=0;k<3;++k){mn[k]=std::min(mn[k],c(p,k)); mx[k]=std::max(mx[k],c(p,k));} sum.x+=p.x;sum.y+=p.y;sum.z+=p.z;++n;} }
		float w=0; for(int k=0;k<3;++k) if(k!=ax) w+=mx[k]-mn[k]; return std::make_pair(w, n? Vector3{sum.x/n,sum.y/n,sum.z/n}:Vector3{}); };
	auto [wTop, cTop] = spread(true); auto [wBot, cBot] = spread(false);
	bool top = wTop >= wBot; Vector3 tip = top ? cTop : cBot;
	float end = top ? a1 - 0.04f*(a1-a0) : a0 + 0.04f*(a1-a0);
	if (ax==0) tip.x=end; else if (ax==1) tip.y=end; else tip.z=end;
	printf("axis %d head at %s end (width %.1f vs %.1f): node at %.1f %.1f %.1f\n", ax, top?"high":"low", wTop, wBot, tip.x, tip.y, tip.z);
	auto* root = nif.GetRootNode();
	auto node = std::make_unique<NiNode>(); node->name.get() = "MagicEffectsNode"; node->transform.translation = tip;
	int id = nif.GetHeader().AddBlock(std::move(node));
	root->childRefs.AddBlockRef(id);
	nif.Save(argv[2]);
	return 0;
}
