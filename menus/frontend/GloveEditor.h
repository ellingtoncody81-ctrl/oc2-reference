#pragma once
#include "Framework.h"
#include "Action.h"
#include "Field.h"
#include "YesNoMessageBox.h"
#include "SkinInventory.h"
#include "../../../../common/oc2_glove_inventory.h"
#include "keydefs.h"

namespace oc2_local_gloves
{
inline bool LoadCatalog(oc2_gloves::Catalog &catalog,std::string &error)
{
    std::string contents;
    if(!oc2_local_inventory::Read(oc2_gloves::kCatalogPath,contents))
        {error="Glove collection could not be read.";return false;}
    return oc2_gloves::ParseCatalog(contents,catalog,error);
}
inline bool LoadConfig(oc2_gloves::Config &config,std::string &error)
{
    config=oc2_gloves::DefaultConfig();std::string contents;
    if(oc2_local_inventory::Read(oc2_gloves::kConfigPath,contents))
        return oc2_gloves::ParseConfig(contents,config,error);
    if(!EngFuncs::FileExists(oc2_gloves::kConfigPath,true)){error.clear();return true;}
    error="Saved glove selection could not be read.";return false;
}
inline bool SaveConfig(const oc2_gloves::Config &config)
{
    const std::string contents=oc2_gloves::SerializeConfig(config);
    EngFuncs::COM_SaveFile(oc2_gloves::kConfigPath,contents.data(),int(contents.size()));
    std::string saved;return oc2_local_inventory::Read(oc2_gloves::kConfigPath,saved)&&saved==contents;
}
inline std::string SelectionName(const oc2_gloves::Catalog &catalog,int resource)
{
    if(!resource)return "Team default";
    const auto *entry=oc2_gloves::FindEntry(catalog,resource);
    return entry?entry->name:"Unavailable saved gloves";
}
}

