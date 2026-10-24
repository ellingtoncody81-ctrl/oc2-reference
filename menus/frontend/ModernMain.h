#pragma once
#include "Framework.h"
#include "Action.h"
#include "YesNoMessageBox.h"
#include "AccessoriesEditor.h"
#include "SkinEditor.h"
#include "GloveEditor.h"
#include "PanoramaChrome.h"
#include "PanoramaPlay.h"
#include "PanoramaInventory.h"
#include "PanoramaSettings.h"
#include "PanoramaModelView.h"
#include "keydefs.h"
#include <cstring>

// The native root follows CS:GO's main-menu/page ownership and keeps the
// original in-game pause root separate. The model view uses an engine scene.
class CMenuModernMain : public CMenuFramework
{
public:
    CMenuModernMain():CMenuFramework("CMenuModernMain") {}
    void Show() override
    {
        if(CL_IsActive())return;
        CMenuFramework::Show();m_page=-1;
        m_lobby.SetLobby(1,28);m_lobby.SetInteractive(false);
        SetCursorToItem(m_playButton);
    }
    void Navigate(int page)
    {
        if(CL_IsActive())return;
        if(!IsVisible())Show();
        HidePages();m_page=page;
        if(page==0)m_play.Show();
        else if(page==1)m_inventory.Show();
        else if(page==2)m_settings.Show();
        else if(page==3){m_page=-1;m_quit.Show();m_quit.SetCursorToItem(m_quit.no);}
        else SetCursorToItem(m_playButton);
    }
    bool KeyDown(int key) override
    {
        if(UI::Key::IsEscape(key)){Navigate(3);return true;}
        return CMenuFramework::KeyDown(key);
    }
    void Draw() override
    {
        if(CL_IsActive())return;
        oc2_panorama::DrawBackground();
        const int width=uiStatic.width,panelWidth=int(width*.46f);
        Fill(88,72,panelWidth,566,PackRGBA(49,66,79,82));
        Fill(112,102,panelWidth-48,1,PackRGBA(236,242,247,75));
        Text(112,126,panelWidth-48,60,"OFFENSIVE",38,true);
        Text(112,184,panelWidth-48,58,"CLASSIC 2",38,true);
        Text(112,272,panelWidth-48,32,"单人练习",21,true);
        Text(112,321,panelWidth-48,30,"选择地图与电脑玩家难度，开始训练。",16);
        Text(112,548,panelWidth-48,28,"搭配你的武器、刀具与手套。",16);
        Fill(112,605,panelWidth-48,1,PackRGBA(236,242,247,60));
        m_chrome.Draw();
        CMenuFramework::Draw();
    }
    void Dump()
    {
        CMenuBaseWindow *top=WindowStack()->Current();
        const CMenuBaseItem *focus=top?top->ItemAtCursor():nullptr;
        const char *page=m_page==0?"play":m_page==1?"inventory":m_page==2?"settings":"home";
        Con_Printf("frontend: visible=%d page=%s screen=%dx%d client_active=%d connected=%d cursor=%d/%d focus=%s top_window=%s modal=%d\n",
            int(IsVisible()),page,int(ScreenWidth),int(ScreenHeight),int(CL_IsActive()),int(CL_IsActive()),uiStatic.cursorX,uiStatic.cursorY,
            focus&&focus->szTag?focus->szTag:"none",top&&top->szName?top->szName:"none",int(m_quit.IsVisible()));
        for(int i=0;i<ItemCount();++i)
        {
            auto *item=GetItemByIndex(i);if(!item||!item->szTag)continue;
            const Point p=item->GetRenderPosition();const Size s=item->GetRenderSize();
            Con_Printf("frontend_item: tag=%s x=%d y=%d w=%d h=%d visible=%d enabled=%d focus=%d units=screen_pixels\n",
                item->szTag,p.x,p.y,s.w,s.h,int(IsVisible()&&top==this&&item->IsVisible()),int(!(item->iFlags&(QMF_GRAYED|QMF_INACTIVE))),int(item->IsCurrentSelected()));
        }
        m_lobby.Dump("lobby");m_play.Dump();m_inventory.Dump();m_settings.Dump();m_skins.Dump();m_gloves.Dump();m_accessories.Dump();
    }
    void DebugPage(const char *name)
    {
        if(!gpGlobals->developer||CL_IsActive())return;
        if(!std::strcmp(name,"play"))Navigate(0);
        else if(!std::strcmp(name,"inventory")||!std::strcmp(name,"loadout"))Navigate(1);
        else if(!std::strcmp(name,"settings"))Navigate(2);
        else if(!std::strcmp(name,"home"))Navigate(-1);
    }
    void OpenSkin(int weapon){Navigate(1);m_inventory.OpenWeapon(weapon);}
    void OpenKnife(int definition){Navigate(1);m_inventory.OpenKnife(definition);}
    void OpenGloves(){Navigate(1);m_inventory.OpenGloves();}
    void OpenAccessory(int weapon)
    {
        if(oc2_accessories::Index(weapon)<0)return;
        Navigate(1);m_inventory.OpenWeapon(weapon);m_accessories.Open(weapon);
    }
private:
    void _Init() override
    {
        m_lobby.SetNameAndStatus("",nullptr,"lobby_model");AddItem(m_lobby);
        m_chrome.Init(*this,-1);
        Button(m_playButton,"开始游戏","home_play");m_playButton.onReleased=UI_PanoramaOpenPlay;
        Button(m_inventoryButton,"前往物品栏","home_inventory");m_inventoryButton.onReleased=UI_PanoramaOpenInventory;
        m_play.Link(this);m_inventory.Link(this);m_settings.Link(this);
        m_skins.Link(&m_inventory);m_accessories.Link(&m_inventory);m_gloves.Link(&m_inventory);
        m_skins.SetAccessories(&m_accessories);m_inventory.SetEditors(&m_skins,&m_accessories,&m_gloves);
        m_quit.Link(this);m_quit.SetMessage("退出游戏？");m_quit.onPositive.SetCommand(false,"quit\n");
        m_quit.yes.szTag="exit_yes";m_quit.no.szTag="exit_no";
    }
    void _VidInit() override
    {
        const int width=uiStatic.width;
        m_chrome.Layout();
        m_lobby.SetRect(int(width*.58f),72,int(width*.40f),690);
        m_playButton.SetRect(112,386,int(width*.31f),52);
        m_inventoryButton.SetRect(112,459,int(width*.31f),48);
    }
    void HidePages()
    {
        if(m_play.IsVisible())m_play.Hide();
        if(m_inventory.IsVisible())m_inventory.Hide();
        if(m_settings.IsVisible())m_settings.Hide();
    }
    Point Pixel(int x,int y)const{return GetRenderPosition()+Point(x,y).Scale();}
    void Fill(int x,int y,int w,int h,unsigned c){UI_FillRect(Pixel(x,y),Size(w,h).Scale(),c);}
    void Text(int x,int y,int w,int h,const char *s,int size,bool bold=false)
    {UI_DrawString(bold?uiStatic.hBoldFont:uiStatic.hDefaultFont,Pixel(x,y),Size(w,h).Scale(),s,PackRGBA(245,248,250,255),int(size*uiStatic.scaleY),QM_LEFT,ETF_FORCECOL|ETF_NO_WRAP);}
    void Button(CMenuAction &button,const char *label,const char *tag)
    {
        button.SetNameAndStatus(label,nullptr,tag);button.SetCharSize(QM_SMALLFONT);button.SetTextInsetX(18);
        button.colorBase=PackRGBA(250,252,255,255);button.colorFocus=PackRGBA(255,255,255,255);
        button.SetBackground(PackRGBA(51,70,81,160),PackRGBA(85,110,124,220));AddItem(button);
    }
    int m_page=-1;
    CMenuPanoramaChrome m_chrome;
    CMenuPanoramaModelView m_lobby;
    CMenuAction m_playButton,m_inventoryButton;
    CMenuPanoramaPlay m_play;
    CMenuPanoramaInventory m_inventory;
    CMenuPanoramaSettings m_settings;
    CMenuSkinEditor m_skins;
    CMenuAccessoriesEditor m_accessories;
    CMenuGloveEditor m_gloves;
    CMenuYesNoMessageBox m_quit;
};
