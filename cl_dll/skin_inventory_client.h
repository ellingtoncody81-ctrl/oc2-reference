#pragma once
#include <vector>
namespace oc2_accessories {struct WeaponConfig;}
class CStudioModelRenderer;
struct cl_entity_s;
struct model_s;
struct alight_s;
namespace oc2_gloves { struct Entry; }
void OC2_InventoryInit();
void OC2_InventoryShutdown();
void OC2_WorldAccessoriesInit();
void OC2_WorldAccessoriesReset();
void OC2_InventoryVidInit();
void OC2_InventoryFrame();
void OC2_InventoryResetSession();
int OC2_InventoryKnifeDefinition();
int OC2_InventoryPlayerKnifeDefinition(int player);
float OC2_InventoryKnifeDuration(int animation,float fallback);
bool OC2_InventoryOwnAccessories(int weapon);
bool OC2_InventoryInstanceAccessories(int weapon,oc2_accessories::WeaponConfig &config,unsigned &count);
bool OC2_InventoryWorldAccessories(model_s *model,int player,int serial,int entity,oc2_accessories::WeaponConfig &config,unsigned &count,int &pattern,int &instance);
int OC2_LegacyCharmTexture(int sticker);
void OC2_DrawWorldAccessories(CStudioModelRenderer &renderer,model_s *model,int player,int serial,alight_s &lighting,float *lightdir);
void OC2_InventoryPublishAccessories(int weapon,const oc2_accessories::WeaponConfig &config,unsigned count);
model_s *OC2_InventoryPlayerModel(model_s *model,int player);
// Returns 0 for legacy customization, -1 for removal, or the local pattern ID.
int OC2_InventoryCharmSelection(int weapon);
bool OC2_InventoryCharmOverride(int weapon,int &texture,bool &hide);
// Resolves the observed player, independently of the held weapon's owner.
int OC2_InventoryViewPlayer();
const oc2_gloves::Entry *OC2_InventoryPlayerGlove(int player);
void OC2_InventoryGloveDrawn(int player,int resource,int parts,const char *model,int body,int skin);
void OC2_InventoryCaptureCorpseGlove(cl_entity_s *corpse,int player);
class OC2_InventoryGloveBodyModelScope
{
public:
 // Drawing only: the canonical owner/model/header remains in force during
 // animation, live-pose capture, ragdoll registration and ragdoll application.
 explicit OC2_InventoryGloveBodyModelScope(CStudioModelRenderer &renderer,int player=0);
 ~OC2_InventoryGloveBodyModelScope();
private:
 CStudioModelRenderer &renderer;
 model_s *original=nullptr;
 void *header=nullptr,*bodyPart=nullptr,*subModel=nullptr;
};

// One body draw owns the temporary suppression of its original hand mesh.
// Draw() follows StudioSaveBones, then restores the body pose/render header.
class OC2_InventoryPlayerGloveScope
{
public:
 explicit OC2_InventoryPlayerGloveScope(CStudioModelRenderer &renderer,int player=0);
 ~OC2_InventoryPlayerGloveScope();
 void Draw(alight_s &lighting,float *lightdir);
private:
 struct Saved {int *address;int value;Saved(int *p,int v):address(p),value(v){}};
 CStudioModelRenderer &renderer;
 std::vector<Saved> hidden;
 model_s *model=nullptr;
 void *header=nullptr;
 int resource=0,player=0,corpseTag=0;
 bool corpse=false;
 bool replacementBody=false;
};

class OC2_InventoryGloveMaterialScope
{
public:
 explicit OC2_InventoryGloveMaterialScope(CStudioModelRenderer &renderer,int resource,bool record=true);
 ~OC2_InventoryGloveMaterialScope();
 OC2_InventoryGloveMaterialScope(const OC2_InventoryGloveMaterialScope &)=delete;
 OC2_InventoryGloveMaterialScope &operator=(const OC2_InventoryGloveMaterialScope &)=delete;
 bool Ready() const { return ready; }
 int Swaps() const { return int(saved.size()); }
private:
 struct Saved {int *address;int value;Saved(int *p,int v):address(p),value(v){}};
 std::vector<Saved> saved;
 std::vector<int> pinned;
 bool ready=false;
 bool record=true;
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
 // A carried W model supplies its actual item serial and owner; zero selects
 // the existing active-player or world-entity lookup for ordinary P/W draws.
 explicit OC2_InventoryMaterialScope(CStudioModelRenderer &renderer,model_s *weaponModel=nullptr,int player=0,int instance=0,char context=0,bool stickersOnly=false);
 ~OC2_InventoryMaterialScope();
 bool Ready() const { return ready; }
 OC2_InventoryMaterialScope(const OC2_InventoryMaterialScope &)=delete;
 OC2_InventoryMaterialScope &operator=(const OC2_InventoryMaterialScope &)=delete;
private:
 bool ready=false;
 struct Saved {int *address;int value;Saved(int *p,int v):address(p),value(v){}};
 struct SavedShort {short *address;short value;SavedShort(short *p,short v):address(p),value(v){}};
 std::vector<Saved> saved;
 std::vector<SavedShort> hidden;
 std::vector<int> pinned;
};
