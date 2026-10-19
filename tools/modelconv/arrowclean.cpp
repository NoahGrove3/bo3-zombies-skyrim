// The arrow alone: the elemental arrow's flight model without its long white ice-trail and its magic-projectile
// animation graph (which the arrow projectile never plays: it hung the model nose-down)
#include "NifFile.hpp"
#include <iostream>
using namespace nifly;
int main(int c,char**v){
  NifFile n; if(n.Load(v[1])) return 1;
  auto& hdr=n.GetHeader();
  for(auto* sh: n.GetShapes()) if(std::string(sh->name.get())=="Icicle01_Low02") { n.DeleteShape(sh); break; }
  auto* root=n.GetRootNode();
  root->controllerRef.Clear();
  // extra data: drop the behaviour graph
  for(auto& r: root->extraDataRefs){
    auto* ed=hdr.GetBlock<NiExtraData>(r);
    if(ed && std::string(ed->GetBlockName())=="BSBehaviorGraphExtraData") r.Clear();
  }
  // nothing animated: every controller off every node and shader
  for(uint32_t i=0;i<hdr.GetNumBlocks();++i){
    if(auto* o=hdr.GetBlock<NiObjectNET>(i)) o->controllerRef.Clear();
  }
  n.DeleteUnreferencedBlocks();
  n.PrettySortBlocks();
  n.Save(v[2]);
  std::cout<<"ok "<<hdr.GetNumBlocks()<<"\n";
}
