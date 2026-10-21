#pragma once
#include "Framework.h"
#include "Action.h"
#include "Slider.h"
#include "Field.h"
#include "YesNoMessageBox.h"
#include "keydefs.h"
#include "../../../../common/oc2_accessories.h"
#include <cstdio>
#include <cstring>

// Coordinates are an editable side-surface parameter, not a new sticker slot.
// The client fits the chosen position onto the supported weapon receiver.
class CMenuAccessoryPad : public CMenuBaseItem
{
public:
    float *u=nullptr,*v=nullptr;bool *dirty=nullptr;bool dragging=false;int weapon=0;
    bool KeyDown(int key) override
    {
        if(!u||!v)return false;
        if(UI::Key::IsLeftMouse(key))
        {
            if(!UI_CursorInRect(GetRenderPosition(),GetRenderSize()))return false;
            dragging=true;MovePointer();return true;
        }
        float du=0,dv=0;
        if(UI::Key::IsLeftArrow(key))du=-.01f;
        else if(UI::Key::IsRightArrow(key))du=.01f;
        else if(key==K_UPARROW)dv=.01f;
        else if(key==K_DOWNARROW)dv=-.01f;
        else return false;
        Set(*u+du,*v+dv);return true;
    }
    bool KeyUp(int key) override
    {
        if(UI::Key::IsLeftMouse(key)){dragging=false;return true;}
        return false;
    }
    void Draw() override
    {
        if(dragging)MovePointer();
        const Point p=GetRenderPosition();const Size z=GetRenderSize();
        UI_FillRect(p,z,PackRGBA(25,40,54,255));
        for(int i=1;i<4;++i)
        {
            UI_FillRect(p.x+z.w*i/4,p.y,1,z.h,PackRGBA(72,97,116,100));
            UI_FillRect(p.x,p.y+z.h*i/4,z.w,1,PackRGBA(72,97,116,100));
        }
        char preview[96];
        std::snprintf(preview,sizeof(preview),"gfx/inventory/previews/weapon_%d.tga",weapon);
        const HIMAGE image=EngFuncs::PIC_Load(preview);
        if(image){EngFuncs::PIC_Set(image,255,255,255,255);EngFuncs::PIC_DrawTrans(p,z);}
        else
        {
            UI_FillRect(p.x+z.w/8,p.y+z.h*2/5,z.w*5/8,z.h/5,PackRGBA(126,145,156,120));
            UI_FillRect(p.x+z.w*3/4,p.y+z.h*9/20,z.w/5,z.h/14,PackRGBA(126,145,156,120));
            UI_FillRect(p.x+z.w*3/8,p.y+z.h*3/5,z.w/12,z.h/5,PackRGBA(126,145,156,120));
        }
        if(u&&v)
        {
            const int x=p.x+int(*u*(z.w-1)),y=p.y+int((1-*v)*(z.h-1));
            UI_FillRect(x-7,y-7,15,15,PackRGBA(228,195,119,255));
            UI_FillRect(x-2,y-2,5,5,PackRGBA(20,29,38,255));
        }
    }
private:
    void Set(float x,float y)
    {
        x=Q_max(0.f,Q_min(1.f,x));y=Q_max(0.f,Q_min(1.f,y));
        if(*u!=x||*v!=y){*u=x;*v=y;if(dirty)*dirty=true;}
    }
    void MovePointer()
    {
        const Point p=GetRenderPosition();const Size z=GetRenderSize();
        Set(float(uiStatic.cursorX-p.x)/Q_max(1,z.w-1),1-float(uiStatic.cursorY-p.y)/Q_max(1,z.h-1));
    }
};

