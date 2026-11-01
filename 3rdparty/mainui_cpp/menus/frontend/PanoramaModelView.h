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
    static void ReleaseResources();
    static void InvalidateGeometry(const void *geometry);
    void SetLobby(int team,int weaponId=28);
    void SetWeapon(int weaponId,int skinId=0);
    void SetKnife(int definition,int skinId=0);
    void SetGloves(int resourceId);
    void SetAppearance(const oc2_inventory::WeaponState &state,
        const oc2_accessories::WeaponConfig *accessories=nullptr);
    void SetAccessoryPreview(const std::string &pattern,unsigned count);
    // Select one real gun in the dual-pistol attachment editor; -1 is normal view.
    void SetAccessoryRegion(int region);
    // Expand Elite into both real sticker sides in full-size detail views.
    void SetDualUnfold(bool enabled);
    // Applies the inventory inspect-only side-by-side mirrored placement.
    void SetDualSideBySide(bool enabled);
    // On-screen size of the drawn model relative to its viewport, 0..1 each.
    bool ContentExtent(float &widthFraction,float &heightFraction) const;
    bool ProjectAccessory(int kind,float u,float v,float &screenX,float &screenY,float &pixelsPerUnit) const;
    bool UnprojectAccessory(float screenX,float screenY,float &u,float &v) const;
    bool AttachmentBounds(int kind,float &left,float &top,float &right,float &bottom) const;
    void SetInteractive(bool interactive);
    void ReloadConfiguration();
    void ResetView();
    void Dump(const char *tag) const;
    void DumpStyle(const char *surface,int index) const;
    void VidInit() override;
    void Draw() override;
    bool DrawAt(int x,int y,int width,int height);
    bool KeyDown(int key) override;
    bool KeyUp(int key) override;
    bool MouseMove(int x,int y) override;
private:
    struct Scene;
    CMenuPanoramaModelView *m_dualView=nullptr;
    Scene *m_scene;
    CMenuPanoramaModelView(const CMenuPanoramaModelView &)=delete;
    CMenuPanoramaModelView &operator=(const CMenuPanoramaModelView &)=delete;
};
