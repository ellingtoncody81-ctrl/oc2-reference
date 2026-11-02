#pragma once
#include "Framework.h"
#include "Action.h"
#include "Field.h"
#include "YesNoMessageBox.h"
#include "SkinInventory.h"
#include "AccessoriesEditor.h"
#include "PanoramaChrome.h"
#include "PanoramaInventoryLabels.h"
#include "keydefs.h"

// Presentation order on the default sticker-side preview. Values are the
// original material/profile slots; saved appearances always keep that order.
namespace oc2_sticker_display
{
inline int PhysicalSlot(int weapon,int displaySlot)
{
    static const unsigned char order[24][4]={
        {2,1,0,3}, {3,2,1,0}, {3,2,1,0}, {3,2,1,0},
        {3,2,1,0}, {2,3,1,0}, {2,1,0,3}, {3,2,1,0},
        {3,2,1,0}, {2,1,0,3}, {3,2,1,0}, {3,2,1,0},
        {2,1,0,3}, {3,2,1,0}, {1,2,0,3}, {3,2,1,0},
        {3,2,1,0}, {3,1,2,0}, {2,1,3,0}, {2,3,1,0},
        {3,2,1,0}, {2,3,1,0}, {3,2,1,0}, {1,3,0,2}
    };
    const int index=oc2_accessories::Index(weapon);
    return index>=0&&displaySlot>=0&&displaySlot<4?order[index][displaySlot]:-1;
}
}

class CMenuPanoramaPatternCard : public CMenuAction
{
public:
    std::string image,title;
    bool selected=false,imageOnly=false;
    void Draw() override
    {
        CMenuAction::Draw();const Point p=GetRenderPosition();const Size z=GetRenderSize();
        const int inset=Q_max(1,int(10*uiStatic.scaleY));
        const unsigned accent=PackRGBA(221,193,124,255);
        if(selected)UI_DrawRectangle(p,z,accent);
        oc2_local_inventory::DrawImage(image,p+Point(inset,inset),
            Size(z.w-2*inset,imageOnly?z.h-2*inset:z.h-int(42*uiStatic.scaleY)));
        if(!imageOnly)oc2_panorama::DrawText(p+Point(inset,z.h-int(31*uiStatic.scaleY)),
            Size(z.w-2*inset,int(25*uiStatic.scaleY)),title.c_str(),16,selected?accent:PackRGBA(234,239,243,255),false,QM_CENTER);
        if(selected)UI_FillRect(p+Point(0,z.h-Q_max(2,int(3*uiStatic.scaleY))),Size(z.w,Q_max(2,int(3*uiStatic.scaleY))),accent);
    }
};

