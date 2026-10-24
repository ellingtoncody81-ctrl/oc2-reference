#pragma once
#include "BaseItem.h"
#include "../../../../common/oc2_skin_inventory.h"

// A self-contained studio scene. DrawAt takes physical screen coordinates;
// regular BaseItem coordinates, VidInit and Draw work as usual.
class CMenuPanoramaModelView : public CMenuBaseItem
{
public:
    CMenuPanoramaModelView();
    ~CMenuPanoramaModelView() override;
    void SetLobby(int team,int weaponId=28);
    void SetWeapon(int weaponId,int skinId=0);
    void SetKnife(int definition,int skinId=0);
    void SetGloves(int resourceId);
    void SetAppearance(const oc2_inventory::WeaponState &state,
        const oc2_accessories::WeaponConfig *accessories=nullptr);
    void SetInteractive(bool interactive);
    void ReloadConfiguration();
    void ResetView();
    void Dump(const char *tag) const;
    void VidInit() override;
    void Draw() override;
    bool DrawAt(int x,int y,int width,int height);
    bool KeyDown(int key) override;
    bool KeyUp(int key) override;
    bool MouseMove(int x,int y) override;
private:
    struct Scene;
    Scene *m_scene;
    CMenuPanoramaModelView(const CMenuPanoramaModelView &)=delete;
    CMenuPanoramaModelView &operator=(const CMenuPanoramaModelView &)=delete;
};
