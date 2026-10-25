#pragma once
#include "Framework.h"
#include "Action.h"
#include "AccessoriesEditor.h"
#include "SkinEditor.h"
#include "GloveEditor.h"
#include "PanoramaChrome.h"
#include "PanoramaPlay.h"
#include "PanoramaInventory.h"
#include "PanoramaSettings.h"
#include "PanoramaModelView.h"
#include "../client/BuyModelPreview.h"
#include "keydefs.h"
#include <cstring>
#include <string>

class CMenuPanoramaConfirm : public CMenuBaseWindow
{
public:
    CMenuPanoramaConfirm():CMenuBaseWindow("PanoramaConfirm"){iFlags|=QMF_DIALOG;}
    CEventCallback onPositive;
    void SetMessage(const char *text){m_message=text;}
    void Show() override {CMenuBaseWindow::Show();SetCursorToItem(m_no);}
    bool KeyDown(int key) override {if(UI::Key::IsEscape(key)){Hide();return true;}return CMenuBaseWindow::KeyDown(key);}
    void Draw() override
    {
        UI_FillRect(0,0,ScreenWidth,ScreenHeight,PackRGBA(5,10,15,155));
        UI_FillRect(GetRenderPosition(),GetRenderSize(),PackRGBA(28,39,48,250));
        oc2_panorama::DrawText(GetRenderPosition()+Point(24,24).Scale(),Size(392,62).Scale(),m_message.c_str(),22,PackRGBA(245,247,250,255),false,QM_CENTER);
        CMenuBaseWindow::Draw();
    }
private:
    void _Init() override
    {
        m_yes.SetNameAndStatus("确定",nullptr,"exit_yes");m_no.SetNameAndStatus("取消",nullptr,"exit_no");
        for(auto *button:{&m_yes,&m_no}){oc2_panorama::CenterButton(*button);button->colorBase=PackRGBA(245,247,250,255);button->colorFocus=PackRGBA(255,220,145,255);button->SetBackground(PackRGBA(57,73,86,255),PackRGBA(80,99,114,255));AddItem(*button);}
        m_yes.onReleased=VoidCb(&CMenuPanoramaConfirm::Accept);m_no.onReleased=VoidCb(&CMenuPanoramaConfirm::Cancel);
    }
    void _VidInit() override {SetRect((uiStatic.width-440)/2,280,440,184);m_yes.SetRect(48,112,154,42);m_no.SetRect(238,112,154,42);}
    void Accept(){Hide();onPositive(this);}
    void Cancel(){Hide();}
    CMenuAction m_yes,m_no;
    std::string m_message;
};