class CMenuGloveEditor : public CMenuFramework
{
public:
    CMenuGloveEditor():CMenuFramework("CMenuGloveEditor"){for(int &id:m_rowIds)id=-1;}
    void Open()
    {
        if(CL_IsActive())return;
        m_family=0;m_page=0;Reload();
        if(const auto *entry=oc2_gloves::FindEntry(m_catalog,m_draft.resource))
            {const int family=FamilyIndex(entry->collection);if(family>=0)m_family=family;}
        CMenuFramework::Show();m_search.SetBuffer("");m_query.clear();
        RefreshRows();SetCursorToItem(m_families[m_family]);
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
        Text(48,30,w-96,44,"GLOVES",32,Ink(),true);
        Text(48,78,w-96,22,"LOCAL LOADOUT   /   shared by both teams",15,Muted());
        Text(48,124,lw,22,"GLOVE FAMILY",12,Muted());
        Text(48,270,lw,20,"SEARCH VARIANTS",12,Muted());
        Text(right,188,rw,22,"SELECTED HANDWEAR",15,Accent(),true);
        if(const auto *entry=oc2_gloves::FindEntry(m_catalog,m_draft.resource))
        {
            oc2_local_inventory::DrawImage(entry->preview,Pixel(right,230),Size(rw,272).Scale());
            Text(right,510,rw,22,"TEXTURE PREVIEW",12,Muted());
            Text(right,540,rw,44,entry->name.c_str(),21,Ink(),true);
            Text(right,588,rw,24,entry->collection.c_str(),14,Accent());
        }
        else if(!m_draft.resource)
        {
            Text(right,282,rw,52,"TEAM DEFAULT",29,Accent(),true);
            Text(right,354,rw,30,"CT: Hard Knuckle   /   T: Full Finger",17,Ink());
            Text(right,400,rw,26,"Restores the original gloves and team sleeves.",15,Muted());
        }
        else Text(right,282,rw,52,"SAVED GLOVES UNAVAILABLE",23,Ink(),true);
        char page[96];std::snprintf(page,sizeof(page),"%u variants   /   Page %d / %d",unsigned(m_options.size()),m_page+1,Pages());
        Text(48,666,lw,20,page,13,Muted());
        Text(right,654,rw,28,"Choose a variant, then APPLY to equip.",13,Muted());
        Text(196,700,Q_max(100,w-720),38,m_message.c_str(),13,m_error?PackRGBA(246,133,119,255):Muted());
        if(m_dirty)Text(w-380,78,332,22,"Unsaved changes",14,Accent(),false,QM_RIGHT);
        CMenuFramework::Draw();
    }
    void Dump()
    {
        const std::string instance=std::to_string(m_draft.instance);
        Con_Printf("glove_ui: visible=%d selection=%d saved=%d dirty=%d catalog=%u catalog_valid=%d config_valid=%d revision=%u page=%d pages=%d family=%s family_index=%d options=%u error=%d discard_modal=%d instance=%s\n",
            int(IsVisible()),m_draft.resource,m_saved.resource,int(m_dirty),unsigned(m_catalog.size()),int(m_catalogValid),
            int(m_configValid),m_saved.revision,m_page,Pages(),FamilyKey(m_family),m_family,unsigned(m_options.size()),int(m_error),
            int(m_discard.IsVisible()),instance.c_str());
        const auto *entry=oc2_gloves::FindEntry(m_catalog,m_draft.resource);
        Con_Printf("glove_selection: resource=%d model=%s body=%d family=%s collection=%s preview=%s config=%s\n",
            m_draft.resource,entry?entry->model.c_str():"team_default",entry?entry->body:0,
            entry?FamilyKey(FamilyIndex(entry->collection)):"team_default",
            entry?entry->collection.c_str():"Team Default",entry?entry->preview.c_str():"none",oc2_gloves::kConfigPath);
        for(int i=0;i<FamilyCount;++i)
            Con_Printf("glove_family: family=%s index=%d selected=%d variants=%u\n",FamilyKey(i),i,int(m_family==i),FamilyOptions(i));
        for(int i=0;i<8;++i)if(m_rowIds[i]>=0)
            Con_Printf("glove_option: row=%d id=%d selected=%d family=%s\n",i,m_rowIds[i],int(m_rowIds[i]==m_draft.resource),FamilyKey(m_family));
        for(int i=0;i<ItemCount();++i)
        {
            CMenuBaseItem *item=GetItemByIndex(i);if(item&&item->szTag)DumpItem(*item,IsVisible()&&item->IsVisible());
        }
        DumpItem(m_discard.yes,m_discard.IsVisible());DumpItem(m_discard.no,m_discard.IsVisible());
    }
private:
    enum { FamilyCount=9 };
    static const char *FamilyName(int index)
    {
        static const char *names[]={"Bloodhound","Fingerless","Full Finger","Hand Wraps","Hard Knuckle","Motorcycle","Driver","Specialist","Sport"};
        return index>=0&&index<FamilyCount?names[index]:"Unavailable";
    }
    static const char *FamilyKey(int index)
    {
        static const char *keys[]={"bloodhound","fingerless","full_finger","hand_wraps","hard_knuckle","motorcycle","driver","specialist","sport"};
        return index>=0&&index<FamilyCount?keys[index]:"unavailable";
    }
    static int FamilyIndex(const std::string &name)
    {
        for(int i=0;i<FamilyCount;++i)if(name==FamilyName(i))return i;
        return -1;
    }
    unsigned FamilyOptions(int family)const
    {
        unsigned count=0;for(const auto &entry:m_catalog)if(entry.collection==FamilyName(family))++count;
        return count;
    }
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
    static void DumpItem(CMenuBaseItem &item,bool visible)
    {
        const Point p=item.GetRenderPosition();const Size z=item.GetRenderSize();
        Con_Printf("frontend_item: tag=%s x=%d y=%d w=%d h=%d visible=%d enabled=%d focus=%d units=screen_pixels\n",
            item.szTag?item.szTag:"none",p.x,p.y,z.w,z.h,int(visible),int(!(item.iFlags&(QMF_GRAYED|QMF_INACTIVE))),int(item.IsCurrentSelected()));
    }
    void Button(CMenuAction &button,const char *label,const char *tag)
    {
        button.SetNameAndStatus(label,nullptr,tag);button.SetCharSize(QM_SMALLFONT);button.SetTextInsetX(12);
        button.bIgnoreColorstring=true;button.eFocusAnimation=QM_HIGHLIGHTIFFOCUS;
        button.colorBase=Ink();button.colorFocus=Accent();
        button.SetBackground(PackRGBA(42,56,70,255),PackRGBA(65,82,98,255));AddItem(button);
    }
    void Reload()
    {
        m_catalog.clear();std::string error,configError;
        m_catalogValid=oc2_local_gloves::LoadCatalog(m_catalog,error);
        m_configValid=oc2_local_gloves::LoadConfig(m_saved,configError);
        if(m_catalogValid&&m_configValid)m_configValid=oc2_gloves::ValidateConfig(m_saved,m_catalog,configError);
        m_draft=m_saved;m_dirty=false;m_error=!m_catalogValid||!m_configValid;
        m_message=!m_catalogValid?error:!m_configValid?configError:"Saved glove selection loaded.";
    }
    void _Init() override
    {
        for(int i=0;i<FamilyCount;++i)
        {
            std::snprintf(m_familyTags[i],sizeof(m_familyTags[i]),"glove_family_%s",FamilyKey(i));
            Button(m_families[i],FamilyName(i),m_familyTags[i]);
            SET_EVENT_MULTI(m_families[i].onReleased,{static_cast<CMenuGloveEditor*>(pSelf->Parent())->Family(pSelf);});
        }
        Button(m_default,"USE TEAM DEFAULT","glove_default");m_default.onReleased=VoidCb(&CMenuGloveEditor::Default);
        Button(m_back,"BACK","glove_back");m_back.onReleased=VoidCb(&CMenuGloveEditor::Back);
        Button(m_revert,"REVERT","glove_revert");m_revert.onReleased=VoidCb(&CMenuGloveEditor::Revert);
        Button(m_apply,"APPLY","glove_apply");m_apply.onReleased=VoidCb(&CMenuGloveEditor::Apply);
        Button(m_previous,"< PREVIOUS","glove_previous");m_previous.onReleased=VoidCb(&CMenuGloveEditor::Previous);
        Button(m_next,"NEXT >","glove_next");m_next.onReleased=VoidCb(&CMenuGloveEditor::Next);
        for(int i=0;i<8;++i)
        {
            std::snprintf(m_rowTags[i],sizeof(m_rowTags[i]),"glove_option_%d",i);
            Button(m_rows[i],"",m_rowTags[i]);
            SET_EVENT_MULTI(m_rows[i].onReleased,{static_cast<CMenuGloveEditor*>(pSelf->Parent())->Select(pSelf);});
        }
        m_search.SetNameAndStatus("",nullptr,"glove_search");m_search.iMaxLength=80;m_search.bAllowColorstrings=false;AddItem(m_search);
        m_discard.Link(this);m_discard.SetMessage("Discard the unsaved glove selection?");
        m_discard.onPositive=VoidCb(&CMenuGloveEditor::Discard);
        m_discard.yes.szTag="glove_discard_yes";m_discard.no.szTag="glove_discard_no";
    }
    void _VidInit() override
    {
        const int w=uiStatic.width,right=Right(),lw=right-72;
        const int familyWidth=(lw-20)/3;
        for(int i=0;i<FamilyCount;++i)m_families[i].SetRect(48+(i%3)*(familyWidth+10),154+(i/3)*40,familyWidth,36);
        m_default.SetRect(right,126,w-right-48,42);m_search.SetRect(48,306,lw,28);
        for(int i=0;i<8;++i)m_rows[i].SetRect(48,346+i*35,lw,30);
        m_previous.SetRect(48,630,(lw-12)/2,30);m_next.SetRect(60+(lw-12)/2,630,(lw-12)/2,30);
        m_back.SetRect(48,700,128,36);m_revert.SetRect(w-370,700,140,36);m_apply.SetRect(w-210,700,162,36);
        RefreshRows();
    }
    void Think() override
    {
        const std::string query=oc2_local_inventory::Fold(m_search.GetBuffer());
        if(query!=m_query){m_query=query;m_page=0;RefreshRows();}
        CMenuFramework::Think();
    }
    void RefreshRows()
    {
        m_options.clear();
        for(const auto &entry:m_catalog)
        {
            if(entry.collection!=FamilyName(m_family))continue;
            if(!m_query.empty()&&oc2_local_inventory::Fold(entry.name).find(m_query)==std::string::npos)continue;
            m_options.push_back(entry.id);
        }
        m_page=Q_min(m_page,Pages()-1);
        for(int i=0;i<8;++i)
        {
            const int index=m_page*8+i;const bool visible=index<int(m_options.size());
            m_rows[i].SetVisibility(visible);m_rowIds[i]=visible?m_options[index]:-1;
            if(!visible)continue;
            const auto *entry=oc2_gloves::FindEntry(m_catalog,m_rowIds[i]);
            std::string label=entry?entry->name:"Team default";
            const bool selected=m_rowIds[i]==m_draft.resource;
            if(selected)label="[SELECTED] "+label;
            m_rows[i].SetText(label.c_str());m_rows[i].colorBase=selected?Accent():Ink();
        }
        for(int i=0;i<FamilyCount;++i)m_families[i].colorBase=m_family==i?Accent():Ink();
        m_default.colorBase=!m_draft.resource?Accent():Ink();
        m_previous.SetGrayed(m_page==0);m_next.SetGrayed(m_page+1>=Pages());
        m_apply.SetGrayed(!m_catalogValid||!m_configValid||!m_dirty);
    }
    void Family(CMenuBaseItem *button)
    {
        for(int i=0;i<FamilyCount;++i)if(button==&m_families[i])m_family=i;
        m_search.SetBuffer("");m_query.clear();
        m_page=0;RefreshRows();
    }
    void Choose(int resource)
    {
        if(resource&&!oc2_gloves::FindEntry(m_catalog,resource))return;
        oc2_gloves::Equip(m_draft,resource);m_dirty=m_draft.resource!=m_saved.resource;
        m_error=false;m_message=m_dirty?"Selection changed. Apply to equip.":"This selection is already saved.";RefreshRows();
    }
    void Default(){Choose(0);}
    void Select(CMenuBaseItem *button)
    {
        for(int i=0;i<8;++i)if(button==&m_rows[i]&&m_rowIds[i]>=0){Choose(m_rowIds[i]);return;}
    }
    void Previous(){if(m_page>0)--m_page;RefreshRows();}
    void Next(){if(m_page+1<Pages())++m_page;RefreshRows();}
    void Revert(){Reload();RefreshRows();}
    void Apply()
    {
        if(!m_dirty||!m_catalogValid||!m_configValid)return;
        oc2_gloves::Config latest;std::string error;
        if(!oc2_local_gloves::LoadConfig(latest,error)||!oc2_gloves::ValidateConfig(latest,m_catalog,error))
            {m_error=true;m_message=error;return;}
        if(latest.revision!=m_saved.revision||latest.resource!=m_saved.resource||latest.instance!=m_saved.instance)
            {m_error=true;m_message="Saved gloves changed. Revert to reload before applying.";return;}
        oc2_gloves::Equip(latest,m_draft.resource);
        latest.revision=latest.revision==std::numeric_limits<unsigned>::max()?1:latest.revision+1;
        if(!oc2_gloves::ValidateConfig(latest,m_catalog,error)){m_error=true;m_message=error;return;}
        if(!oc2_local_gloves::SaveConfig(latest)){m_error=true;m_message="Save failed. Selection is still unsaved.";return;}
        m_saved=m_draft=latest;m_dirty=false;m_error=false;m_message="Gloves equipped for both teams.";RefreshRows();
    }
    void Back(){if(m_dirty){m_discard.Show();m_discard.SetCursorToItem(m_discard.no);}else Hide();}
    void Discard(){m_dirty=false;Hide();}
    oc2_gloves::Catalog m_catalog;oc2_gloves::Config m_saved,m_draft;
    int m_family=0,m_page=0;bool m_catalogValid=false,m_configValid=false,m_dirty=false,m_error=false;
    std::string m_query,m_message;std::vector<int> m_options;int m_rowIds[8];char m_rowTags[8][32];
    char m_familyTags[FamilyCount][40];
    CMenuAction m_families[FamilyCount],m_rows[8],m_default,m_back,m_revert,m_apply,m_previous,m_next;
    CMenuField m_search;CMenuYesNoMessageBox m_discard;
};