class CMenuSkinEditor : public CMenuFramework,public IMenuCharmPatternPicker
{
public:
    CMenuSkinEditor():CMenuFramework("CMenuSkinEditor"){for(int &id:m_rowIds)id=-1;}
    ~CMenuSkinEditor() override{delete m_charmPicker;}
    void SetAccessories(CMenuAccessoriesEditor *editor){m_accessories=editor;if(editor)editor->SetPatternPicker(this);}
    void OpenWeapon(int weapon,int skin=-1)
    {
        if(oc2_accessories::Index(weapon)<0)return;
        m_weapon=weapon;m_skin=skin;m_definition=0;m_knife=false;m_stickerOnly=false;m_charmOnly=false;m_patternOwner=nullptr;Open();
    }
    void OpenKnife(int definition)
    {
        if(definition<0)return;
        m_weapon=29;m_definition=definition;m_knife=true;m_stickerOnly=false;m_charmOnly=false;m_patternOwner=nullptr;Open();
    }
    void OpenSticker(int weapon,int slot,int skin=-1)
    {
        if(slot<0||slot>=4||oc2_accessories::Index(weapon)<0)return;
        m_weapon=weapon;m_skin=skin;m_definition=0;m_knife=false;m_stickerOnly=true;m_charmOnly=false;m_patternOwner=nullptr;Open();
        m_tab=slot+1;m_page=0;RefreshRows();SetCursorToItem(m_tabs[m_tab]);
    }
    void OpenCharmPattern(int weapon,int skin,int pattern,CMenuAccessoriesEditor *owner) override
    {
        if(!m_charmPicker)m_charmPicker=new CMenuSkinEditor;
        m_charmPicker->OpenCharmGrid(weapon,skin,pattern,owner);
    }
    void Hide() override
    {
        if(m_charmPicker&&m_charmPicker->IsVisible())m_charmPicker->Hide();
        CMenuFramework::Hide();
    }
    void OpenCharmGrid(int weapon,int skin,int pattern,CMenuAccessoriesEditor *owner)
    {
        if(!owner||oc2_accessories::Index(weapon)<0)return;
        m_weapon=weapon;m_skin=skin;m_definition=0;m_knife=false;m_stickerOnly=false;m_charmOnly=true;m_patternOwner=owner;
        Open();m_draft.charm=pattern;m_charmOriginal=pattern;m_tab=5;m_page=0;RefreshRows();SetCursorToItem(m_tabs[5]);
    }
    bool KeyDown(int key) override
    {
        if(UI::Key::IsEscape(key)){Back();return true;}
        return CMenuFramework::KeyDown(key);
    }
    void Draw() override
    {
        const int w=uiStatic.width,right=Right(),rw=w-right-34,lw=right-130;
        oc2_panorama::DrawBackground();
        Fill(64,0,w-64,96,PackRGBA(12,18,25,80));Fill(98,95,w-132,1,PackRGBA(190,206,218,80));
        const std::string heading=Title()+u8"  /  "+(m_charmOnly?u8"挂件图案":m_stickerOnly?u8"贴纸图案":u8"外观图案");
        Text(98,30,w-132,42,heading.c_str(),28,Ink(),true);
        Fill(84,168,lw+28,524,PackRGBA(19,29,38,150));
        Fill(right-14,168,rw+28,524,PackRGBA(18,28,37,210));
        Text(98,173,lw,24,u8"搜索图案名称或收藏",13,Ink());
        Text(right,181,rw,28,TabName(),20,Accent(),true);
        const int selection=Selection();const oc2_inventory::Entry *entry=SelectedEntry();
        if(!m_knife&&m_weapon==10&&m_tab>=1&&m_tab<=4)
        {
            DrawGunPreview(right+16,224,rw-32,238);
            const std::string caption=selection<0?u8"已卸下":selection==0?u8"保留原贴纸":
                std::string(u8"贴纸图案 · ")+oc2_inventory_labels::Sticker(m_catalog,selection);
            Text(right,476,rw,44,caption.c_str(),20,Ink(),true);
            char description[128];std::snprintf(description,sizeof(description),u8"本地图案 · 用于贴纸槽 %c",'A'+m_tab-1);
            Text(right,522,rw,30,description,13,Muted());
        }
        else if(entry&&!oc2_inventory::IsTransparent(*entry))
        {
            if(!m_knife&&m_tab==0)DrawGunPreview(right+16,224,rw-32,238);
            else oc2_local_inventory::DrawImage(entry->preview,Pixel(right+16,224),Size(rw-32,238).Scale());
            const bool pattern=!m_knife&&m_tab>0;
            const std::string caption=pattern?std::string(m_tab==5?u8"挂件图案 · ":u8"贴纸图案 · ")+
                oc2_inventory_labels::Sticker(m_catalog,selection):oc2_inventory_labels::Finish(m_catalog,selection);
            Text(right,476,rw,44,caption.c_str(),20,Ink(),true);
            char description[128];
            if(pattern&&m_tab!=5)std::snprintf(description,sizeof(description),u8"本地图案 · 用于贴纸槽 %c",'A'+m_tab-1);
            else std::snprintf(description,sizeof(description),pattern?u8"本地图案 · 用于当前挂件":u8"本地武器涂装 · 双方阵营共用");
            // Use the same clear font as the caption for the charm description.
            Text(right,522,rw,34,description,pattern&&m_tab==5?20:13,
                pattern&&m_tab==5?Ink():Muted());
        }
        else if(selection==-1)
        {
            Text(right,300,rw,54,u8"已卸下",28,Accent(),true);
            Text(right,370,rw,70,m_tab==5?u8"卸下整个挂件":u8"清空当前贴纸槽",16,Ink());
        }
        else if(!m_knife&&m_tab==0)
        {
            DrawGunPreview(right+16,224,rw-32,238);
            Text(right,476,rw,44,u8"原版皮肤",22,Ink(),true);
        }
        else
        {
            Text(right,300,rw,62,DefaultLabel(),25,Ink(),true);
            Text(right,375,rw,82,m_tab==5?u8"使用挂件页的图案与开关":
                m_knife?u8"使用此刀型的原版纹理":u8"保留该槽的模型原贴纸",16,Muted());
        }
        char page[96];std::snprintf(page,sizeof(page),u8"%u 款图案  /  第 %d / %d 页",unsigned(m_options.size()),m_page+1,Pages());
        Text(152,655,lw-108,26,page,13,Muted());
        if(m_options.empty())Text(98,372,lw,70,u8"没有匹配的图案",20,Muted());
        Text(right,650,rw,28,m_charmOnly?u8"选择后，点击使用此图案返回":u8"选择图案后，点击应用保存",13,Muted());
        Fill(64,704,w-64,64,PackRGBA(9,14,20,125));
        Text(246,716,Q_max(100,w-640),38,m_message.c_str(),13,m_error?PackRGBA(246,133,119,255):Muted());
        if(m_dirty)Text(w-310,75,276,22,u8"有未保存的更改",13,Accent());
        m_chrome.Draw();CMenuFramework::Draw();
    }
    void Dump()
    {
        if(m_charmPicker&&m_charmPicker->IsVisible()){m_charmPicker->Dump();return;}
        const std::string instance=std::to_string(m_knife?m_knifeDraft.instance:m_draft.instance);
        char line[1024];std::snprintf(line,sizeof(line),
            "skin_ui: visible=%d weapon=%d knife=%d definition=%d tab=%d page=%d pages=%d options=%u selection=%d dirty=%d catalog_valid=%d config_valid=%d revision=%u error=%d instance=%s discard_modal=%d\n",
            int(IsVisible()),m_weapon,int(m_knife),m_definition,m_tab,m_page,Pages(),unsigned(m_options.size()),Selection(),int(m_dirty),
            int(m_catalogValid),int(m_configValid),m_revision,int(m_error),instance.c_str(),int(m_discard.IsVisible()));
        Con_Printf("%s",line);
        Con_Printf("skin_draft: weapon=%d skin=%d sticker_a=%d sticker_b=%d sticker_c=%d sticker_d=%d charm=%d knife_skin=%d\n",
            m_weapon,m_draft.skin,m_draft.stickers[0],m_draft.stickers[1],m_draft.stickers[2],m_draft.stickers[3],m_draft.charm,m_knifeDraft.skin);
        for(int i=0;i<CardCount;++i)if(m_rowIds[i]>=0)
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
    enum { CardCount=12 };
    int Right()const{return uiStatic.width*74/100;}
    int Pages()const{return Q_max(1,(int(m_options.size())+CardCount-1)/CardCount);}
    Point Pixel(int x,int y)const{return GetRenderPosition()+Point(x,y).Scale();}
    void Fill(int x,int y,int w,int h,unsigned color){UI_FillRect(Pixel(x,y),Size(w,h).Scale(),color);}
    void Text(int x,int y,int w,int h,const char *text,int height,unsigned color,bool bold=false,unsigned align=QM_CENTER)
    {
        oc2_panorama::DrawText(Pixel(x,y),Size(w,h).Scale(),text,height,color,bold,align);
    }
    void DrawGunPreview(int x,int y,int w,int h)
    {
        m_preview.SetInteractive(false);m_preview.SetDualUnfold(true);m_preview.SetAppearance(m_draft,&m_accessoryDraft);
        const Point p=Pixel(x,y);const Size z=Size(w,h).Scale();
        if(!m_preview.DrawAt(p.x,p.y,z.w,z.h))Text(x,y,w,h,u8"3D 预览加载中",16,Muted());
    }
    void Button(CMenuAction &button,const char *text,const char *tag)
    {
        button.SetNameAndStatus(text,nullptr,tag);oc2_panorama::CenterButton(button);
        button.bIgnoreColorstring=true;button.eFocusAnimation=QM_HIGHLIGHTIFFOCUS;
        button.colorBase=Ink();button.colorFocus=Accent();
        button.SetBackground(PackRGBA(36,48,59,228),PackRGBA(58,75,89,245));AddItem(button);
    }
    std::string Title()const
    {
        if(!m_knife)return std::string(oc2_accessories::Name(m_weapon))+u8" · "+oc2_inventory_labels::Finish(m_catalog,m_skin);
        return oc2_inventory_labels::KnifeName(m_definition);
    }
    const char *TabName()const
    {
        static const char *names[]={u8"皮肤图案",u8"贴纸槽 A",u8"贴纸槽 B",u8"贴纸槽 C",u8"贴纸槽 D",u8"挂件图案"};
        return names[m_tab];
    }
    const char *DefaultLabel()const{return m_tab==5?u8"使用挂件页图案":m_tab?u8"保留原贴纸":u8"原版皮肤";}
    int Selection()const{return m_knife?m_knifeDraft.skin:m_tab==0?m_draft.skin:m_tab==5?m_draft.charm:m_draft.stickers[oc2_sticker_display::PhysicalSlot(m_weapon,m_tab-1)];}
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
        m_tab=m_charmOnly?5:m_stickerOnly?1:0;m_page=0;m_returnAccessories=false;Reload();CMenuFramework::Show();m_search.SetBuffer("");m_query.clear();RefreshRows();SetCursorToItem(m_tabs[m_tab]);
    }
    void Reload()
    {
        m_catalog.clear();std::string error;
        m_catalogValid=oc2_local_inventory::LoadCatalog(m_catalog,error);
        if(m_knife&&m_definition&&!oc2_inventory::FindKnifeEntry(m_catalog,m_definition))m_catalogValid=false;
        std::string configError;oc2_inventory::Config config;
        m_configValid=oc2_local_profiles::Load(m_profiles,configError);config=m_profiles.loadout;
        if(m_catalogValid&&m_configValid)m_configValid=oc2_inventory::ValidateConfig(config,m_catalog,configError);
        m_revision=config.revision;m_equipped=config.knife;m_draft=oc2_inventory::WeaponState();m_knifeDraft=oc2_inventory::KnifeState();
        if(m_knife)
        {
            if(auto *value=oc2_inventory::EnsureKnife(config,m_definition))m_knifeDraft=*value;
        }
        else
        {
            if(m_skin<0){const auto *value=oc2_inventory::Find(config,m_weapon);m_skin=value?value->skin:0;}
            const auto profile=oc2_local_profiles::Resolve(m_profiles,m_weapon,m_skin);
            m_draft=profile.appearance;m_accessoryDraft=profile.accessories;
        }
        m_dirty=false;m_changedSlots=0;m_charmChanged=false;m_error=!m_catalogValid||!m_configValid;
        m_message=!m_catalogValid?u8"图案目录读取失败，请检查资源。":!m_configValid?u8"库存存档无法读取，未修改原文件。":u8"已读取保存的装备";
    }
    void _Init() override
    {
        m_chrome.Init(*this,1);
        static const char *names[]={u8"皮肤",u8"贴纸 A",u8"贴纸 B",u8"贴纸 C",u8"贴纸 D",u8"挂件图案"};
        static const char *tags[]={"skin_tab_skin","skin_tab_a","skin_tab_b","skin_tab_c","skin_tab_d","skin_tab_charm"};
        for(int i=0;i<6;++i)
        {
            Button(m_tabs[i],names[i],tags[i]);
            SET_EVENT_MULTI(m_tabs[i].onReleased,{static_cast<CMenuSkinEditor*>(pSelf->Parent())->SelectTab(pSelf);});
        }
        Button(m_accessoryButton,u8"挂件与计数器","skin_accessories");m_accessoryButton.onReleased=VoidCb(&CMenuSkinEditor::OpenAccessories);
        Button(m_back,u8"返回检视","skin_back");m_back.onReleased=VoidCb(&CMenuSkinEditor::Back);
        Button(m_revert,u8"还原","skin_revert");m_revert.onReleased=VoidCb(&CMenuSkinEditor::Revert);
        Button(m_apply,u8"应用","skin_apply");m_apply.onReleased=VoidCb(&CMenuSkinEditor::Apply);
        Button(m_previous,"<","skin_previous");m_previous.onReleased=VoidCb(&CMenuSkinEditor::Previous);
        Button(m_next,">","skin_next");m_next.onReleased=VoidCb(&CMenuSkinEditor::Next);
        Button(m_default,u8"保留原贴纸","skin_original");m_default.onReleased=VoidCb(&CMenuSkinEditor::Original);
        Button(m_clear,u8"卸下贴纸","skin_unequip");m_clear.onReleased=VoidCb(&CMenuSkinEditor::Unequip);
        m_apply.colorBase=PackRGBA(20,26,31,255);m_apply.colorFocus=PackRGBA(8,12,17,255);
        m_apply.SetBackground(Accent(),PackRGBA(239,216,160,255));
        for(int i=0;i<CardCount;++i)
        {
            std::snprintf(m_rowTags[i],sizeof(m_rowTags[i]),"skin_option_%d",i);m_rowIds[i]=-1;
            Button(m_rows[i],"",m_rowTags[i]);
            SET_EVENT_MULTI(m_rows[i].onReleased,{static_cast<CMenuSkinEditor*>(pSelf->Parent())->SelectOption(pSelf);});
        }
        m_search.SetNameAndStatus("",nullptr,"skin_search");m_search.iMaxLength=80;m_search.bAllowColorstrings=false;AddItem(m_search);
        m_discard.Link(this);m_discard.SetMessage(u8"放弃尚未保存的图案更改？");
        m_discard.SetPositiveButton(u8"放弃更改",PC_OK);m_discard.SetNegativeButton(u8"继续编辑",PC_CANCEL);
        m_discard.yes.SetPicture("");m_discard.no.SetPicture("");
        m_discard.yes.eTextAlignment=m_discard.no.eTextAlignment=QM_CENTER;
        m_discard.onPositive=VoidCb(&CMenuSkinEditor::Discard);
        m_discard.yes.szTag="skin_discard_yes";m_discard.no.szTag="skin_discard_no";
    }
    void _VidInit() override
    {
        m_chrome.Layout();const int w=uiStatic.width,lw=Right()-130,rw=w-Right()-34;
        const int tabWidth=(w-132-5*10)/6;
        for(int i=0;i<6;++i)m_tabs[i].SetRect(98+i*(tabWidth+10),112,tabWidth,42);
        m_search.SetRect(98,205,lw,28);
        const int gap=10,cw=(lw-gap*3)/4;
        for(int i=0;i<CardCount;++i)m_rows[i].SetRect(98+(i%4)*(cw+gap),244+(i/4)*135,cw,125);
        m_previous.SetRect(98,650,42,32);m_next.SetRect(98+lw-42,650,42,32);
        m_default.SetRect(Right(),570,rw,34);m_clear.SetRect(Right(),612,rw,34);
        m_accessoryButton.SetRect(Right(),612,rw,34);
        m_back.SetRect(98,718,134,36);m_revert.SetRect(w-310,718,112,36);m_apply.SetRect(w-184,718,150,36);
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
        m_options.clear();
        for(const auto &entry:m_catalog)
        {
            const bool matches=m_knife?(entry.kind==oc2_inventory::Kind::KnifeSkin&&entry.definition==m_definition):
                m_tab==0?(entry.kind==oc2_inventory::Kind::GunSkin&&entry.weapon==m_weapon):entry.kind==oc2_inventory::Kind::Sticker;
            if(!matches||oc2_inventory::IsTransparent(entry))continue;
            if(!m_query.empty()&&oc2_local_inventory::Fold(oc2_inventory_labels::SearchText(m_catalog,entry)).find(m_query)==std::string::npos)continue;
            m_options.push_back(entry.id);
        }
        m_page=Q_min(m_page,Pages()-1);
        for(int i=0;i<CardCount;++i)
        {
            const int index=m_page*CardCount+i;const bool visible=index<int(m_options.size());m_rows[i].SetVisibility(visible);m_rowIds[i]=visible?m_options[index]:-1;
            if(!visible)continue;
            const auto *entry=oc2_inventory::FindEntry(m_catalog,m_rowIds[i]);
            m_rows[i].imageOnly=m_stickerOnly||m_charmOnly||(!m_knife&&m_tab>0);
            m_rows[i].title=m_rows[i].imageOnly?"":oc2_inventory_labels::Finish(m_catalog,m_rowIds[i]);
            m_rows[i].image=entry?entry->preview:"";m_rows[i].selected=IsSelected(m_rowIds[i]);
        }
        for(int i=0;i<6;++i)
        {
            m_tabs[i].SetVisibility(m_charmOnly?i==5:m_stickerOnly?(i>=1&&i<=4):(!m_knife||!i));m_tabs[i].colorBase=m_tab==i?Accent():Ink();
            if(m_charmOnly&&i==5)m_tabs[i].SetRect(98,112,uiStatic.width-132,42);
            if(m_stickerOnly&&i>=1&&i<=4)m_tabs[i].SetRect(98+(i-1)*((uiStatic.width-132-30)/4+10),112,(uiStatic.width-132-30)/4,42);
            else if(!m_stickerOnly&&!m_charmOnly)m_tabs[i].SetRect(98+i*((uiStatic.width-182)/6+10),112,(uiStatic.width-182)/6,42);
        }
        m_accessoryButton.SetVisibility(!m_knife&&!m_stickerOnly&&!m_charmOnly&&m_tab==0);
        m_default.SetVisibility(m_tab!=5);m_default.SetText(DefaultLabel());m_default.colorBase=Selection()==0?Accent():Ink();
        m_clear.SetVisibility(!m_knife&&m_tab!=0);m_clear.colorBase=Selection()==-1?Accent():Ink();
        m_clear.SetText(m_tab==5?u8"卸下挂件":u8"卸下贴纸");
        m_previous.SetGrayed(m_page==0);m_next.SetGrayed(m_page+1>=Pages());m_apply.SetGrayed(!m_catalogValid||!m_configValid);
        m_apply.SetText(m_charmOnly?u8"使用此图案":m_knife?u8"装备并保存":u8"应用");
    }
    void SelectTab(CMenuBaseItem *button)
    {
        for(int i=0;i<6;++i)if(button==&m_tabs[i])m_tab=i;
        m_page=0;m_query.clear();m_search.SetBuffer("");RefreshRows();
    }
    void SelectOption(CMenuBaseItem *button)
    {
        for(int i=0;i<CardCount;++i)if(button==&m_rows[i]&&m_rowIds[i]>=0)
        {
            const int id=m_rowIds[i];const auto *entry=oc2_inventory::FindEntry(m_catalog,id);
            if(m_knife)m_knifeDraft.skin=id;
            else if(!m_tab)SelectSkin(id);
            else if(m_tab==5){if(entry)oc2_inventory::ApplyCharm(m_draft,*entry);else m_draft.charm=0;m_accessoryDraft.charm=m_draft.charm!=-1;m_charmChanged=true;}
            else{const int slot=oc2_sticker_display::PhysicalSlot(m_weapon,m_tab-1);if(entry)oc2_inventory::ApplySticker(m_draft,slot,*entry);else m_draft.stickers[slot]=0;m_changedSlots|=1<<slot;}
            m_dirty=true;m_error=false;m_message=u8"图案已选择，点击应用保存。";RefreshRows();return;
        }
    }
    void SetSpecial(int id)
    {
        if(m_knife)m_knifeDraft.skin=id;
        else if(!m_tab)SelectSkin(id);
        else if(m_tab==5){m_draft.charm=id;m_accessoryDraft.charm=id!=-1;m_charmChanged=true;}
        else{const int slot=oc2_sticker_display::PhysicalSlot(m_weapon,m_tab-1);m_draft.stickers[slot]=id;m_changedSlots|=1<<slot;}
        m_dirty=true;m_error=false;m_message=u8"选择已更改，点击应用保存。";RefreshRows();
    }
    void SelectSkin(int id)
    {
        const auto profile=oc2_local_profiles::Resolve(m_profiles,m_weapon,id);
        m_skin=id;m_draft=profile.appearance;m_accessoryDraft=profile.accessories;m_changedSlots=0;m_charmChanged=false;
    }
    void Original(){SetSpecial(0);}
    void Unequip(){SetSpecial(-1);}
    void Previous(){if(m_page>0)--m_page;RefreshRows();}
    void Next(){if(m_page+1<Pages())++m_page;RefreshRows();}
    void Revert()
    {
        if(m_charmOnly){m_draft.charm=m_charmOriginal;m_dirty=false;m_error=false;}
        else Reload();
        RefreshRows();
    }
    void Apply()
    {
        if(!m_catalogValid||!m_configValid){m_error=true;m_message=u8"请先恢复有效的库存存档。";return;}
        if(m_charmOnly&&m_patternOwner)
        {
            m_patternOwner->SelectInventoryPattern(m_draft.charm);m_dirty=false;Hide();return;
        }
        if(!m_knife)
        {
            oc2_local_profiles::State latest;std::string error;
            if(!oc2_local_profiles::Load(latest,error))
                {m_error=true;m_message=u8"皮肤设置存档无法读取，未保存更改。";return;}
            auto profile=oc2_local_profiles::Resolve(latest,m_weapon,m_skin);
            for(int i=0;i<4;++i)if(m_changedSlots&(1<<i))profile.appearance.stickers[i]=m_draft.stickers[i];
            if(m_charmChanged){profile.appearance.charm=m_draft.charm;profile.accessories.charm=m_accessoryDraft.charm;}
            if(!oc2_local_profiles::SaveProfile(profile,m_catalog,error))
                {m_error=true;m_message=u8"保存失败，当前皮肤的草稿仍保留。";return;}
            m_draft=profile.appearance;m_accessoryDraft=profile.accessories;m_changedSlots=0;m_charmChanged=false;
            oc2_local_profiles::Load(m_profiles,error);
            m_dirty=false;m_error=false;m_message=u8"已保存当前皮肤的图案设置。";RefreshRows();return;
        }
        oc2_inventory::Config latest;std::string error;
        if(!oc2_local_inventory::LoadConfig(latest,error)||!oc2_inventory::ValidateConfig(latest,m_catalog,error))
            {m_error=true;m_message=u8"库存存档无法读取，未保存更改。";return;}
        if(m_knife)
        {
            if(!oc2_inventory::EquipKnife(latest,m_definition)){m_error=true;m_message=u8"无法装备此刀型。";return;}
            if(m_definition){auto *state=oc2_inventory::FindKnife(latest,m_definition);if(state)state->skin=m_knifeDraft.skin;}
        }
        latest.revision=latest.revision==std::numeric_limits<unsigned>::max()?1:latest.revision+1;
        if(!oc2_inventory::ValidateConfig(latest,m_catalog,error)){m_error=true;m_message=u8"所选图案无法保存，请检查资源。";return;}
        if(!oc2_local_inventory::SaveConfig(latest)){m_error=true;m_message=u8"保存失败，更改仍保留在编辑页。";return;}
        m_revision=latest.revision;m_equipped=latest.knife;m_dirty=false;m_error=false;
        if(!m_knife)if(const auto *state=oc2_inventory::Find(latest,m_weapon))m_draft=*state;
        m_changedSlots=0;m_message=m_knife?u8"已为双方阵营装备此刀。":u8"已保存图案选择。";
        RefreshRows();
    }
    void OpenAccessories()
    {
        if(m_dirty){m_error=false;m_message=u8"请先应用或还原当前更改。";return;}
        if(!m_knife&&m_accessories){m_returnAccessories=true;m_accessories->Open(m_weapon,m_skin);}
    }
    void Back(){if(m_dirty){m_discard.Show();m_discard.SetCursorToItem(m_discard.no);}else Hide();}
    void Discard(){m_dirty=false;Hide();}
    int m_weapon=1,m_skin=-1,m_definition=0,m_tab=0,m_page=0,m_equipped=0;unsigned m_revision=1;
    bool m_knife=false,m_dirty=false,m_error=false,m_catalogValid=false,m_configValid=false,m_returnAccessories=false,m_stickerOnly=false,m_charmOnly=false,m_charmChanged=false;
    int m_charmOriginal=0;
    unsigned m_changedSlots=0;
    std::string m_message,m_query;
    oc2_inventory::Catalog m_catalog;oc2_inventory::WeaponState m_draft;oc2_inventory::KnifeState m_knifeDraft;
    oc2_local_profiles::State m_profiles;oc2_accessories::WeaponConfig m_accessoryDraft;
    std::vector<int> m_options;int m_rowIds[CardCount]{};char m_rowTags[CardCount][32];
    CMenuAction m_tabs[6],m_accessoryButton,m_back,m_revert,m_apply,m_previous,m_next,m_default,m_clear;
    CMenuPanoramaPatternCard m_rows[CardCount];CMenuPanoramaChrome m_chrome;
    CMenuField m_search;CMenuYesNoMessageBox m_discard;CMenuAccessoriesEditor *m_accessories=nullptr;
    CMenuAccessoriesEditor *m_patternOwner=nullptr;
    CMenuSkinEditor *m_charmPicker=nullptr;
    CMenuPanoramaModelView m_preview;
};
