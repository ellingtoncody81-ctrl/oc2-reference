#pragma once
#include "Framework.h"
#include "Action.h"
#include "Slider.h"
#include "Field.h"
#include "YesNoMessageBox.h"
#include "keydefs.h"
#include "../../../../common/oc2_accessories.h"
#include "../../../../common/oc2_accessory_domains.h"
#include <cstdio>
#include <cstring>
#include <cmath>

// The preview and both attachments share one model-unit projection. The saved
// UV still describes the whole sticker-facing gun, including moving parts.
class CMenuAccessoryPad : public CMenuBaseItem
{
public:
    struct Bounds {float x0=0,y0=0,x1=0,y1=0;};
    struct Region {float u0=0,v0=0,u1=1,v1=1,sx=1,sy=1;};
    struct Preview {int count=0;Region regions[2];};
    struct Attachment {std::string file;Bounds bounds;};
    struct Pane {Point p;Size z;float scale=1;Region region;};
    float *u=nullptr,*v=nullptr;bool dragging=false;int weapon=0,selected=0;
    oc2_accessories::WeaponConfig *draft=nullptr;
    unsigned count=0;std::string sticker;
    const oc2_accessory_domains::Domains *domains=nullptr;
    CEventCallback onSelected;
    bool previewValid=false;
    bool LoadPreview(const char *text)
    {
        previewValid=false;
        if(!text)return false;
        std::istringstream input(text);std::string magic,tag;int version=0,total=0;
        if(!(input>>magic>>version)||magic!="OC2_EDITOR_PREVIEWS"||version!=1)return false;
        std::array<Preview,24> next{};Attachment models[2];Bounds stamp;std::string frame;
        Bounds digits[6];std::string digitFiles[6];bool stampSeen=false,digitSeen[6]={};
        while(input>>tag)
        {
            if(tag=="weapon")
            {
                int id=0,n=0;if(!(input>>id>>n)||n<1||n>2)return false;
                const int index=oc2_accessories::Index(id);if(index<0||next[index].count)return false;
                next[index].count=n;++total;
                for(int i=0;i<n;++i)
                {
                    Region &r=next[index].regions[i];
                    if(!(input>>r.u0>>r.v0>>r.u1>>r.v1>>r.sx>>r.sy)||
                        !oc2_accessories::Range(r.u0,0,1)||!oc2_accessories::Range(r.v0,0,1)||
                        !oc2_accessories::Range(r.u1,0,1)||!oc2_accessories::Range(r.v1,0,1)||
                        r.u1<=r.u0||r.v1<=r.v0||!oc2_accessories::Range(r.sx,.01f,1000)||
                        !oc2_accessories::Range(r.sy,.01f,1000))return false;
                }
            }
            else if(tag=="attachment")
            {
                std::string kind;if(!(input>>kind))return false;
                const int i=kind=="stattrak"?0:kind=="charm"?1:-1;
                if(i<0||!(input>>models[i].file)||!ReadBounds(input,models[i].bounds)||!SafeImage(models[i].file))return false;
            }
            else if(tag=="stamp")
            {
                std::string kind;if(!(input>>kind)||kind!="charm"||!ReadBounds(input,stamp))return false;
                stampSeen=true;
            }
            else if(tag=="frame")
            {
                std::string kind;if(!(input>>kind>>frame)||kind!="charm"||!SafeImage(frame))return false;
            }
            else if(tag=="digit")
            {
                int i=-1;if(!(input>>i)||i<0||i>5||!(input>>digitFiles[i])||
                    !ReadBounds(input,digits[i]))return false;
                // The only substitution is the selected decimal digit.
                if(digitFiles[i]!="attachment_digit_%d.tga")return false;
                digitSeen[i]=true;
            }
            else return false;
        }
        if(total!=24||models[0].file.empty()||models[1].file.empty()||!stampSeen||frame.empty())return false;
        for(int i=0;i<6;++i)if(!digitSeen[i])return false;
        m_previews=next;m_models[0]=models[0];m_models[1]=models[1];m_stamp=stamp;m_frame=frame;
        for(int i=0;i<6;++i){m_digits[i]=digits[i];m_digitFiles[i]=digitFiles[i];}
        previewValid=true;return true;
    }
    bool SetPosition(float x,float y)
    {
        if(!u||!v)return false;
        x=Q_max(0.f,Q_min(1.f,x));y=Q_max(0.f,Q_min(1.f,y));
        SnapToSurface(x,y);
        if(*u!=x||*v!=y){*u=x;*v=y;_Event(QM_CHANGED);}
        return true;
    }
    bool KeyDown(int key) override
    {
        if(!u||!v)return false;
        if(UI::Key::IsLeftMouse(key))
        {
            if(!UI_CursorInRect(GetRenderPosition(),GetRenderSize()))return false;
            // Keep the grabbed point under the cursor, rather than jumping the
            // attachment's pivot to whichever part of its body was clicked.
            const int other=1-selected;
            int hit=HitAttachment(selected)?selected:HitAttachment(other)?other:-1;
            if(hit>=0)
            {
                selected=hit;if(onSelected)onSelected(this);
                const Point anchor=Anchor(selected);
                m_grab=Point(uiStatic.cursorX-anchor.x,uiStatic.cursorY-anchor.y);
            }
            else m_grab=Point(0,0);
            dragging=true;if(hit<0)MovePointer();return true;
        }
        float du=0,dv=0;
        if(UI::Key::IsLeftArrow(key))du=-.01f;
        else if(UI::Key::IsRightArrow(key))du=.01f;
        else if(key==K_UPARROW)dv=.01f;
        else if(key==K_DOWNARROW)dv=-.01f;
        else return false;
        SetPosition(*u+du,*v+dv);return true;
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
        if(!previewValid)return;
        char file[96];std::snprintf(file,sizeof(file),"gfx/inventory/previews/weapon_%d.tga",weapon);
        const HIMAGE image=EngFuncs::PIC_Load(file);Pane panes[2];const int n=Panes(panes);
        for(int i=0;image&&i<n;++i)
        {
            const Region &r=panes[i].region;wrect_t crop;
            crop.left=int(r.u0*EngFuncs::PIC_Width(image));crop.right=int(r.u1*EngFuncs::PIC_Width(image));
            crop.top=int(r.v0*EngFuncs::PIC_Height(image));crop.bottom=int(r.v1*EngFuncs::PIC_Height(image));
            EngFuncs::PIC_Set(image,255,255,255,255);EngFuncs::PIC_DrawTrans(panes[i].p,panes[i].z,&crop);
        }
        DrawAttachment(1-selected);DrawAttachment(selected);
    }
    void Dump()
    {
        Pane panes[2];const int n=Panes(panes);
        for(int i=0;i<n;++i)
        {
            const Pane &p=panes[i];const Region &r=p.region;
            Con_Printf("accessory_preview_pane: index=%d loaded=%d x=%d y=%d w=%d h=%d span_x=%.6f span_y=%.6f scale=%.6f u0=%.6f v0=%.6f u1=%.6f v1=%.6f\n",
                i,int(previewValid),p.p.x,p.p.y,p.z.w,p.z.h,r.sx,r.sy,p.scale,r.u0,r.v0,r.u1,r.v1);
        }
        for(int i=0;i<2&&draft;++i)
        {
            const Point anchor=Anchor(i);const Box b=AttachmentBox(i);const Bounds &world=m_models[i].bounds;
            Con_Printf("accessory_preview_attachment: kind=%s selected=%d enabled=%d x=%d y=%d w=%d h=%d pivot_x=%d pivot_y=%d model_w=%.6f model_h=%.6f length=%.6f sticker=%d pattern=%s\n",
                i?"charm":"stattrak",int(selected==i),int(i?draft->charm:draft->stattrak),b.p.x,b.p.y,b.z.w,b.z.h,
                anchor.x,anchor.y,world.x1-world.x0,world.y1-world.y0,i?draft->length:1.f,draft->sticker,
                sticker.empty()?"none":sticker.c_str());
        }
    }
private:
    struct Box {Point p;Size z;};
    std::array<Preview,24> m_previews{};Attachment m_models[2];Bounds m_stamp,m_digits[6];
    std::string m_frame,m_digitFiles[6];Point m_grab;
    static bool SafeImage(const std::string &s)
    {return !s.empty()&&s.size()<81&&s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-")==std::string::npos&&s.find("..") == std::string::npos;}
    static bool ReadBounds(std::istringstream &s,Bounds &b)
    {
        return bool(s>>b.x0>>b.y0>>b.x1>>b.y1)&&oc2_accessories::Range(b.x0,-100,100)&&
            oc2_accessories::Range(b.y0,-100,100)&&oc2_accessories::Range(b.x1,-100,100)&&
            oc2_accessories::Range(b.y1,-100,100)&&b.x1>b.x0&&b.y1>b.y0;
    }
    int Panes(Pane (&panes)[2])const
    {
        const int index=oc2_accessories::Index(weapon);if(!previewValid||index<0)return 0;
        const Preview &preview=m_previews[index];const Point p=GetRenderPosition();const Size z=GetRenderSize();
        const float border=3.1f,gap=Q_max(8.f,8.f*uiStatic.scaleX);float width=0,height=0;
        for(int i=0;i<preview.count;++i){width+=preview.regions[i].sx+2*border;height=Q_max(height,preview.regions[i].sy+2*border);}
        const float scale=Q_min((z.w-gap*(preview.count-1))/width,z.h/height);
        float x=p.x+(z.w-width*scale-gap*(preview.count-1))*.5f;
        for(int i=0;i<preview.count;++i)
        {
            Pane &pane=panes[i];pane.region=preview.regions[i];pane.scale=scale;
            pane.z=Size(Q_max(1,int(pane.region.sx*scale)),Q_max(1,int(pane.region.sy*scale)));
            pane.p=Point(int(x+border*scale),p.y+(z.h-pane.z.h)/2);
            x+=(pane.region.sx+2*border)*scale+gap;
        }
        return preview.count;
    }
    int RegionFor(float x)const
    {
        const int index=oc2_accessories::Index(weapon);if(!previewValid||index<0)return 0;
        const Preview &preview=m_previews[index];
        for(int i=0;i<preview.count;++i)if(x>=preview.regions[i].u0&&x<preview.regions[i].u1)return i;
        return x<preview.regions[0].u0?0:preview.count-1;
    }
    void SnapToSurface(float &x,float &y)const
    {
        const int index=oc2_accessories::Index(weapon);
        if(!domains||!domains->valid||index<0)return;
        const std::string &grid=domains->cells[index];
        const int gx=Q_min(127,int(x*128)),gy=Q_min(63,int((1-y)*64));
        if(grid[gy*128+gx]!='0')return;
        Pane panes[2];const int n=Panes(panes),region=RegionFor(x);
        const Region r=n?panes[region].region:Region();float best=std::numeric_limits<float>::max(),bx=x,by=y;
        for(int cy=0;cy<64;++cy)for(int cx=0;cx<128;++cx)
        {
            if(grid[cy*128+cx]=='0')continue;
            const float px=(cx+.5f)/128,py=1-(cy+.5f)/64;
            if(n&&RegionFor(px)!=region)continue;
            const float dx=(px-x)*r.sx/(r.u1-r.u0),dy=(py-y)*r.sy/(r.v1-r.v0),d=dx*dx+dy*dy;
            if(d<best){best=d;bx=px;by=py;}
        }
        x=bx;y=by;
    }
    void Position(int i,float &x,float &y)const
    {x=draft?(i?draft->charmU:draft->statU):.5f;y=draft?(i?draft->charmV:draft->statV):.5f;SnapToSurface(x,y);}
    Point Anchor(int i)const
    {
        float x,y;Position(i,x,y);Pane panes[2];const int n=Panes(panes);
        if(!n)return GetRenderPosition();const Pane &p=panes[RegionFor(x)];const Region &r=p.region;
        return Point(p.p.x+int((x-r.u0)/(r.u1-r.u0)*p.z.w),p.p.y+int((1-y-r.v0)/(r.v1-r.v0)*p.z.h));
    }
    Box WorldBox(int i,const Bounds &bounds)const
    {
        float x,y;Position(i,x,y);Pane panes[2];const int n=Panes(panes);const Point pivot=Anchor(i);
        const float scale=(n?panes[RegionFor(x)].scale:1)*(i&&draft?draft->length:1);
        const int left=int(std::floor(pivot.x+bounds.x0*scale)),top=int(std::floor(pivot.y-bounds.y1*scale));
        const int right=int(std::ceil(pivot.x+bounds.x1*scale)),bottom=int(std::ceil(pivot.y-bounds.y0*scale));
        return {Point(left,top),Size(Q_max(1,right-left),Q_max(1,bottom-top))};
    }
    Box AttachmentBox(int i)const{return WorldBox(i,m_models[i].bounds);}
    bool HitAttachment(int i)const
    {
        if(!previewValid)return false;const Box b=AttachmentBox(i);const int tolerance=3;
        return UI_CursorInRect(Point(b.p.x-tolerance,b.p.y-tolerance),Size(b.z.w+2*tolerance,b.z.h+2*tolerance));
    }
    static void DrawImage(const std::string &file,const Box &b,int alpha,bool completePath=false)
    {
        const std::string path=completePath?file:std::string("gfx/inventory/previews/")+file;
        const HIMAGE image=EngFuncs::PIC_Load(path.c_str());if(!image)return;
        EngFuncs::PIC_Set(image,255,255,255,alpha);EngFuncs::PIC_DrawTrans(b.p,b.z);
    }
    void DrawAttachment(int i)
    {
        if(!draft)return;const Box b=AttachmentBox(i);const int alpha=(i?draft->charm:draft->stattrak)?255:145;
        DrawImage(m_models[i].file,b,alpha);
        if(i)
        {
            if(!sticker.empty())DrawImage(sticker,WorldBox(i,m_stamp),alpha,true);
            DrawImage(m_frame,b,alpha);
        }
        else
        {
            char text[7];std::snprintf(text,sizeof(text),"%06u",Q_min(999999u,count));
            for(int d=0;d<6;++d)
            {
                char file[96];std::snprintf(file,sizeof(file),m_digitFiles[d].c_str(),text[d]-'0');
                DrawImage(file,WorldBox(0,m_digits[d]),alpha);
            }
        }
        if(i==selected)
        {
            const int x=b.p.x-2,y=b.p.y-2,w=b.z.w+4,h=b.z.h+4;const unsigned ink=PackRGBA(224,194,119,220);
            UI_FillRect(x,y,w,1,ink);UI_FillRect(x,y+h-1,w,1,ink);UI_FillRect(x,y,1,h,ink);UI_FillRect(x+w-1,y,1,h,ink);
        }
    }
    void MovePointer()
    {
        Pane panes[2];const int n=Panes(panes);if(!n)return;
        const float x=float(uiStatic.cursorX-m_grab.x),y=float(uiStatic.cursorY-m_grab.y);
        int best=0;float distance=std::numeric_limits<float>::max();
        for(int i=0;i<n;++i)
        {
            const Pane &p=panes[i];const float dx=Q_max(float(p.p.x)-x,Q_max(0.f,x-p.p.x-p.z.w));
            const float dy=Q_max(float(p.p.y)-y,Q_max(0.f,y-p.p.y-p.z.h)),d=dx*dx+dy*dy;
            if(d<distance){distance=d;best=i;}
        }
        const Pane &p=panes[best];const Region &r=p.region;
        const float pu=Q_max(0.f,Q_min(1.f,(x-p.p.x)/p.z.w)),pv=Q_max(0.f,Q_min(1.f,(y-p.p.y)/p.z.h));
        SetPosition(r.u0+pu*(r.u1-r.u0),1-r.v0-pv*(r.v1-r.v0));
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
        Text(48,490,left,24,"Drag an attachment. Arrows and sliders also move it.",13,Muted());
        Text(48,516,left,22,m_weapon==10?"Both inner gun sides are editable. Editing turns it ON.":
            "Editing turns it ON; you can switch it OFF.",12,Muted());
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
        UpdatePreview();CMenuFramework::Draw();
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
        Con_Printf("accessory_regions: loaded=%d stat_allowed=%d charm_allowed=%d dual_sides=%d\n",
            int(m_domains.valid),int(oc2_accessory_domains::Allowed(m_domains,m_weapon,m_draft.statU,m_draft.statV)),
            int(oc2_accessory_domains::Allowed(m_domains,m_weapon,m_draft.charmU,m_draft.charmV)),int(m_weapon==10));
        UpdatePreview();m_pad.Dump();
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
        m_domains=oc2_accessory_domains::Domains();
        std::string regionText,regionError;
        if(Read(oc2_accessory_domains::kPath,regionText))oc2_accessory_domains::Parse(regionText.c_str(),m_domains,regionError);
        std::string previewText;
        m_pad.LoadPreview(Read("gfx/inventory/previews/editor_previews.dat",previewText)?previewText.c_str():nullptr);
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
        m_pad.onChanged=VoidCb(&CMenuAccessoriesEditor::PositionEdited);
        m_pad.onSelected=VoidCb(&CMenuAccessoriesEditor::PreviewSelected);
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
    void SyncEnableLabels()
    {
        m_statToggle.SetText(m_draft.stattrak?"StatTrak ON":"StatTrak OFF");
        m_charmToggle.SetText(m_draft.charm?"Charm ON":"Charm OFF");
    }
    void EditedAttachment(bool charm)
    {
        (charm?m_draft.charm:m_draft.stattrak)=true;
        m_dirty=true;SyncEnableLabels();
        Message(charm?"Charm ON. Apply to save.":"StatTrak ON. Apply to save.");
    }
    void PositionEdited(){EditedAttachment(m_tab!=0);SyncPositionSliders();}
    void PreviewSelected(){m_tab=m_pad.selected;Sync();}
    void UpdatePreview()
    {
        m_pad.draft=&m_draft;m_pad.count=m_count;m_pad.selected=m_tab;
        const oc2_accessories::Sticker *sticker=SelectedSticker();
        m_pad.sticker=sticker?std::string(oc2_accessories::kStickerDirectory)+sticker->file:std::string();
    }
    void Sync()
    {
        SyncEnableLabels();
        m_statTab.colorBase=m_tab?Ink():Accent();m_charmTab.colorBase=m_tab?Accent():Ink();
        m_pad.u=m_tab?&m_draft.charmU:&m_draft.statU;m_pad.v=m_tab?&m_draft.charmV:&m_draft.statV;m_pad.weapon=m_weapon;
        m_pad.domains=&m_domains;
        UpdatePreview();
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
            if(i<2)
            {
                const float u=m_tab?m_draft.charmU:m_draft.statU,v=m_tab?m_draft.charmV:m_draft.statV;
                const float value=m_sliders[i].GetCurrentValue()*.01f;
                m_pad.SetPosition(i==0?value:u,i==1?value:v);SyncPositionSliders();return;
            }
            float *const values[4]={&m_draft.amplitude,&m_draft.length,&m_draft.stiffness,&m_draft.damping};
            const float value=m_sliders[i].GetCurrentValue();
            if(*values[i-2]!=value){*values[i-2]=value;EditedAttachment(true);}
            return;
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
        i=(i+direction+int(m_catalog.size()))%int(m_catalog.size());m_draft.sticker=m_catalog[i].id;
        EditedAttachment(true);
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
        m_importStatus="registered";EditedAttachment(true);Message("Image added; Charm ON. Apply to save.");
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
        m_revision=latest.revision;m_dirty=false;m_catalogDirty=false;
        char status[96];std::snprintf(status,sizeof(status),"Saved: StatTrak %s / Charm %s.",
            m_draft.stattrak?"ON":"OFF",m_draft.charm?"ON":"OFF");Message(status);
    }
    void UseDefaults()
    {
        const auto defaults=oc2_accessories::DefaultConfig();
        const auto *value=oc2_accessories::Find(defaults,m_weapon);if(!value)return;
        oc2_accessories::WeaponConfig next=*value;
        oc2_accessory_domains::NearestAllowed(m_domains,m_weapon,next.statU,next.statV);
        oc2_accessory_domains::NearestAllowed(m_domains,m_weapon,next.charmU,next.charmV);
        const int sticker=m_draft.sticker;
        m_draft=next;m_draft.stattrak=true;m_draft.charm=true;m_draft.sticker=sticker;
        m_pad.dragging=false;m_dirty=true;Sync();Message("Defaults: both ON. Apply to save.");
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
    oc2_accessory_domains::Domains m_domains;
    CMenuAction m_statTab,m_charmTab,m_statToggle,m_charmToggle,m_apply,m_revert,m_defaults,m_back,m_reset;
    CMenuAction m_prevSticker,m_nextSticker,m_import;
    CMenuSlider m_sliders[6];CMenuField m_filename;CMenuAccessoryPad m_pad;
    CMenuYesNoMessageBox m_resetDialog,m_discardDialog;
};
