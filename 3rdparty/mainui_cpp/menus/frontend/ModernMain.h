#pragma once
#include "Framework.h"
#include "Action.h"
#include "YesNoMessageBox.h"
#include "FrontendLayout.h"
#include "InventoryModel.h"
#include "AccessoriesEditor.h"
#include "keydefs.h"
#include <cstdio>
#include <cstring>

// CS:GO's csgo_mainmenu.cpp separates main-menu and pause-menu state and
// releases focus with the owning root. This native shell follows that boundary;
// it never changes gameplay/HUD cvars or requires a running client DLL/Steam.
class CMenuModernMain : public CMenuFramework
{
public:
    enum Page { Play, Inventory, Loadout };
    CMenuModernMain():CMenuFramework("CMenuModernMain"),m_page(Play),m_layout(1024),m_revision(~std::uint64_t(0))
    {
        for(int i=0;i<8;++i)m_cardIds[i]=0;
    }

    void Show() override
    {
        m_page=Play;
        CMenuFramework::Show();
        m_inventory.RequestRefresh();
        m_inventory.Tick();
        RefreshContents();
        SetCursorToItem(m_navPlay);
    }

    bool KeyDown(int key) override
    {
        m_lastKey=key;++m_keyEvents;
        if(UI::Key::IsEscape(key))
        {
            if(m_page!=Play)SetPage(Play);
            else QuitDialog();
            return true;
        }
        return CMenuFramework::KeyDown(key);
    }

    bool KeyUp(int key) override
    {
        m_lastKeyUp=key;++m_keyUpEvents;
        return CMenuFramework::KeyUp(key);
    }

    void Draw() override
    {
        if(CL_IsActive())return;
        // Restrained dark glass on the existing game background, with a solid
        // header/footer and readable content surfaces at both target sizes.
        UI_FillRect(0,0,ScreenWidth,ScreenHeight,PackRGBA(9,14,20,234));
        Fill(0,0,m_layout.width,108,PackRGBA(12,18,25,245));
        Fill(40,106,m_layout.width-80,1,PackRGBA(110,130,146,65));
        Fill(0,712,m_layout.width,56,PackRGBA(9,14,20,245));
        Text(40,34,130,48,"OC2",40,PackRGBA(238,232,212,255),true);
        Text(43,78,140,18,"CLASSIC 2",11,Muted());
        Panel(m_layout.content);
        Panel(m_layout.profile);
        DrawProfile();
        if(m_page==Play)DrawPlay();
        else if(m_page==Inventory)DrawInventory();
        else DrawLoadout();
        Text(40,728,500,24,"TAB / ARROWS  Navigate       ENTER  Select",14,Muted());
        Text(592,728,164,24,"ESC  Back",14,Muted());
        CMenuFramework::Draw();
    }

    void Dump()
    {
        CMenuBaseWindow *top=WindowStack()->Current();
        const CMenuBaseItem *focus=top?top->ItemAtCursor():nullptr;
        const char *focusTag=focus && focus->szTag?focus->szTag:"none";
        const auto &snapshot=m_inventory.Snapshot();
        // mainui's legacy variadic formatter does not implement 64-bit length
        // modifiers. Format IDs with the standard library before forwarding.
        char state[1024];
        const std::string revision=std::to_string(snapshot.revision);
        const std::string selected=std::to_string(m_inventory.SelectedId());
        std::snprintf(state,sizeof(state),"frontend: visible=%d page=%s screen=%dx%d focus=%s modal=%d connected=%d client_active=%d cursor=%d/%d stack_depth=%d top_window=%s inventory_status=%s owned=%u revision=%s selected=%s units=screen_pixels\n",
            int(IsVisible()),PageName(),int(ScreenWidth),int(ScreenHeight),focusTag,int(m_quitDialog.IsVisible()),
            int(CL_IsActive()),int(uiStatic.client.IsActive()),uiStatic.cursorX,uiStatic.cursorY,uiStatic.menu.Count(),
            top && top->szName?top->szName:"none",StatusName(snapshot.status),unsigned(snapshot.items.size()),
            revision.c_str(),selected.c_str());
        Con_Printf("%s",state);
        Con_Printf("frontend_config: volume=%.6f sensitivity=%.6f gl_vsync=%.0f\n",
            EngFuncs::GetCvarFloat("volume"),EngFuncs::GetCvarFloat("sensitivity"),EngFuncs::GetCvarFloat("gl_vsync"));
        Con_Printf("frontend_input: last_key=%d key_events=%u last_up=%d up_events=%u\n",
            m_lastKey,m_keyEvents,m_lastKeyUp,m_keyUpEvents);
        for(int i=0;i<ItemCount();++i)
        {
            CMenuBaseItem *item=GetItemByIndex(i);
            if(item && item->szTag)DumpItem(item->szTag,*item,IsVisible() && item->IsVisible());
        }
        DumpItem("exit_yes",m_quitDialog.yes,m_quitDialog.IsVisible());
        DumpItem("exit_no",m_quitDialog.no,m_quitDialog.IsVisible());
        Con_Printf("frontend_customization: weapons=24 page=%d filter=%d source=local_configuration\n",m_localPage,m_localFilter);
        m_accessories.Dump();
    }

