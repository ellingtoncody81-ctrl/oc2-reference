#pragma once
#include "Framework.h"
#include "Action.h"
#include "Field.h"
#include "YesNoMessageBox.h"
#include "SkinInventory.h"
#include "AccessoriesEditor.h"
#include "keydefs.h"

class CMenuSkinEditor : public CMenuFramework
{
public:
    CMenuSkinEditor():CMenuFramework("CMenuSkinEditor"){for(int &id:m_rowIds)id=-1;}
    void SetAccessories(CMenuAccessoriesEditor *editor){m_accessories=editor;}
    void OpenWeapon(int weapon)
    {
        if(CL_IsActive()||oc2_accessories::Index(weapon)<0)return;
        m_weapon=weapon;m_definition=0;m_knife=false;Open();
    }
    void OpenKnife(int definition)
    {
        if(CL_IsActive()||definition<0)return;
        m_weapon=29;m_definition=definition;m_knife=true;Open();
    }
    void OpenSticker(int weapon,int slot)
    {
        if(slot<0||slot>=4)return;
        OpenWeapon(weapon);m_tab=slot+1;m_page=0;RefreshRows();
    }
    bool KeyDown(int key) override
    {
        if(UI::Key::IsEscape(key)){Back();return true;}
        return CMenuFramework::KeyDown(key);
    }
    void Draw() override
    {
        if(CL_IsActive())return;
        const int w=uiStatic.width,right=Right(),rw=w-right-48,lw=right-72;
        UI_FillRect(0,0,ScreenWidth,ScreenHeight,PackRGBA(9,14,20,250));
        Fill(32,110,w-64,578,PackRGBA(23,33,44,250));
        Text(48,30,w-96,44,Title().c_str(),32,Ink(),true);
        Text(48,78,w-96,22,"LOCAL LOADOUT   /   shared by both teams",15,Muted());
        Text(48,180,lw,22,"SEARCH NAME OR COLLECTION",12,Muted());
        Text(right,188,rw,22,TabName(),15,Accent(),true);
        const int selection=Selection();const oc2_inventory::Entry *entry=SelectedEntry();
        if(entry&&!oc2_inventory::IsTransparent(*entry))
        {
            oc2_local_inventory::DrawImage(entry->preview,Pixel(right,230),Size(rw,272).Scale());
            Text(right,510,rw,22,"TEXTURE PREVIEW",12,Muted());
            Text(right,540,rw,48,entry->name.c_str(),21,Ink(),true);
            Text(right,594,rw,44,entry->collection.c_str(),14,Muted());
        }
        else if(selection==-1)
        {
            Text(right,290,rw,54,"UNEQUIPPED",29,Accent(),true);
            Text(right,365,rw,70,m_tab==5?"The entire charm is removed.":"Only this sticker slot is cleared.",18,Ink());
        }
        else if(!m_knife&&m_tab==0)
        {
            char path[96];std::snprintf(path,sizeof(path),"gfx/inventory/previews/weapon_%d.tga",m_weapon);
            oc2_local_inventory::DrawImage(path,Pixel(right,240),Size(rw,258).Scale());
            Text(right,510,rw,22,"ORIGINAL MODEL REFERENCE",12,Muted());
            Text(right,550,rw,48,"Original skin",24,Ink(),true);
        }
        else
        {
            Text(right,290,rw,62,DefaultLabel(),25,Ink(),true);
            Text(right,375,rw,82,m_tab==5?"Uses the pattern and ON/OFF setting from Accessories.":
                m_knife?"Keeps this knife's original textures.":"Keeps this slot's original model material.",17,Muted());
        }
        char page[96];std::snprintf(page,sizeof(page),"%u options   /   Page %d / %d",unsigned(m_options.size()),m_page+1,Pages());
        Text(48,660,lw,20,page,13,Muted());
        Text(right,654,rw,28,m_knife?"EQUIP + SAVE applies this knife to both teams.":"Select a pattern, then APPLY to save.",13,Muted());
        Text(196,700,Q_max(100,w-720),38,m_message.c_str(),13,m_error?PackRGBA(246,133,119,255):Muted());
        if(m_dirty)Text(w-380,78,332,22,"Unsaved changes",14,Accent(),false,QM_RIGHT);
        CMenuFramework::Draw();
    }
    void Dump()
    {
        const std::string instance=std::to_string(m_knife?m_knifeDraft.instance:m_draft.instance);
        char line[1024];std::snprintf(line,sizeof(line),
            "skin_ui: visible=%d weapon=%d knife=%d definition=%d tab=%d page=%d pages=%d options=%u selection=%d dirty=%d catalog_valid=%d config_valid=%d revision=%u error=%d instance=%s discard_modal=%d\n",
            int(IsVisible()),m_weapon,int(m_knife),m_definition,m_tab,m_page,Pages(),unsigned(m_options.size()),Selection(),int(m_dirty),
            int(m_catalogValid),int(m_configValid),m_revision,int(m_error),instance.c_str(),int(m_discard.IsVisible()));
        Con_Printf("%s",line);
        Con_Printf("skin_draft: weapon=%d skin=%d sticker_a=%d sticker_b=%d sticker_c=%d sticker_d=%d charm=%d knife_skin=%d\n",
            m_weapon,m_draft.skin,m_draft.stickers[0],m_draft.stickers[1],m_draft.stickers[2],m_draft.stickers[3],m_draft.charm,m_knifeDraft.skin);
        for(int i=0;i<8;++i)if(m_rowIds[i]>=0)
            Con_Printf("skin_option: row=%d id=%d selected=%d\n",i,m_rowIds[i],int(IsSelected(m_rowIds[i])));
        for(int i=0;i<ItemCount();++i)
        {
            CMenuBaseItem *item=GetItemByIndex(i);if(!item||!item->szTag)continue;
            const Point p=item->GetRenderPosition();const Size z=item->GetRenderSize();
            Con_Printf("frontend_item: tag=%s x=%d y=%d w=%d h=%d visible=%d enabled=%d focus=%d units=screen_pixels\n",
                item->szTag,p.x,p.y,z.w,z.h,int(IsVisible()&&item->IsVisible()),int(!(item->iFlags&(QMF_GRAYED|QMF_INACTIVE))),int(item->IsCurrentSelected()));
        }
        CMenuBaseItem *answers[]={&m_discard.yes,&m_discard.no};
        for(auto *item:answers)
        {
            const Point p=item->GetRenderPosition();const Size z=item->GetRenderSize();
            Con_Printf("frontend_item: tag=%s x=%d y=%d w=%d h=%d visible=%d enabled=1 focus=%d units=screen_pixels\n",
                item->szTag,p.x,p.y,z.w,z.h,int(m_discard.IsVisible()),int(item->IsCurrentSelected()));
        }
    }
private:
    static unsigned Ink(){return PackRGBA(234,239,243,255);}
    static unsigned Muted(){return PackRGBA(159,174,187,255);}
    static unsigned Accent(){return PackRGBA(221,193,124,255);}
    int Right()const{return uiStatic.width*57/100;}
    int Pages()const{return Q_max(1,(int(m_options.size())+7)/8);}
    Point Pixel(int x,int y)const{return GetRenderPosition()+Point(x,y).Scale();}
    void Fill(int x,int y,int w,int h,unsigned color){UI_FillRect(Pixel(x,y),Size(w,h).Scale(),color);}
    void Text(int x,int y,int w,int h,const char *text,int height,unsigned color,bool bold=false,unsigned align=QM_LEFT)
    {
        UI_DrawString(bold?uiStatic.hBoldFont:uiStatic.hDefaultFont,Pixel(x,y),Size(w,h).Scale(),text,color,
            Q_max(1,int(height*uiStatic.scaleY)),align,ETF_FORCECOL|ETF_NO_WRAP);
    }
    void Button(CMenuAction &button,const char *text,const char *tag)
    {
        button.SetNameAndStatus(text,nullptr,tag);button.SetCharSize(QM_SMALLFONT);button.SetTextInsetX(12);
        button.bIgnoreColorstring=true;button.eFocusAnimation=QM_HIGHLIGHTIFFOCUS;
        button.colorBase=Ink();button.colorFocus=Accent();
        button.SetBackground(PackRGBA(42,56,70,255),PackRGBA(65,82,98,255));AddItem(button);
    }
    std::string Title()const
    {
        if(!m_knife)return oc2_accessories::Name(m_weapon);
        const auto *entry=oc2_inventory::FindKnifeEntry(m_catalog,m_definition);
        return entry?entry->name:"Default T Knife";
    }
    const char *TabName()const
    {
        static const char *names[]={"SKIN","STICKER SLOT A","STICKER SLOT B","STICKER SLOT C","STICKER SLOT D","CHARM PATTERN"};
        return names[m_tab];
    }
    const char *DefaultLabel()const{return m_tab==5?"Use Accessories setting":m_tab?"Original slot material":"Original skin";}
    int Selection()const{return m_knife?m_knifeDraft.skin:m_tab==0?m_draft.skin:m_tab==5?m_draft.charm:m_draft.stickers[m_tab-1];}
    const oc2_inventory::Entry *SelectedEntry()const
    {
        if(Selection()==-1){for(const auto &entry:m_catalog)if(oc2_inventory::IsTransparent(entry))return &entry;return nullptr;}
        if(m_knife&&!Selection())return oc2_inventory::FindKnifeEntry(m_catalog,m_definition);
        return oc2_inventory::FindEntry(m_catalog,Selection());
    }
    bool IsSelected(int id)const
    {
        if(Selection()==id)return true;
        const auto *entry=oc2_inventory::FindEntry(m_catalog,id);
        return Selection()==-1&&entry&&oc2_inventory::IsTransparent(*entry);
    }
    void Open()
    {
        m_tab=0;m_page=0;m_returnAccessories=false;Reload();CMenuFramework::Show();m_search.SetBuffer("");m_query.clear();RefreshRows();SetCursorToItem(m_tabs[0]);
    }
    void Reload()
    {
        m_catalog.clear();std::string error;
        m_catalogValid=oc2_local_inventory::LoadCatalog(m_catalog,error);
        if(m_knife&&m_definition&&!oc2_inventory::FindKnifeEntry(m_catalog,m_definition))m_catalogValid=false;
        std::string configError;oc2_inventory::Config config;
        m_configValid=oc2_local_inventory::LoadConfig(config,configError);
        if(m_catalogValid&&m_configValid)m_configValid=oc2_inventory::ValidateConfig(config,m_catalog,configError);
        m_revision=config.revision;m_equipped=config.knife;m_draft=oc2_inventory::WeaponState();m_knifeDraft=oc2_inventory::KnifeState();
        if(m_knife)
        {
            if(auto *value=oc2_inventory::EnsureKnife(config,m_definition))m_knifeDraft=*value;
        }
        else if(const auto *value=oc2_inventory::Find(config,m_weapon))m_draft=*value;
        m_dirty=false;m_error=!m_catalogValid||!m_configValid;
        m_message=!m_catalogValid?(error.empty()?"Unknown knife definition.":error):!m_configValid?configError:"Saved local loadout loaded.";
    }
    void _Init() override
    {
        static const char *names[]={"SKIN","STICKER A","STICKER B","STICKER C","STICKER D","CHARM PATTERN"};
        static const char *tags[]={"skin_tab_skin","skin_tab_a","skin_tab_b","skin_tab_c","skin_tab_d","skin_tab_charm"};
        for(int i=0;i<6;++i)
        {
            Button(m_tabs[i],names[i],tags[i]);
            SET_EVENT_MULTI(m_tabs[i].onReleased,{static_cast<CMenuSkinEditor*>(pSelf->Parent())->SelectTab(pSelf);});
        }
        Button(m_accessoryButton,"ACCESSORIES","skin_accessories");m_accessoryButton.onReleased=VoidCb(&CMenuSkinEditor::OpenAccessories);
        Button(m_back,"BACK","skin_back");m_back.onReleased=VoidCb(&CMenuSkinEditor::Back);
        Button(m_revert,"REVERT","skin_revert");m_revert.onReleased=VoidCb(&CMenuSkinEditor::Revert);
        Button(m_apply,"APPLY","skin_apply");m_apply.onReleased=VoidCb(&CMenuSkinEditor::Apply);
        Button(m_previous,"< PREVIOUS","skin_previous");m_previous.onReleased=VoidCb(&CMenuSkinEditor::Previous);
        Button(m_next,"NEXT >","skin_next");m_next.onReleased=VoidCb(&CMenuSkinEditor::Next);
        for(int i=0;i<8;++i)
        {
            std::snprintf(m_rowTags[i],sizeof(m_rowTags[i]),"skin_option_%d",i);m_rowIds[i]=-1;
            Button(m_rows[i],"",m_rowTags[i]);
            SET_EVENT_MULTI(m_rows[i].onReleased,{static_cast<CMenuSkinEditor*>(pSelf->Parent())->SelectOption(pSelf);});
        }
        m_search.SetNameAndStatus("",nullptr,"skin_search");m_search.iMaxLength=80;m_search.bAllowColorstrings=false;AddItem(m_search);
        m_discard.Link(this);m_discard.SetMessage("Discard this item's unsaved skin and pattern changes?");
        m_discard.onPositive=VoidCb(&CMenuSkinEditor::Discard);
        m_discard.yes.szTag="skin_discard_yes";m_discard.no.szTag="skin_discard_no";
    }
    void _VidInit() override
    {
        const int w=uiStatic.width,lw=Right()-72;
        const int widths[]={96,96,96,96,96,142};int x=48;
        for(int i=0;i<6;++i){m_tabs[i].SetRect(x,126,widths[i],42);x+=widths[i]+10;}
        m_accessoryButton.SetRect(x,126,Q_min(160,w-x-48),42);
        m_search.SetRect(48,216,lw,28);
        for(int i=0;i<8;++i)m_rows[i].SetRect(48,256+i*43,lw,37);
        m_previous.SetRect(48,610,(lw-12)/2,36);m_next.SetRect(60+(lw-12)/2,610,(lw-12)/2,36);
        m_back.SetRect(48,700,128,36);m_revert.SetRect(w-370,700,140,36);m_apply.SetRect(w-210,700,162,36);
        RefreshRows();
    }
    void Think() override
    {
        if(m_returnAccessories&&WindowStack()->Current()==this){m_returnAccessories=false;Reload();RefreshRows();}
        const std::string query=oc2_local_inventory::Fold(m_search.GetBuffer());
        if(query!=m_query){m_query=query;m_page=0;RefreshRows();}
        CMenuFramework::Think();
    }
    void RefreshRows()
    {
        m_options.clear();const std::string original=oc2_local_inventory::Fold(DefaultLabel());
        if(m_query.empty()||original.find(m_query)!=std::string::npos)m_options.push_back(0);
        for(const auto &entry:m_catalog)
        {
            const bool matches=m_knife?(entry.kind==oc2_inventory::Kind::KnifeSkin&&entry.definition==m_definition):
                m_tab==0?(entry.kind==oc2_inventory::Kind::GunSkin&&entry.weapon==m_weapon):entry.kind==oc2_inventory::Kind::Sticker;
            if(!matches)continue;
            if(!m_query.empty()&&oc2_local_inventory::Fold(entry.name+" "+entry.collection).find(m_query)==std::string::npos)continue;
            m_options.push_back(entry.id);
        }
        m_page=Q_min(m_page,Pages()-1);
        for(int i=0;i<8;++i)
        {
            const int index=m_page*8+i;const bool visible=index<int(m_options.size());m_rows[i].SetVisibility(visible);m_rowIds[i]=visible?m_options[index]:-1;
            if(!visible)continue;
            const auto *entry=oc2_inventory::FindEntry(m_catalog,m_rowIds[i]);
            std::string name=entry?entry->name:DefaultLabel();if(IsSelected(m_rowIds[i]))name="[SELECTED] "+name;
            m_rows[i].SetText(name.c_str());m_rows[i].colorBase=IsSelected(m_rowIds[i])?Accent():Ink();
        }
        for(int i=0;i<6;++i){m_tabs[i].SetVisibility(!m_knife||!i);m_tabs[i].colorBase=m_tab==i?Accent():Ink();}
        m_accessoryButton.SetVisibility(!m_knife);
        m_previous.SetGrayed(m_page==0);m_next.SetGrayed(m_page+1>=Pages());m_apply.SetGrayed(!m_catalogValid||!m_configValid);
        m_apply.SetText(m_knife?"EQUIP + SAVE":"APPLY");
    }
    void SelectTab(CMenuBaseItem *button)
    {
        for(int i=0;i<6;++i)if(button==&m_tabs[i])m_tab=i;
        m_page=0;m_query.clear();m_search.SetBuffer("");RefreshRows();
    }
    void SelectOption(CMenuBaseItem *button)
    {
        for(int i=0;i<8;++i)if(button==&m_rows[i]&&m_rowIds[i]>=0)
        {
            const int id=m_rowIds[i];const auto *entry=oc2_inventory::FindEntry(m_catalog,id);
            if(m_knife)m_knifeDraft.skin=id;
            else if(!m_tab)m_draft.skin=id;
            else if(m_tab==5){if(entry)oc2_inventory::ApplyCharm(m_draft,*entry);else m_draft.charm=0;}
            else{if(entry)oc2_inventory::ApplySticker(m_draft,m_tab-1,*entry);else m_draft.stickers[m_tab-1]=0;}
            m_dirty=true;m_error=false;m_message="Selection changed. Apply to save.";RefreshRows();return;
        }
    }
    void Previous(){if(m_page>0)--m_page;RefreshRows();}
    void Next(){if(m_page+1<Pages())++m_page;RefreshRows();}
    void Revert(){Reload();RefreshRows();}
    void Apply()
    {
        if(!m_catalogValid||!m_configValid){m_error=true;m_message="Restore the saved files before saving.";return;}
        oc2_inventory::Config latest;std::string error;
        if(!oc2_local_inventory::LoadConfig(latest,error)||!oc2_inventory::ValidateConfig(latest,m_catalog,error))
            {m_error=true;m_message=error;return;}
        if(m_knife)
        {
            if(!oc2_inventory::EquipKnife(latest,m_definition)){m_error=true;m_message="Knife could not be equipped.";return;}
            if(m_definition){auto *state=oc2_inventory::FindKnife(latest,m_definition);if(state)state->skin=m_knifeDraft.skin;}
        }
        else
        {
            auto *state=oc2_inventory::Find(latest,m_weapon);
            if(!state){m_error=true;m_message="Unknown weapon.";return;}
            state->skin=m_draft.skin;state->stickers=m_draft.stickers;state->charm=m_draft.charm;
        }
        latest.revision=latest.revision==std::numeric_limits<unsigned>::max()?1:latest.revision+1;
        if(!oc2_inventory::ValidateConfig(latest,m_catalog,error)){m_error=true;m_message=error;return;}
        if(!oc2_local_inventory::SaveConfig(latest)){m_error=true;m_message="Save failed. Changes are still unsaved.";return;}
        m_revision=latest.revision;m_equipped=latest.knife;m_dirty=false;m_error=false;
        m_message=m_knife?"Knife equipped for both teams.":"Saved skin, four sticker slots and charm pattern.";
        RefreshRows();
    }
    void OpenAccessories()
    {
        if(m_dirty){m_error=false;m_message="Apply or Revert before opening Accessories.";return;}
        if(!m_knife&&m_accessories){m_returnAccessories=true;m_accessories->Open(m_weapon);}
    }
    void Back(){if(m_dirty){m_discard.Show();m_discard.SetCursorToItem(m_discard.no);}else Hide();}
    void Discard(){m_dirty=false;Hide();}
    int m_weapon=1,m_definition=0,m_tab=0,m_page=0,m_equipped=0;unsigned m_revision=1;
    bool m_knife=false,m_dirty=false,m_error=false,m_catalogValid=false,m_configValid=false,m_returnAccessories=false;
    std::string m_message,m_query;
    oc2_inventory::Catalog m_catalog;oc2_inventory::WeaponState m_draft;oc2_inventory::KnifeState m_knifeDraft;
    std::vector<int> m_options;int m_rowIds[8]{};char m_rowTags[8][32];
    CMenuAction m_tabs[6],m_rows[8],m_accessoryButton,m_back,m_revert,m_apply,m_previous,m_next;
    CMenuField m_search;CMenuYesNoMessageBox m_discard;CMenuAccessoriesEditor *m_accessories=nullptr;
};
