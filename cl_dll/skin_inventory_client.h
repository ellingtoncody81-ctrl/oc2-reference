#pragma once
#include <vector>
class CStudioModelRenderer;
struct cl_entity_s;
struct model_s;
void OC2_InventoryInit();
void OC2_InventoryVidInit();
int OC2_InventoryKnifeDefinition();
float OC2_InventoryKnifeDuration(int animation,float fallback);
// Returns 0 for legacy customization, -1 for removal, or the local pattern ID.
int OC2_InventoryCharmSelection(int weapon);
bool OC2_InventoryCharmOverride(int weapon,int &texture,bool &hide);

// Changes are scoped to drawing the local viewmodel. Engine/network entities,
// cached model materials, and following arm/accessory draws retain their state.
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
 explicit OC2_InventoryMaterialScope(CStudioModelRenderer &renderer);
 ~OC2_InventoryMaterialScope();
 OC2_InventoryMaterialScope(const OC2_InventoryMaterialScope &)=delete;
 OC2_InventoryMaterialScope &operator=(const OC2_InventoryMaterialScope &)=delete;
private:
 struct Saved {int *address;int value;Saved(int *p,int v):address(p),value(v){}};
 std::vector<Saved> saved;
};
