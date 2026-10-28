#pragma once
#include "oc2_item_appearance.h"
class CStudioModelRenderer;
struct entity_state_s;
struct alight_s;
struct model_s;

// Snapshot copies of the specified owner's live OC2Item records. The
// inventory implementation returns the number copied, bounded by capacity.
int OC2_InventoryOwnedItems(int player,oc2_item::Appearance *out,int capacity);
model_s *OC2_InventoryWorldKnifeModel(int definition,int &displayDefinition);
void OC2_CarryInit();
void OC2_CarryFrame();
void OC2_InventoryDrawCarried(CStudioModelRenderer &renderer,const entity_state_s *player,alight_s &lighting,float *lightdir);
