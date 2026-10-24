#pragma once
#include <vector>
class CStudioModelRenderer;
struct cl_entity_s;
struct model_s;
namespace oc2_gloves { struct Entry; }
void OC2_InventoryInit();
void OC2_InventoryVidInit();
void OC2_InventoryFrame();
void OC2_InventoryResetSession();
int OC2_InventoryKnifeDefinition();
int OC2_InventoryPlayerKnifeDefinition(int player);
float OC2_InventoryKnifeDuration(int animation,float fallback);
bool OC2_InventoryOwnAccessories(int weapon);
model_s *OC2_InventoryPlayerModel(model_s *model,int player);
// Returns 0 for legacy customization, -1 for removal, or the local pattern ID.
int OC2_InventoryCharmSelection(int weapon);
bool OC2_InventoryCharmOverride(int weapon,int &texture,bool &hide);
// Resolves the observed player, independently of the held weapon's owner.
int OC2_InventoryViewPlayer();
const oc2_gloves::Entry *OC2_InventoryPlayerGlove(int player);
void OC2_InventoryGloveDrawn(int player,int resource,int parts,const char *model,int body,int skin);

class OC2_InventoryGloveMaterialScope
{
public:
 explicit OC2_InventoryGloveMaterialScope(CStudioModelRenderer &renderer,int resource);
 ~OC2_InventoryGloveMaterialScope();
 OC2_InventoryGloveMaterialScope(const OC2_InventoryGloveMaterialScope &)=delete;
 OC2_InventoryGloveMaterialScope &operator=(const OC2_InventoryGloveMaterialScope &)=delete;
 bool Ready() const { return ready; }
private:
 struct Saved {int *address;int value;Saved(int *p,int v):address(p),value(v){}};
 std::vector<Saved> saved;
 bool ready=false;
};

// Changes are scoped to one weapon draw. Shared model materials and following
// player, arm and accessory draws retain their original state.
class OC2_InventoryModelScope
{
public:
 explicit OC2_InventoryModelScope(cl_entity_s *entity);
 ~OC2_InventoryModelScope();
 OC2_InventoryModelScope(const OC2_InventoryModelScope &)=delete;
 OC2_InventoryModelScope &operator=(const OC2_InventoryModelScope &)=delete;
private:
 cl_entity_s *entity=nullptr;model_s *original=nullptr;
};
class OC2_InventoryMaterialScope
{
public:
 explicit OC2_InventoryMaterialScope(CStudioModelRenderer &renderer,model_s *weaponModel=nullptr,int player=0);
 ~OC2_InventoryMaterialScope();
 OC2_InventoryMaterialScope(const OC2_InventoryMaterialScope &)=delete;
 OC2_InventoryMaterialScope &operator=(const OC2_InventoryMaterialScope &)=delete;
private:
 struct Saved {int *address;int value;Saved(int *p,int v):address(p),value(v){}};
 std::vector<Saved> saved;
};
