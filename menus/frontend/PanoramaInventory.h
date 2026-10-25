#pragma once
#include "Framework.h"
#include "Action.h"
#include "Field.h"
#include "SkinEditor.h"
#include "GloveEditor.h"
#include "PanoramaModelView.h"
#include "PanoramaChrome.h"
#include "ProfileInventory.h"
#include <memory>
#include <string>
#include <vector>

class CMenuPanoramaWheel : public CMenuBaseItem
{
public:
    struct Item { int key;std::string name,image;std::shared_ptr<CMenuPanoramaModelView> model; };
    std::vector<Item> entries;
    std::string title;
    int selected=0,hover=-1;
    int previewWidth=200,previewHeight=114;
    bool pagedLayout=false;
    void Draw() override;
    bool KeyDown(int key) override;
    bool KeyUp(int key) override;
    bool MouseMove(int x,int y) override;
    Point SlotCenter(int slot) const;
    void SlotBounds(int slot,Point &position,Size &size) const;
private:
    int Hit(int x,int y) const;
    float InnerRadiusSquared() const { return pagedLayout?.0784f:.19f; }
    struct Span {int x,y,width,sector;};
    std::vector<Span> m_spans;
    int m_cachedRadius=0,m_cachedCount=0,m_cachedStep=0;
    bool m_cachedPaged=false;
};

class CMenuPanoramaSkinCard : public CMenuAction
{
public:
    CMenuPanoramaModelView model;
    std::string title,collection;
    bool equipped=false,selected=false;
    void Draw() override;
};

// Local definitions, finishes and equipped slots retain their existing saved
// identities. Browsing or inspecting does not save until EQUIP is selected.
class CMenuPanoramaInventory : public CMenuFramework
{
public:
    CMenuPanoramaInventory();
    void SetEditors(CMenuSkinEditor *skins,CMenuAccessoriesEditor *accessories,CMenuGloveEditor *gloves);
    void Show() override;
    void OpenWeapon(int weapon);
    void OpenKnife(int definition);
    void OpenGloves();
    void Dump();
    void Draw() override;
    bool KeyDown(int key) override;
    bool KeyUp(int key) override;
    bool MouseMove(int x,int y) override;
private:
    enum { Pistol,SMG,Rifle,Sniper,Heavy,Knife,Glove,CategoryCount,CardCount=9,WheelPageSize=5 };
    void _Init() override;
    void _VidInit() override;
    void Think() override;
    static int WeaponCategory(int weapon);
    static const char *CategoryName(int category);
    static const char *GloveFamily(int index);
    static unsigned Ink();
    static unsigned Muted();
    static unsigned Accent();
    Point Pixel(int x,int y) const;
    void Fill(int x,int y,int w,int h,unsigned color);
    void Text(int x,int y,int w,int h,const char *text,int height,unsigned color,bool bold=false,unsigned align=QM_CENTER);
    void Button(CMenuAction &button,const char *name,const char *tag,bool primary=false);
    int Right() const;
    int Pages() const;
    int WheelPages() const;
    int SavedSkin() const;
    std::string ItemName() const;
    std::string FinishName(int id) const;
    std::string CollectionName(int id) const;
    bool IsEquipped(int id) const;
    void Reload();
    void BuildWheel();
    void RefreshCards();
    void SetCategory(int category);
    void Category(CMenuBaseItem *button);
    void WheelSelected();
    void ChooseWheelKey(int key);
    void PreviousWheel();
    void NextWheel();
    void SelectCard(CMenuBaseItem *button);
    void Preview(CMenuPanoramaModelView &model,int skin);
    void SyncInspect();
    void ToggleInspect(bool inspect);
    void InspectSelected();
    void CloseInspect();
    void Previous();
    void Next();
    void Equip();
    void OpenSticker(CMenuBaseItem *button);
    void OpenAccessories();
    void Back();
    void Refresh();
    bool Gun() const { return m_category<Knife; }
    int m_category=Rifle,m_weapon=28,m_definition=0,m_family=0,m_page=0,m_wheelPage=0,m_selection=0;
    bool m_catalogValid=false,m_configValid=false,m_gloveCatalogValid=false,m_gloveConfigValid=false;
    bool m_inspecting=false,m_returnEditor=false,m_error=false;
    std::string m_query,m_message;
    oc2_inventory::Catalog m_catalog;
    oc2_inventory::Config m_config;
    oc2_local_profiles::State m_profiles;
    oc2_gloves::Catalog m_gloveCatalog;
    oc2_gloves::Config m_gloveConfig;
    std::vector<int> m_wheelKeys,m_options;
    int m_cardIds[CardCount];
    char m_categoryTags[CategoryCount][40],m_cardTags[CardCount][32],m_stickerTags[4][32];
    CMenuAction m_categories[CategoryCount],m_back,m_refresh,m_previous,m_next,m_wheelPrevious,m_wheelNext;
    CMenuAction m_closeInspect,m_equip,m_accessories,m_stickers[4];
    CMenuPanoramaSkinCard m_cards[CardCount];
    CMenuPanoramaWheel m_wheel;
    CMenuField m_search;
    CMenuPanoramaModelView m_inspect;
    CMenuPanoramaChrome m_chrome;
    CMenuSkinEditor *m_skins=nullptr;
    CMenuAccessoriesEditor *m_accessoryEditor=nullptr;
    CMenuGloveEditor *m_gloveEditor=nullptr;
};