    void OpenAccessory(int weapon)
    {
        if(CL_IsActive()||oc2_accessories::Index(weapon)<0)return;
        if(!IsVisible())Show();
        m_accessories.Open(weapon);
    }

    void DebugPage(const char *name)
    {
        if(!gpGlobals->developer || CL_IsActive() || WindowStack()->Current()!=this)return;
        if(!std::strcmp(name,"play"))SetPage(Play);
        else if(!std::strcmp(name,"inventory"))SetPage(Inventory);
        else if(!std::strcmp(name,"loadout"))SetPage(Loadout);
    }

private:
    static unsigned int Ink(){return PackRGBA(234,239,243,255);}
    static unsigned int Muted(){return PackRGBA(159,174,187,255);}
    static unsigned int Accent(){return PackRGBA(221,193,124,255);}
    const char *PageName()const{return m_page==Play?"play":m_page==Inventory?"inventory":"loadout";}
    static const char *StatusName(oc2_frontend::InventoryStatus status)
    {
        switch(status)
        {
        case oc2_frontend::InventoryStatus::Loading:return "loading";
        case oc2_frontend::InventoryStatus::Ready:return "ready";
        case oc2_frontend::InventoryStatus::Error:return "error";
        default:return "unavailable";
        }
    }
    Point Pixel(int x,int y)const{return GetRenderPosition()+Point(x,y).Scale();}
    void Fill(int x,int y,int w,int h,unsigned int color)
    {
        UI_FillRect(Pixel(x,y),Size(w,h).Scale(),color);
    }
    void Text(int x,int y,int w,int h,const char *text,int height,unsigned int color,
              bool bold=false,unsigned int align=QM_LEFT)
    {
        UI_DrawString(bold?uiStatic.hBoldFont:uiStatic.hDefaultFont,Pixel(x,y),Size(w,h).Scale(),
            text,color,Q_max(1,int(height*uiStatic.scaleY)),align,ETF_FORCECOL|ETF_NO_WRAP);
    }
    void Panel(const oc2_frontend::FrontendRect &r)
    {
        Fill(r.x,r.y,r.w,r.h,PackRGBA(24,33,43,224));
        Fill(r.x,r.y,r.w,1,PackRGBA(110,135,155,60));
    }
    static void DumpItem(const char *tag,CMenuBaseItem &item,bool visible)
    {
        const Point p=item.GetRenderPosition();const Size z=item.GetRenderSize();
        Con_Printf("frontend_item: tag=%s x=%d y=%d w=%d h=%d visible=%d enabled=%d focus=%d flags=%u pressed=%d units=screen_pixels\n",
            tag,p.x,p.y,z.w,z.h,int(visible),int(!(item.iFlags&(QMF_GRAYED|QMF_INACTIVE))),int(item.IsCurrentSelected()),
            item.iFlags,int(item.IsPressed()));
    }
    void Button(CMenuAction &button,const char *label,const char *tag,bool primary=false)
    {
        button.SetNameAndStatus(label,nullptr,tag);
        button.SetCharSize(QM_SMALLFONT);
        button.SetTextInsetX(18);
        button.eTextAlignment=QM_LEFT;
        button.eFocusAnimation=QM_HIGHLIGHTIFFOCUS;
        button.bIgnoreColorstring=true;
        button.colorBase=primary?PackRGBA(20,26,31,255):Ink();
        button.colorFocus=primary?PackRGBA(8,12,17,255):Accent();
        button.SetBackground(primary?Accent():PackRGBA(39,51,63,240),
            primary?PackRGBA(239,216,160,255):PackRGBA(54,70,84,255));
        AddItem(button);
    }
    static void Place(CMenuBaseItem &item,const oc2_frontend::FrontendRect &r)
    {
        item.SetRect(r.x,r.y,r.w,r.h);
    }
    void _Init() override
    {
        Button(m_navPlay,"PLAY","nav_play");
        Button(m_navInventory,"INVENTORY","nav_inventory");
        Button(m_navLoadout,"LOADOUT","nav_loadout");
        Button(m_settings,"SETTINGS","settings");
        Button(m_exit,"EXIT","exit");
        m_navPlay.onReleased=VoidCb(&CMenuModernMain::OpenPlay);
        m_navInventory.onReleased=VoidCb(&CMenuModernMain::OpenInventory);
        m_navLoadout.onReleased=VoidCb(&CMenuModernMain::OpenLoadout);
        m_settings.onReleased=UI_Options_Menu;
        m_exit.onReleased=VoidCb(&CMenuModernMain::QuitDialog);
        Button(m_local,"LOCAL MATCH","play_local",true);
        Button(m_internet,"SERVER BROWSER","play_internet");
        Button(m_lan,"LAN SERVERS","play_lan");
        Button(m_multiplayer,"More multiplayer options","play_multiplayer");
        Button(m_player,"EDIT PLAYER PROFILE","player_profile");
        m_local.onReleased=UI_CreateGame_Menu;
        m_internet.onReleased=UI_InternetGames_Menu;
        m_lan.onReleased=UI_LanGame_Menu;
        m_multiplayer.onReleased=UI_MultiPlayer_Menu;
        m_player.onReleased=UI_PlayerSetup_Menu;
        Button(m_refresh,"RELOAD LIST","inventory_refresh");
        m_refresh.onReleased=VoidCb(&CMenuModernMain::RefreshInventory);
        Button(m_filters[0],"ALL","filter_all");
        Button(m_filters[1],"PISTOLS","filter_weapons");
        Button(m_filters[2],"OTHER GUNS","filter_equipment");
        m_filters[0].onReleased=VoidCb(&CMenuModernMain::FilterAll);
        m_filters[1].onReleased=VoidCb(&CMenuModernMain::FilterWeapons);
        m_filters[2].onReleased=VoidCb(&CMenuModernMain::FilterEquipment);
        Button(m_previous,"PREVIOUS","inventory_previous");
        Button(m_next,"NEXT","inventory_next");
        m_previous.onReleased=VoidCb(&CMenuModernMain::PreviousPage);
        m_next.onReleased=VoidCb(&CMenuModernMain::NextPage);
        for(int i=0;i<8;++i)
        {
            std::snprintf(m_cardTags[i],sizeof(m_cardTags[i]),"inventory_card_%d",i);
            Button(m_cards[i],"",m_cardTags[i]);
            SET_EVENT_MULTI(m_cards[i].onReleased,{
                static_cast<CMenuModernMain*>(pSelf->Parent())->SelectCard(pSelf);
            });
        }
        m_quitDialog.Link(this);
        m_quitDialog.SetMessage("Quit Offensive Classic 2?");
        m_quitDialog.onPositive.SetCommand(false,"quit\n");
        m_quitDialog.yes.szTag="exit_yes";
        m_quitDialog.no.szTag="exit_no";
        m_accessories.Link(this);
    }
    void _VidInit() override
    {
        m_layout=oc2_frontend::FrontendLayout(uiStatic.width);
        Place(m_navPlay,m_layout.navPlay);Place(m_navInventory,m_layout.navInventory);
        Place(m_navLoadout,m_layout.navLoadout);Place(m_settings,m_layout.settings);Place(m_exit,m_layout.exit);
        Place(m_local,m_layout.local);Place(m_internet,m_layout.internet);Place(m_lan,m_layout.lan);
        Place(m_multiplayer,m_layout.multiplayer);Place(m_player,m_layout.player);
        Place(m_refresh,m_layout.refresh);Place(m_previous,m_layout.previous);Place(m_next,m_layout.next);
        for(int i=0;i<3;++i)Place(m_filters[i],m_layout.filters[i]);
        for(int i=0;i<8;++i)Place(m_cards[i],m_layout.cards[i]);
        m_inventory.SetPage(m_inventory.Page().page,m_layout.pageSize);
        RefreshContents();
    }
    void Think() override
    {
        m_inventory.Tick();
        const auto &snapshot=m_inventory.Snapshot();
        if(m_revision!=snapshot.revision || m_status!=snapshot.status)RefreshContents();
        CMenuFramework::Think();
    }
    void SetPage(Page page)
    {
        m_page=page;RefreshContents();
        SetCursorToItem(page==Play?m_navPlay:page==Inventory?m_navInventory:m_navLoadout);
    }
    void OpenPlay(){SetPage(Play);}
    void OpenInventory(){SetPage(Inventory);}
    void OpenLoadout(){SetPage(Loadout);}
    void QuitDialog(){m_quitDialog.Show();m_quitDialog.SetCursorToItem(m_quitDialog.no);}
    void RefreshInventory(){RefreshContents();}
    void Filter(oc2_frontend::InventoryCategory category)
    {
        m_localFilter=category==oc2_frontend::InventoryCategory::All?0:
            category==oc2_frontend::InventoryCategory::Weapons?1:2;
        m_localPage=0;RefreshContents();
    }
    void FilterAll(){Filter(oc2_frontend::InventoryCategory::All);}
    void FilterWeapons(){Filter(oc2_frontend::InventoryCategory::Weapons);}
    void FilterEquipment(){Filter(oc2_frontend::InventoryCategory::Equipment);}
    void PreviousPage()
    {
        if(m_localPage>0)--m_localPage;
        RefreshContents();
    }
    void NextPage()
    {
        const int pages=(int(LocalWeapons().size())+m_layout.pageSize-1)/m_layout.pageSize;
        if(m_localPage+1<pages)++m_localPage;
        RefreshContents();
    }
    void SelectCard(CMenuBaseItem *button)
    {
        for(int i=0;i<8;++i)if(button==&m_cards[i] && m_cardIds[i])OpenAccessory(int(m_cardIds[i]));
    }
    std::vector<int> LocalWeapons() const
    {
        std::vector<int> ids;
        for(int id:oc2_accessories::kWeaponIds)
        {
            const bool pistol=id==1||id==10||id==11||id==16||id==17||id==26;
            if(!m_localFilter||(m_localFilter==1&&pistol)||(m_localFilter==2&&!pistol))ids.push_back(id);
        }
        return ids;
    }
    void RefreshContents()
    {
        const bool play=m_page==Play,inventory=m_page!=Play;
        m_local.SetVisibility(play);m_internet.SetVisibility(play);m_lan.SetVisibility(play);
        m_multiplayer.SetVisibility(play);m_player.SetVisibility(true);
        m_refresh.SetVisibility(inventory);
        for(int i=0;i<3;++i)m_filters[i].SetVisibility(inventory);
        const auto ids=LocalWeapons();const int pages=(int(ids.size())+m_layout.pageSize-1)/m_layout.pageSize;
        if(m_localPage>=pages)m_localPage=Q_max(0,pages-1);
        m_previous.SetVisibility(inventory && pages>1);m_next.SetVisibility(inventory && pages>1);
        m_previous.SetGrayed(m_localPage==0);m_next.SetGrayed(m_localPage+1>=pages);
        for(int i=0;i<8;++i)
        {
            const int index=m_localPage*m_layout.pageSize+i;
            const bool visible=inventory&&i<m_layout.pageSize&&index<int(ids.size());
            m_cards[i].SetVisibility(visible);m_cardIds[i]=visible?ids[index]:0;
            if(visible)m_cards[i].SetText(oc2_accessories::Name(int(m_cardIds[i])));
        }
        m_navPlay.colorBase=play?Accent():Ink();
        m_navInventory.colorBase=m_page==Inventory?Accent():Ink();
        m_navLoadout.colorBase=m_page==Loadout?Accent():Ink();
        const auto &snapshot=m_inventory.Snapshot();
        m_revision=snapshot.revision;m_status=snapshot.status;
    }
    void DrawPlay()
    {
        const auto &r=m_layout.content;
        Text(r.x+24,r.y+24,r.w-48,24,"PLAY",16,Accent(),true);
        Text(r.x+24,r.y+72,r.w-48,62,"COUNTER-STRIKE",44,Ink(),true);
        Text(r.x+26,r.y+138,r.w-52,32,"OFFENSIVE CLASSIC 2",22,Muted());
        Fill(r.x+26,r.y+194,64,3,Accent());
        Text(r.x+24,r.y+220,r.w-48,48,"Create a local match or join a community server.",21,Ink());
        Text(r.x+24,r.y+274,r.w-48,22,"CHOOSE HOW TO PLAY",13,Muted());
    }
    void DrawProfile()
    {
        const auto &r=m_layout.profile;
        Text(r.x+20,r.y+24,r.w-40,24,"LOCAL PLAYER",14,Muted(),true);
        Fill(r.x+20,r.y+66,52,52,PackRGBA(58,75,89,255));
        Text(r.x+20,r.y+66,52,52,"P",30,Accent(),true,QM_CENTER);
        const char *name=EngFuncs::GetCvarString("name");
        Text(r.x+20,r.y+138,r.w-40,42,name && name[0]?name:"Player",26,Ink(),true);
        Text(r.x+20,r.y+182,r.w-40,24,"PLAYER PROFILE",12,Muted());
        Fill(r.x+20,r.y+294,r.w-40,1,PackRGBA(116,135,151,50));
        Text(r.x+20,r.y+316,r.w-40,24,"YOUR INVENTORY",14,Muted(),true);
        Text(r.x+20,r.y+354,r.w-40,32,"24 weapon setups",22,Ink());
        Text(r.x+20,r.y+410,r.w-40,50,"Local customization",17,Muted());
        Text(r.x+20,r.y+458,r.w-40,60,"StatTrak / sticker charms",17,Accent());
    }
    void DrawInventory()
    {
        const auto &r=m_layout.content;
        Text(r.x+24,r.y+24,r.w-48,24,"INVENTORY",16,Accent(),true);
        Text(r.x+24,r.y+58,r.w-232,48,"Weapon customization",30,Ink(),true);
        const int pages=(int(LocalWeapons().size())+m_layout.pageSize-1)/m_layout.pageSize;
        char value[96];std::snprintf(value,sizeof(value),"Local configurations  /  Page %d / %d",m_localPage+1,Q_max(1,pages));
        Text(r.x+24,r.y+458,r.w-48,24,"Select a weapon to edit its saved StatTrak and charm settings.",15,Muted());
        Text(r.x+r.w-340,r.y+494,316,38,value,14,Muted(),false,QM_RIGHT);
    }
    void DrawLoadout()
    {
        DrawInventory();
        const auto &r=m_layout.content;
        Fill(r.x+16,r.y+16,220,36,PackRGBA(24,33,43,255));
        Text(r.x+24,r.y+24,212,24,"LOCAL LOADOUT",16,Accent(),true);
    }

    Page m_page;
    oc2_frontend::FrontendLayout m_layout;
    oc2_frontend::InventoryModel m_inventory;
    std::uint64_t m_revision;
    oc2_frontend::InventoryStatus m_status=oc2_frontend::InventoryStatus::Unavailable;
    CMenuAction m_navPlay,m_navInventory,m_navLoadout,m_settings,m_exit;
    CMenuAction m_local,m_internet,m_lan,m_multiplayer,m_player,m_refresh;
    CMenuAction m_filters[3],m_cards[8],m_previous,m_next;
    std::uint64_t m_cardIds[8];
    char m_cardTags[8][32];
    CMenuYesNoMessageBox m_quitDialog;
    CMenuAccessoriesEditor m_accessories;
    int m_localPage=0,m_localFilter=0;
    int m_lastKey=0;
    unsigned int m_keyEvents=0;
    int m_lastKeyUp=0;
    unsigned int m_keyUpEvents=0;
};