// Main and pause are separate presentation states sharing the same inventory
// and settings pages, following CS:GO's menu-state/input ownership pattern.
class CMenuModernMain : public CMenuFramework
{
public:
    CMenuModernMain():CMenuFramework("CMenuModernMain") {}
    void Show() override
    {
        m_ingame=CL_IsActive();
        if(m_ingame)EngFuncs::CvarSetValue("ui_renderworld",1);
        CMenuFramework::Show();m_page=-1;
        m_chrome.Layout();
        CMenuBaseItem *hallItems[]={&m_lobby,&m_playButton,&m_inventoryButton,&m_agentButton,&m_weaponButton,&m_weaponPrevious,&m_weaponNext};
        for(auto *item:hallItems)item->SetVisibility(!m_ingame);
        m_restart.SetVisibility(m_ingame);m_team.SetVisibility(m_ingame);
        m_restart.SetGrayed(!EngFuncs::GetCvarFloat("host_serverstate"));
        if(!m_ingame){RefreshLobby();SetCursorToItem(m_playButton);}
    }
    void Navigate(int page)
    {
        if(!IsVisible()||m_ingame!=bool(CL_IsActive()))Show();
        HidePages();m_page=page;
        if(page==0){if(m_ingame){UI_CloseMenu();return;}m_play.Show();}
        else if(page==1)m_inventory.Show();
        else if(page==2)m_settings.Show();
        else if(page==3){m_page=-1;m_pending=m_ingame?1:0;m_confirm.SetMessage(m_ingame?"离开当前对局，返回大厅？":"退出游戏？");m_confirm.Show();}
        else if(!m_ingame){RefreshLobby();SetCursorToItem(m_playButton);}
    }
    bool KeyDown(int key) override
    {
        if(UI::Key::IsEscape(key)){if(m_ingame)UI_CloseMenu();else Navigate(3);return true;}
        if(!m_ingame&&(key==K_MWHEELUP||key==K_MWHEELDOWN)&&UI_CursorInRect(m_weaponButton.GetRenderPosition(),m_weaponButton.GetRenderSize()))
        {ChangeWeapon(key==K_MWHEELUP?-1:1);return true;}
        return CMenuFramework::KeyDown(key);
    }
    void Think() override
    {
        if(WindowStack()->Current()==this&&m_ingame!=bool(CL_IsActive()))Show();
        CMenuFramework::Think();
    }
    void Draw() override
    {
        if(WindowStack()->Current()!=this&&!m_confirm.IsVisible())return;
        if(m_ingame)
        {
            m_chrome.Draw();
            oc2_panorama::DrawText(Point(104,22).Scale(),Size(300,44).Scale(),"对局菜单",26,PackRGBA(248,248,248,255),true);
            const auto *focus=ItemAtCursor();
            const char *hint=focus&&focus->szTag?focus->szTag:"";
            const char *text=!std::strcmp(hint,"nav_play")?"继续游戏":!std::strcmp(hint,"pause_restart")?"重新开始回合":!std::strcmp(hint,"pause_team")?"选择阵营":!std::strcmp(hint,"nav_inventory")?"物品栏":!std::strcmp(hint,"nav_settings")?"游戏设置":!std::strcmp(hint,"nav_exit")?"离开对局":"按 Esc 返回游戏";
            oc2_panorama::DrawText(Point(104,70).Scale(),Size(320,34).Scale(),text,18,PackRGBA(237,238,240,255));
            CMenuFramework::Draw();return;
        }
        oc2_panorama::DrawBackground();
        const int width=uiStatic.width,panelWidth=int(width*.46f);
        Fill(88,72,panelWidth,566,PackRGBA(26,39,48,168));
        Text(112,115,panelWidth-48,68,"经典攻势 2",38,true);
        Text(112,214,panelWidth-48,40,"单人练习",26,true);
        Text(112,267,panelWidth-48,36,"选择地图和机器人难度，开始对局。",18);
        Text(112,536,panelWidth-48,38,"搭配武器、刀具、手套与贴纸。",18);
        m_chrome.Draw();CMenuFramework::Draw();
    }
    void Dump()
    {
        CMenuBaseWindow *top=WindowStack()->Current();const CMenuBaseItem *focus=top?top->ItemAtCursor():nullptr;
        const char *page=m_page==0?"play":m_page==1?"inventory":m_page==2?"settings":m_ingame?"pause":"home";
        Con_Printf("frontend: visible=%d page=%s screen=%dx%d client_active=%d connected=%d cursor=%d/%d focus=%s top_window=%s modal=%d\n",
            int(IsVisible()),page,int(ScreenWidth),int(ScreenHeight),int(CL_IsActive()),int(CL_IsActive()),uiStatic.cursorX,uiStatic.cursorY,
            focus&&focus->szTag?focus->szTag:"none",top&&top->szName?top->szName:"none",int(m_confirm.IsVisible()));
        Con_Printf("panorama_lobby: agent=%d weapon=%d ingame=%d\n",int(EngFuncs::GetCvarFloat("cl_lobby_agent")),int(EngFuncs::GetCvarFloat("cl_lobby_weapon")),int(m_ingame));
        for(int i=0;i<ItemCount();++i)
        {
            auto *item=GetItemByIndex(i);if(!item||!item->szTag)continue;const Point p=item->GetRenderPosition();const Size s=item->GetRenderSize();
            Con_Printf("frontend_item: tag=%s x=%d y=%d w=%d h=%d visible=%d enabled=%d focus=%d units=screen_pixels\n",item->szTag,p.x,p.y,s.w,s.h,int(IsVisible()&&top==this&&item->IsVisible()),int(!(item->iFlags&(QMF_GRAYED|QMF_INACTIVE))),int(item->IsCurrentSelected()));
        }
        m_lobby.Dump("lobby");m_play.Dump();m_inventory.Dump();m_settings.Dump();m_skins.Dump();m_gloves.Dump();m_accessories.Dump();
    }
    void DebugPage(const char *name)
    {
        if(!gpGlobals->developer)return;
        if(!std::strcmp(name,"play"))Navigate(0);else if(!std::strcmp(name,"inventory")||!std::strcmp(name,"loadout"))Navigate(1);
        else if(!std::strcmp(name,"settings"))Navigate(2);else if(!std::strcmp(name,"home")||!std::strcmp(name,"pause"))Navigate(-1);
    }
    void OpenSkin(int weapon){Navigate(1);m_inventory.OpenWeapon(weapon);}
    void OpenKnife(int definition){Navigate(1);m_inventory.OpenKnife(definition);}
    void OpenGloves(){Navigate(1);m_inventory.OpenGloves();}
    void OpenAccessory(int weapon){if(oc2_accessories::Index(weapon)<0)return;Navigate(1);m_inventory.OpenWeapon(weapon);m_accessories.Open(weapon);}
private:
    void _Init() override
    {
        m_lobby.SetNameAndStatus("",nullptr,"lobby_model");m_lobby.SetInteractive(false);AddItem(m_lobby);m_chrome.Init(*this,-1);
        Button(m_playButton,"开始游戏","home_play");m_playButton.onReleased=UI_PanoramaOpenPlay;
        Button(m_inventoryButton,"前往物品栏","home_inventory");m_inventoryButton.onReleased=UI_PanoramaOpenInventory;
        Button(m_agentButton,"大厅人物","lobby_agent");m_agentButton.onReleased=VoidCb(&CMenuModernMain::ChangeAgent);
        Button(m_weaponButton,"大厅武器","lobby_weapon");m_weaponButton.onReleased=VoidCb(&CMenuModernMain::NextWeapon);
        Button(m_weaponPrevious,"<","lobby_weapon_previous");m_weaponPrevious.onReleased=VoidCb(&CMenuModernMain::PreviousWeapon);
        Button(m_weaponNext,">","lobby_weapon_next");m_weaponNext.onReleased=VoidCb(&CMenuModernMain::NextWeapon);
        m_restart.icon=4;m_restart.SetNameAndStatus("重新开始回合",nullptr,"pause_restart");m_restart.onReleased=VoidCb(&CMenuModernMain::AskRestart);AddItem(m_restart);
        m_team.icon=5;m_team.SetNameAndStatus("选择阵营",nullptr,"pause_team");m_team.onReleased=VoidCb(&CMenuModernMain::ChangeTeam);AddItem(m_team);
        m_play.Link(this);m_inventory.Link(this);m_settings.Link(this);m_settings.SetBack(UI_PanoramaPause);
        m_skins.Link(&m_inventory);m_accessories.Link(&m_inventory);m_gloves.Link(&m_inventory);
        m_skins.SetAccessories(&m_accessories);m_inventory.SetEditors(&m_skins,&m_accessories,&m_gloves);
        m_confirm.Link(this);m_confirm.onPositive=VoidCb(&CMenuModernMain::ConfirmAction);
    }
    void _VidInit() override
    {
        // This rect anchors the portrait and its input region. The animated
        // renderer draws out to the full UI boundary, including raised guns.
        const int width=uiStatic.width;m_chrome.Layout();m_lobby.SetRect(int(width*.58f),52,int(width*.40f),606);
        m_playButton.SetRect(112,340,int(width*.36f),52);m_inventoryButton.SetRect(112,414,int(width*.36f),52);
        const int left=int(width*.61f),w=int(width*.34f);
        m_agentButton.SetRect(left,668,w,38);m_weaponPrevious.SetRect(left,716,36,36);
        m_weaponButton.SetRect(left+44,716,w-88,36);m_weaponNext.SetRect(left+w-36,716,36,36);
        m_restart.SetRect(0,76,64,64);m_team.SetRect(0,145,64,64);
    }
    void HidePages()
    {
        if(m_skins.IsVisible())m_skins.Hide();if(m_accessories.IsVisible())m_accessories.Hide();if(m_gloves.IsVisible())m_gloves.Hide();
        if(m_play.IsVisible())m_play.Hide();if(m_inventory.IsVisible())m_inventory.Hide();if(m_settings.IsVisible())m_settings.Hide();
    }
    void RefreshLobby()
    {
        const int agent=EngFuncs::GetCvarFloat("cl_lobby_agent")>=.5f?1:0;int weapon=int(EngFuncs::GetCvarFloat("cl_lobby_weapon"));
        if(oc2_accessories::Index(weapon)<0)weapon=28;
        m_agentButton.SetText(agent?"大厅人物：反恐特勤组":"大厅人物：凤凰战士");
        m_weaponLabel=std::string("大厅武器：")+oc2_accessories::Name(weapon);m_weaponButton.SetText(m_weaponLabel.c_str());
        m_lobby.SetLobby(agent?2:1,weapon);m_lobby.ReloadConfiguration();OC2_RestartLobbyPreview();
    }
    void ChangeAgent(){EngFuncs::CvarSetValue("cl_lobby_agent",EngFuncs::GetCvarFloat("cl_lobby_agent")>=.5f?0:1);RefreshLobby();EngFuncs::ClientCmd(false,"host_writeconfig\n");}
    void ChangeWeapon(int direction)
    {
        int index=oc2_accessories::Index(int(EngFuncs::GetCvarFloat("cl_lobby_weapon")));if(index<0)index=0;
        index=(index+direction+24)%24;EngFuncs::CvarSetValue("cl_lobby_weapon",oc2_accessories::kWeaponIds[index]);RefreshLobby();EngFuncs::ClientCmd(false,"host_writeconfig\n");
    }
    void NextWeapon(){ChangeWeapon(1);}void PreviousWeapon(){ChangeWeapon(-1);}
    void AskRestart(){m_pending=2;m_confirm.SetMessage("重新开始当前回合？");m_confirm.Show();}
    void ChangeTeam(){UI_CloseMenu();EngFuncs::ClientCmd(false,"wait;chooseteam\n");}
    void ConfirmAction()
    {
        if(m_pending==2){EngFuncs::ClientCmd(false,"sv_restartround 1\n");UI_CloseMenu();}
        else if(m_pending==1){HidePages();EngFuncs::ClientCmd(false,"disconnect\n");}
        else EngFuncs::ClientCmd(false,"quit\n");
    }
    Point Pixel(int x,int y)const{return GetRenderPosition()+Point(x,y).Scale();}
    void Fill(int x,int y,int w,int h,unsigned c){UI_FillRect(Pixel(x,y),Size(w,h).Scale(),c);}
    void Text(int x,int y,int w,int h,const char *s,int size,bool bold=false){oc2_panorama::DrawText(Pixel(x,y),Size(w,h).Scale(),s,size,PackRGBA(245,248,250,255),bold,QM_CENTER);}
    void Button(CMenuAction &button,const char *label,const char *tag)
    {
        button.SetNameAndStatus(label,nullptr,tag);oc2_panorama::CenterButton(button);button.colorBase=PackRGBA(250,252,255,255);button.colorFocus=PackRGBA(255,226,159,255);
        button.SetBackground(PackRGBA(39,56,68,215),PackRGBA(77,101,117,240));AddItem(button);
    }
    int m_page=-1,m_pending=0;bool m_ingame=false;
    std::string m_weaponLabel;
    CMenuPanoramaChrome m_chrome;CMenuPanoramaModelView m_lobby;
    CMenuPanoramaNav m_restart,m_team;
    CMenuAction m_playButton,m_inventoryButton,m_agentButton,m_weaponButton,m_weaponPrevious,m_weaponNext;
    CMenuPanoramaPlay m_play;CMenuPanoramaInventory m_inventory;CMenuPanoramaSettings m_settings;
    CMenuSkinEditor m_skins;CMenuAccessoriesEditor m_accessories;CMenuGloveEditor m_gloves;CMenuPanoramaConfirm m_confirm;
};