class CMenuAccessoriesEditor : public CMenuFramework
{
public:
    CMenuAccessoriesEditor():CMenuFramework("CMenuAccessoriesEditor"){}
    void Open(int weapon)
    {
        if(CL_IsActive()||oc2_accessories::Index(weapon)<0)return;
        m_weapon=weapon;m_tab=0;ReloadData();
        CMenuFramework::Show();Sync();SetCursorToItem(m_statTab);
    }
    bool KeyDown(int key) override
    {
        if(UI::Key::IsEscape(key)){Back();return true;}
        return CMenuFramework::KeyDown(key);
    }
    bool KeyUp(int key) override
    {
        if(UI::Key::IsLeftMouse(key))m_pad.dragging=false;
        return CMenuFramework::KeyUp(key);
    }
    void Draw() override
    {
        if(CL_IsActive())return;
        const int w=uiStatic.width,left=(w-144)/2,right=left+96;
        UI_FillRect(0,0,ScreenWidth,ScreenHeight,PackRGBA(9,14,20,250));
        Fill(32,110,w-64,578,PackRGBA(23,33,44,250));
        Text(48,30,w-96,44,oc2_accessories::Name(m_weapon),32,Ink(),true);
        Text(48,78,w-96,22,"LOCAL WEAPON CUSTOMIZATION   /   shared by both teams",15,Muted());
        Text(48,194,left,22,m_tab?"CHARM POSITION":"STATTRAK POSITION",16,Accent(),true);
        Text(48,494,left,32,"Drag or use arrows. Positions auto-fit.",13,Muted());
        char value[160];
        std::snprintf(value,sizeof(value),"Horizontal %.0f%%",100*(m_tab?m_draft.charmU:m_draft.statU));
        Text(48,543,left,24,value,15,Ink());
        std::snprintf(value,sizeof(value),"Vertical %.0f%%",100*(m_tab?m_draft.charmV:m_draft.statV));
        Text(48,609,left,24,value,15,Ink());
        if(!m_tab)
        {
            Text(right,194,w-right-48,24,"RECORDED KILLS",15,Muted(),true);
            if(m_countsValid)std::snprintf(value,sizeof(value),"%06u",m_count);
            else std::snprintf(value,sizeof(value),"Unavailable");
            Text(right,235,w-right-48,70,value,46,Accent(),true);
            Text(right,380,w-right-48,28,"Counts enemy kills with StatTrak on.",16,Ink());
            Text(right,420,w-right-48,28,"Reset clears this weapon only.",15,Muted());
            Text(right,482,w-right-48,28,"Apply saves settings; counters stay separate.",15,Muted());
        }
        else
        {
            Text(right,194,w-right-48,22,"CHARM STICKER",15,Accent(),true);
            const oc2_accessories::Sticker *sticker=SelectedSticker();
            if(sticker)
            {
                const std::string path=std::string(oc2_accessories::kStickerDirectory)+sticker->file;
                const HIMAGE image=EngFuncs::PIC_Load(path.c_str());
                if(image){EngFuncs::PIC_Set(image,255,255,255,255);EngFuncs::PIC_DrawTrans(Pixel(right,230),Size(92,92).Scale());}
                Text(right+108,230,w-right-156,42,sticker->name.c_str(),18,Ink(),true);
            }
            Text(right,323,w-right-48,24,"Import image filename",13,Muted());
            Text(right,397,w-right-48,24,"PNG, TGA or BMP. No folders or commands.",12,Muted());
            static const char *names[4]={"Swing amplitude (degrees)","Length","Stiffness","Damping"};
            const float values[4]={m_draft.amplitude,m_draft.length,m_draft.stiffness,m_draft.damping};
            for(int i=0;i<4;++i)
            {
                std::snprintf(value,sizeof(value),"%s  %.2f",names[i],values[i]);
                Text(right,430+i*56,w-right-48,24,value,14,Ink());
            }
        }
        Text(200,700,Q_max(100,w-840),36,m_message.c_str(),14,m_error?PackRGBA(246,133,119,255):Muted());
        if(m_dirty)Text(w-440,75,392,22,"Unsaved changes",14,Accent(),false,QM_RIGHT);
        CMenuFramework::Draw();
        // Sliders can change during Draw; reflect the live pad after it paints.
        if(m_pad.dragging)SyncPositionSliders();
    }
    void Dump()
    {
        char line[1536];
        char filenameHex[163];const unsigned char *filename=reinterpret_cast<const unsigned char*>(m_filename.GetBuffer());
        static const char digits[]="0123456789abcdef";std::size_t n=0;
        while(filename[n]&&n<80){filenameHex[n*2]=digits[filename[n]>>4];filenameHex[n*2+1]=digits[filename[n]&15];++n;}
        filenameHex[n*2]=0;if(!n){filenameHex[0]='-';filenameHex[1]=0;}
        std::snprintf(line,sizeof(line),"accessory_ui: visible=%d weapon=%d tab=%s dirty=%d stattrak=%d stat_u=%.6f stat_v=%.6f charm=%d charm_u=%.6f charm_v=%.6f sticker=%d amplitude=%.6f length=%.6f stiffness=%.6f damping=%.6f count=%u count_valid=%d revision=%u error=%d reset_modal=%d discard_modal=%d filename_hex=%s import_status=%s catalog_size=%u\n",
            int(IsVisible()),m_weapon,m_tab?"charm":"stattrak",int(m_dirty),int(m_draft.stattrak),m_draft.statU,m_draft.statV,
            int(m_draft.charm),m_draft.charmU,m_draft.charmV,m_draft.sticker,m_draft.amplitude,m_draft.length,m_draft.stiffness,m_draft.damping,
            m_count,int(m_countsValid),m_revision,int(m_error),int(m_resetDialog.IsVisible()),int(m_discardDialog.IsVisible()),
            filenameHex,m_importStatus,unsigned(m_catalog.size()));
        Con_Printf("%s",line);
        for(int i=0;i<ItemCount();++i)
        {
            CMenuBaseItem *item=GetItemByIndex(i);
            if(item&&item->szTag)DumpItem(*item,IsVisible()&&item->IsVisible());
        }
        DumpItem(m_resetDialog.yes,m_resetDialog.IsVisible());DumpItem(m_resetDialog.no,m_resetDialog.IsVisible());
        DumpItem(m_discardDialog.yes,m_discardDialog.IsVisible());DumpItem(m_discardDialog.no,m_discardDialog.IsVisible());
    }
private:
    static unsigned Ink(){return PackRGBA(234,239,243,255);}
    static unsigned Muted(){return PackRGBA(160,175,189,255);}
    static unsigned Accent(){return PackRGBA(224,194,119,255);}
    Point Pixel(int x,int y)const{return GetRenderPosition()+Point(x,y).Scale();}
    void Fill(int x,int y,int w,int h,unsigned color){UI_FillRect(Pixel(x,y),Size(w,h).Scale(),color);}
    void Text(int x,int y,int w,int h,const char *text,int height,unsigned color,bool bold=false,unsigned align=QM_LEFT)
    {
        UI_DrawString(bold?uiStatic.hBoldFont:uiStatic.hDefaultFont,Pixel(x,y),Size(w,h).Scale(),text,color,
            Q_max(1,int(height*uiStatic.scaleY)),align,ETF_FORCECOL|ETF_NO_WRAP);
    }
    static void DumpItem(CMenuBaseItem &item,bool visible)
    {
        const Point p=item.GetRenderPosition();const Size s=item.GetRenderSize();
        Con_Printf("frontend_item: tag=%s x=%d y=%d w=%d h=%d visible=%d enabled=%d focus=%d units=screen_pixels\n",
            item.szTag?item.szTag:"none",p.x,p.y,s.w,s.h,int(visible),int(!(item.iFlags&(QMF_GRAYED|QMF_INACTIVE))),int(item.IsCurrentSelected()));
    }
    void Button(CMenuAction &b,const char *name,const char *tag)
    {
        b.SetNameAndStatus(name,nullptr,tag);b.SetCharSize(QM_SMALLFONT);b.SetTextInsetX(12);
        b.eFocusAnimation=QM_HIGHLIGHTIFFOCUS;b.bIgnoreColorstring=true;b.colorBase=Ink();b.colorFocus=Accent();
        b.SetBackground(PackRGBA(42,56,70,255),PackRGBA(65,82,98,255));AddItem(b);
    }
    static bool Read(const char *path,std::string &text)
    {
        int length=0;byte *buffer=EngFuncs::COM_LoadFile(path,&length);
        if(!buffer)return false;
        const bool valid=length>=0&&length<=256*1024&&std::memchr(buffer,0,std::size_t(length))==nullptr;
        if(valid)text.assign(reinterpret_cast<const char*>(buffer),std::size_t(length));
        EngFuncs::COM_FreeFile(buffer);return valid;
    }
    static bool Write(const char *path,const std::string &text)
    {
        EngFuncs::COM_SaveFile(path,text.data(),int(text.size()));
        std::string saved;return Read(path,saved)&&saved==text;
    }
    void Message(const std::string &value,bool error=false){m_message=value;m_error=error;}
    void ReloadData()
    {
        oc2_accessories::Config config=oc2_accessories::DefaultConfig();
        std::string text,error;bool valid=true;
        if(Read(oc2_accessories::kConfigPath,text))valid=oc2_accessories::ParseConfig(text.c_str(),config,error);
        else if(EngFuncs::FileExists(oc2_accessories::kConfigPath,true))valid=false;
        m_configValid=valid;m_revision=config.revision;m_draft=*oc2_accessories::Find(config,m_weapon);m_dirty=false;
        oc2_accessories::Counts counts=oc2_accessories::DefaultCounts();
        m_countsValid=true;
        if(Read(oc2_accessories::kCountsPath,text))m_countsValid=oc2_accessories::ParseCounts(text.c_str(),counts,error);
        else if(EngFuncs::FileExists(oc2_accessories::kCountsPath,true))m_countsValid=false;
        m_count=counts.values[oc2_accessories::Index(m_weapon)];
        m_catalog=oc2_accessories::DefaultCatalog();m_catalogValid=true;m_catalogDirty=false;m_importStatus="idle";
        if(Read(oc2_accessories::kCatalogPath,text))m_catalogValid=oc2_accessories::ParseCatalog(text.c_str(),m_catalog,error);
        else if(EngFuncs::FileExists(oc2_accessories::kCatalogPath,true))m_catalogValid=false;
        ScanStickers();
        Message(!valid||!m_catalogValid?"Invalid saved file. Restore it before saving.":
            !m_countsValid?"Counter file unavailable or invalid. It was left unchanged.":"Saved settings loaded.",
            !valid||!m_catalogValid||!m_countsValid);
    }
    bool AddSticker(const std::string &file,bool report=false)
    {
        const auto failure=[this,report](const char *reason){if(report)m_importStatus=reason;return false;};
        if(!oc2_accessories::SafeStickerFile(file))return failure("unsafe_name");
        const std::string path=std::string(oc2_accessories::kStickerDirectory)+file;
        if(!EngFuncs::FileExists(path.c_str(),true))return failure("file_not_found");
        if(!EngFuncs::PIC_Load(path.c_str()))return failure("image_load_failed");
        const int id=oc2_accessories::StickerId(file);
        for(const auto &s:m_catalog)
        {
            if(s.file==file){if(report)m_importStatus="already_registered";return true;}
            if(s.id==id)return failure("id_collision");
        }
        if(m_catalog.size()>=512)return failure("catalog_full");
        m_catalog.push_back(oc2_accessories::Sticker(id,file,file));m_catalogDirty=true;return true;
    }
    void ScanStickers()
    {
        if(!m_catalogValid)return;
        int count=0;char **files=EngFuncs::GetFilesList("gfx/inventory/stickers/*.*",&count,true);
        for(int i=0;files&&i<count&&i<2048;++i)
        {
            std::string file=files[i];const std::size_t slash=file.find_last_of("/\\");
            if(slash!=std::string::npos)file=file.substr(slash+1);
            AddSticker(file);
        }
    }
    const oc2_accessories::Sticker *SelectedSticker()const
    {
        for(const auto &s:m_catalog)if(s.id==m_draft.sticker)return &s;
        return nullptr;
    }
    void _Init() override
    {
        Button(m_statTab,"STAT TRAK","accessory_tab_stat");m_statTab.onReleased=VoidCb(&CMenuAccessoriesEditor::StatTab);
        Button(m_charmTab,"CHARM","accessory_tab_charm");m_charmTab.onReleased=VoidCb(&CMenuAccessoriesEditor::CharmTab);
        Button(m_statToggle,"StatTrak OFF","accessory_stat_toggle");m_statToggle.onReleased=VoidCb(&CMenuAccessoriesEditor::ToggleStat);
        Button(m_charmToggle,"Charm OFF","accessory_charm_toggle");m_charmToggle.onReleased=VoidCb(&CMenuAccessoriesEditor::ToggleCharm);
        Button(m_apply,"APPLY","accessory_apply");m_apply.onReleased=VoidCb(&CMenuAccessoriesEditor::Apply);
        Button(m_revert,"REVERT","accessory_revert");m_revert.onReleased=VoidCb(&CMenuAccessoriesEditor::Revert);
        Button(m_defaults,"USE DEFAULTS","accessory_defaults");m_defaults.onReleased=VoidCb(&CMenuAccessoriesEditor::UseDefaults);
        Button(m_back,"BACK","accessory_back");m_back.onReleased=VoidCb(&CMenuAccessoriesEditor::Back);
        Button(m_reset,"RESET COUNT","accessory_reset");m_reset.onReleased=VoidCb(&CMenuAccessoriesEditor::ConfirmReset);
        Button(m_prevSticker,"< PREVIOUS","accessory_sticker_previous");m_prevSticker.onReleased=VoidCb(&CMenuAccessoriesEditor::PreviousSticker);
        Button(m_nextSticker,"NEXT >","accessory_sticker_next");m_nextSticker.onReleased=VoidCb(&CMenuAccessoriesEditor::NextSticker);
        Button(m_import,"ADD IMAGE","accessory_import");m_import.onReleased=VoidCb(&CMenuAccessoriesEditor::ImportSticker);
        m_filename.SetNameAndStatus("",nullptr,"accessory_filename");m_filename.iMaxLength=80;m_filename.bAllowColorstrings=false;
        AddItem(m_filename);m_pad.SetNameAndStatus("",nullptr,"accessory_pad");AddItem(m_pad);
        static const char *tags[6]={"accessory_u","accessory_v","accessory_amplitude","accessory_length","accessory_stiffness","accessory_damping"};
        const float lo[6]={0,0,0,.5f,20,2},hi[6]={100,100,25,1.5f,160,30},step[6]={1,1,1,.05f,5,1};
        for(int i=0;i<6;++i)
        {
            m_sliders[i].SetNameAndStatus("",nullptr,tags[i]);m_sliders[i].Setup(lo[i],hi[i],step[i]);
            m_sliders[i].SetCharSize(QM_SMALLFONT);AddItem(m_sliders[i]);
            SET_EVENT_MULTI(m_sliders[i].onChanged,{static_cast<CMenuAccessoriesEditor*>(pSelf->Parent())->SliderChanged(pSelf);});
        }
        m_resetDialog.Link(this);m_resetDialog.SetMessage("Reset this weapon's saved kill counter to zero?");
        m_resetDialog.onPositive=VoidCb(&CMenuAccessoriesEditor::ResetCount);
        m_resetDialog.yes.szTag="accessory_reset_yes";m_resetDialog.no.szTag="accessory_reset_no";
        m_discardDialog.Link(this);m_discardDialog.SetMessage("Discard this weapon's unsaved changes?");
        m_discardDialog.onPositive=VoidCb(&CMenuAccessoriesEditor::Discard);
        m_discardDialog.yes.szTag="accessory_discard_yes";m_discardDialog.no.szTag="accessory_discard_no";
    }
    void _VidInit() override
    {
        const int w=uiStatic.width,left=(w-144)/2,right=left+96,rw=w-right-48;
        m_statTab.SetRect(48,126,166,44);m_charmTab.SetRect(226,126,166,44);
        m_statToggle.SetRect(right,126,(rw-12)/2,44);m_charmToggle.SetRect(right+(rw+12)/2,126,(rw-12)/2,44);
        m_pad.SetRect(48,230,left,250);m_sliders[0].SetRect(48,578,left,22);m_sliders[1].SetRect(48,644,left,22);
        m_reset.SetRect(right,320,Q_min(220,rw),42);
        m_prevSticker.SetRect(right+108,278,(rw-120)/2,42);
        m_nextSticker.SetRect(right+114+(rw-120)/2,278,(rw-120)/2,42);
        m_filename.SetRect(right,363,rw-142,30);m_import.SetRect(w-178,357,130,38);
        for(int i=0;i<4;++i)m_sliders[i+2].SetRect(right,466+i*56,rw,20);
        m_back.SetRect(48,700,128,36);m_revert.SetRect(w-410,700,140,36);m_apply.SetRect(w-250,700,202,36);
        m_defaults.SetRect(w-620,700,190,36);
        Sync();
    }
    void SyncPositionSliders()
    {
        m_sliders[0].SetCurrentValue(100*(m_tab?m_draft.charmU:m_draft.statU));
        m_sliders[1].SetCurrentValue(100*(m_tab?m_draft.charmV:m_draft.statV));
    }
    void Sync()
    {
        m_statToggle.SetText(m_draft.stattrak?"StatTrak ON":"StatTrak OFF");
        m_charmToggle.SetText(m_draft.charm?"Charm ON":"Charm OFF");
        m_statTab.colorBase=m_tab?Ink():Accent();m_charmTab.colorBase=m_tab?Accent():Ink();
        m_pad.u=m_tab?&m_draft.charmU:&m_draft.statU;m_pad.v=m_tab?&m_draft.charmV:&m_draft.statV;m_pad.dirty=&m_dirty;m_pad.weapon=m_weapon;
        m_reset.SetVisibility(!m_tab);m_prevSticker.SetVisibility(m_tab);m_nextSticker.SetVisibility(m_tab);
        m_filename.SetVisibility(m_tab);m_import.SetVisibility(m_tab);
        for(int i=2;i<6;++i)m_sliders[i].SetVisibility(m_tab);
        SyncPositionSliders();m_sliders[2].SetCurrentValue(m_draft.amplitude);m_sliders[3].SetCurrentValue(m_draft.length);
        m_sliders[4].SetCurrentValue(m_draft.stiffness);m_sliders[5].SetCurrentValue(m_draft.damping);
    }
    void SliderChanged(CMenuBaseItem *item)
    {
        for(int i=0;i<6;++i)if(item==&m_sliders[i])
        {
            const float v=m_sliders[i].GetCurrentValue();
            if(i==0)(m_tab?m_draft.charmU:m_draft.statU)=v*.01f;
            if(i==1)(m_tab?m_draft.charmV:m_draft.statV)=v*.01f;
            if(i==2)m_draft.amplitude=v;if(i==3)m_draft.length=v;
            if(i==4)m_draft.stiffness=v;if(i==5)m_draft.damping=v;
            m_dirty=true;
        }
    }
    void StatTab(){m_pad.dragging=false;m_tab=0;Sync();}
    void CharmTab(){m_pad.dragging=false;m_tab=1;Sync();}
    void ToggleStat(){m_draft.stattrak=!m_draft.stattrak;m_dirty=true;Sync();}
    void ToggleCharm(){m_draft.charm=!m_draft.charm;m_dirty=true;Sync();}
    void SelectSticker(int direction)
    {
        if(m_catalog.empty())return;
        int i=0;for(std::size_t n=0;n<m_catalog.size();++n)if(m_catalog[n].id==m_draft.sticker)i=int(n);
        i=(i+direction+int(m_catalog.size()))%int(m_catalog.size());m_draft.sticker=m_catalog[i].id;m_dirty=true;
    }
    void PreviousSticker(){SelectSticker(-1);}
    void NextSticker(){SelectSticker(1);}
    void ImportSticker()
    {
        const std::string file=m_filename.GetBuffer();
        if(!m_catalogValid){m_importStatus="catalog_invalid";Message("Sticker catalog is invalid.",true);return;}
        if(!AddSticker(file,true)){Message("Image not found, unsafe name, unsupported image or ID collision.",true);return;}
        if(!Write(oc2_accessories::kCatalogPath,oc2_accessories::SerializeCatalog(m_catalog)))
            {m_importStatus="catalog_write_failed";Message("Could not save sticker catalog.",true);return;}
        m_catalogDirty=false;
        for(const auto &s:m_catalog)if(s.file==file)m_draft.sticker=s.id;
        m_importStatus="registered";m_dirty=true;Message("Image added. Apply to save this weapon's selection.");
    }
    void Apply()
    {
        if(!m_configValid||!m_catalogValid||!oc2_accessories::Valid(m_draft))
            {Message("Invalid configuration; nothing was saved.",true);return;}
        oc2_accessories::Config latest=oc2_accessories::DefaultConfig();std::string text,error;
        const bool loaded=Read(oc2_accessories::kConfigPath,text);
        if((loaded&&!oc2_accessories::ParseConfig(text.c_str(),latest,error))||
            (!loaded&&EngFuncs::FileExists(oc2_accessories::kConfigPath,true)))
            {Message(error.empty()?"Saved configuration could not be read.":error,true);return;}
        if(m_catalogDirty&&!Write(oc2_accessories::kCatalogPath,oc2_accessories::SerializeCatalog(m_catalog)))
            {Message("Could not save sticker catalog.",true);return;}
        *oc2_accessories::Find(latest,m_weapon)=m_draft;
        latest.revision=latest.revision==std::numeric_limits<unsigned>::max()?1:latest.revision+1;
        if(!Write(oc2_accessories::kConfigPath,oc2_accessories::SerializeConfig(latest)))
            {Message("Save failed. Your changes are still unsaved.",true);return;}
        m_revision=latest.revision;m_dirty=false;m_catalogDirty=false;Message("Saved for the next game / restart.");
    }
    void UseDefaults()
    {
        const auto defaults=oc2_accessories::DefaultConfig();
        const auto *value=oc2_accessories::Find(defaults,m_weapon);if(!value)return;
        const bool stat=m_draft.stattrak,charm=m_draft.charm;const int sticker=m_draft.sticker;
        m_draft=*value;m_draft.stattrak=stat;m_draft.charm=charm;m_draft.sticker=sticker;
        m_pad.dragging=false;m_dirty=true;Sync();Message("Defaults ready. Apply to save.");
    }
    void Revert(){m_pad.dragging=false;ReloadData();Sync();}
    void Back()
    {
        m_pad.dragging=false;
        if(m_dirty){m_discardDialog.Show();m_discardDialog.SetCursorToItem(m_discardDialog.no);}
        else Hide();
    }
    void Discard(){m_dirty=false;Hide();}
    void ConfirmReset()
    {
        if(!m_countsValid){Message("Counter unavailable. Restore the saved file before resetting.",true);return;}
        m_resetDialog.Show();m_resetDialog.SetCursorToItem(m_resetDialog.no);
    }
    void ResetCount()
    {
        oc2_accessories::Counts latest=oc2_accessories::DefaultCounts();std::string text,error;
        const bool loaded=Read(oc2_accessories::kCountsPath,text);
        if((loaded&&!oc2_accessories::ParseCounts(text.c_str(),latest,error))||
            (!loaded&&EngFuncs::FileExists(oc2_accessories::kCountsPath,true)))
            {m_countsValid=false;Message(error.empty()?"Saved counters could not be read.":error,true);return;}
        latest.values[oc2_accessories::Index(m_weapon)]=0;
        if(!Write(oc2_accessories::kCountsPath,oc2_accessories::SerializeCounts(latest)))
            {Message("Counter could not be saved.",true);return;}
        m_count=0;m_countsValid=true;Message("This weapon's saved counter was reset.");
    }
    int m_weapon=1,m_tab=0;unsigned m_count=0,m_revision=1;
    bool m_dirty=false,m_error=false,m_configValid=true,m_countsValid=true,m_catalogValid=true,m_catalogDirty=false;
    std::string m_message;
    const char *m_importStatus="idle";
    oc2_accessories::WeaponConfig m_draft;
    oc2_accessories::Catalog m_catalog;
    CMenuAction m_statTab,m_charmTab,m_statToggle,m_charmToggle,m_apply,m_revert,m_defaults,m_back,m_reset;
    CMenuAction m_prevSticker,m_nextSticker,m_import;
    CMenuSlider m_sliders[6];CMenuField m_filename;CMenuAccessoryPad m_pad;
    CMenuYesNoMessageBox m_resetDialog,m_discardDialog;
};
