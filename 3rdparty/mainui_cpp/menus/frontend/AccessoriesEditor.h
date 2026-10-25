#pragma once
#include "PanoramaChrome.h"
#include "Framework.h"
#include "Action.h"
#include "Slider.h"
#include "Field.h"
#include "YesNoMessageBox.h"
#include "keydefs.h"
#include "../../../../common/oc2_accessories.h"
#include "../../../../common/oc2_accessory_domains.h"
#include "SkinInventory.h"
#include "ProfileInventory.h"
#include "PanoramaModelView.h"
#include "PanoramaInventoryLabels.h"
#include <cstdio>
#include <cstring>
#include <cmath>

class CMenuAccessoriesEditor;
class IMenuCharmPatternPicker
{
public:
    virtual void OpenCharmPattern(int weapon,int skin,int pattern,CMenuAccessoriesEditor *owner)=0;
};

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
    const oc2_inventory::WeaponState *appearance=nullptr;
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
    bool SetPosition(float x,float y,bool snap=true)
    {
        if(!u||!v)return false;
        x=Q_max(0.f,Q_min(1.f,x));y=Q_max(0.f,Q_min(1.f,y));
        if(snap)SnapToSurface(x,y);
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
        UI_FillRect(p,z,PackRGBA(24,36,46,230));
        UI_DrawRectangle(p,z,PackRGBA(124,146,161,80));
        for(int i=1;i<4;++i)
        {
            UI_FillRect(p.x+z.w*i/4,p.y,1,z.h,PackRGBA(124,146,161,35));
            UI_FillRect(p.x,p.y+z.h*i/4,z.w,1,PackRGBA(124,146,161,35));
        }
        if(!appearance||!draft)return;
        m_model.SetInteractive(false);m_model.SetAppearance(*appearance,draft);
        m_model.SetAccessoryPreview(sticker,count);
        if(!m_model.DrawAt(p.x,p.y,z.w,z.h))
            oc2_panorama::DrawText(p,z,u8"3D 预览加载中",16,PackRGBA(160,175,189,255),false,QM_CENTER);
        DrawSelection();
    }
    void Dump()
    {
        m_model.Dump("accessory_detail");
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
    CMenuPanoramaModelView m_model;
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
    {x=draft?(i?draft->charmU:draft->statU):.5f;y=draft?(i?draft->charmV:draft->statV):.5f;}
    Point Anchor(int i)const
    {
        float u,v;Position(i,u,v);float x=0,y=0,scale=0;
        if(!m_model.ProjectAccessory(i,u,v,x,y,scale))return GetRenderPosition();
        return Point(int(std::lround(x)),int(std::lround(y)));
    }
    Box AttachmentBox(int i)const
    {
        float left=0,top=0,right=0,bottom=0;
        if(!m_model.AttachmentBounds(i,left,top,right,bottom))return {Point(),Size(0,0)};
        const int x=int(std::floor(left)),y=int(std::floor(top));
        return {Point(x,y),Size(Q_max(1,int(std::ceil(right))-x),Q_max(1,int(std::ceil(bottom))-y))};
    }
    bool HitAttachment(int i)const
    {
        const Box b=AttachmentBox(i);if(b.z.w<=0||b.z.h<=0)return false;const int tolerance=3;
        return UI_CursorInRect(Point(b.p.x-tolerance,b.p.y-tolerance),Size(b.z.w+2*tolerance,b.z.h+2*tolerance));
    }
    void DrawSelection()
    {
        if(!draft)return;const Box b=AttachmentBox(selected);
        if(b.z.w<=0||b.z.h<=0)return;
        const Point p=GetRenderPosition();const Size z=GetRenderSize();
        const int x=Q_max(p.x,b.p.x-2),y=Q_max(p.y,b.p.y-2);
        const int right=Q_min(p.x+z.w,b.p.x+b.z.w+2),bottom=Q_min(p.y+z.h,b.p.y+b.z.h+2);
        const int w=right-x,h=bottom-y;if(w<=0||h<=0)return;
        const unsigned ink=PackRGBA(224,194,119,220);
        UI_FillRect(x,y,w,1,ink);UI_FillRect(x,y+h-1,w,1,ink);UI_FillRect(x,y,1,h,ink);UI_FillRect(x+w-1,y,1,h,ink);
    }
    void MovePointer()
    {
        const float x=float(uiStatic.cursorX-m_grab.x),y=float(uiStatic.cursorY-m_grab.y);
        float nextU=0,nextV=0;
        if(m_model.UnprojectAccessory(x,y,nextU,nextV))SetPosition(nextU,nextV,false);
    }
};

class CMenuAccessoriesEditor : public CMenuFramework
{
public:
    CMenuAccessoriesEditor():CMenuFramework("CMenuAccessoriesEditor"){}
    void SetPatternPicker(IMenuCharmPatternPicker *picker){m_patternPicker=picker;}
    void Open(int weapon,int skin=-1)
    {
        if(oc2_accessories::Index(weapon)<0)return;
        m_weapon=weapon;m_skin=skin;m_tab=0;ReloadData();
        CMenuFramework::Show();Sync();SetCursorToItem(m_statTab);
    }
    void SelectInventoryPattern(int id)
    {
        m_appearance.charm=id;m_patternOverride=id;m_patternChanged=false;
        m_inventoryPatternFile.clear();m_inventoryPatternName.clear();
        ResolvePattern();m_draft.charm=id!=-1;m_dirty=true;
        Sync();Message(u8"已选择挂件图案，点击应用保存当前皮肤设置。");
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
        const int w=uiStatic.width,left=Left(),right=Right(),rw=w-right-34;
        oc2_panorama::DrawBackground();
        Fill(64,0,w-64,96,PackRGBA(12,18,25,80));Fill(98,95,w-132,1,PackRGBA(190,206,218,80));
        const std::string heading=std::string(oc2_accessories::Name(m_weapon))+u8"  /  "+
            oc2_inventory_labels::Finish(m_skinCatalog,m_skin)+u8"  /  挂件与计数器";
        Text(98,30,w-132,42,heading.c_str(),28,Ink(),true);
        Fill(84,168,left+28,508,PackRGBA(19,29,38,150));
        Fill(right-14,168,rw+28,508,PackRGBA(18,28,37,210));
        Text(98,181,left,30,m_tab?u8"挂件位置 · 实物同比例预览":u8"计数器位置 · 实物同比例预览",20,Accent(),true);
        Text(98,508,left,24,u8"拖动附件调整位置 · 方向键微调",13,Ink());
        Text(98,534,left,22,m_weapon==10?u8"双枪内侧整片枪体可调，挂点随承载部件固定":
            u8"有贴纸一侧的整片枪体可调，挂点随承载部件固定",12,Muted());
        char value[160];
        std::snprintf(value,sizeof(value),u8"水平位置  %.0f%%",100*(m_tab?m_draft.charmU:m_draft.statU));
        Text(98,561,left,24,value,15,Ink());
        std::snprintf(value,sizeof(value),u8"垂直位置  %.0f%%",100*(m_tab?m_draft.charmV:m_draft.statV));
        Text(98,611,left,24,value,15,Ink());
        if(!m_tab)
        {
            Text(right,181,rw,30,u8"累计击杀",20,Accent(),true);
            if(m_countsValid)std::snprintf(value,sizeof(value),"%06u",m_count);
            else std::snprintf(value,sizeof(value),u8"无法读取");
            Text(right,242,rw,70,value,46,Accent(),true);
            Fill(right+24,390,rw-48,1,PackRGBA(190,206,218,60));
            Text(right,414,rw,28,u8"启用计数器后，记录敌方击杀",16,Ink());
            Text(right,454,rw,28,u8"重置仅清空当前武器的计数",15,Muted());
            Text(right,494,rw,28,u8"位置与开关点击应用后保存",15,Muted());
            Text(right,534,rw,28,u8"击杀记录独立保留",15,Muted());
        }
        else
        {
            Text(right,181,rw,30,m_patternChanged?u8"挂件图案 · 待应用":m_patternOverride<0?u8"挂件当前已卸下":m_patternOverride?u8"正在使用库存图案":u8"挂件图案",20,Accent(),true);
            const std::string pattern=PatternFile();
            if(!pattern.empty())
            {
                const HIMAGE image=EngFuncs::PIC_Load(pattern.c_str());
                if(image){EngFuncs::PIC_Set(image,255,255,255,255);EngFuncs::PIC_DrawTrans(Pixel(right,224),Size(92,92).Scale());}
                const std::string patternName=PatternName();
                Text(right+108,224,rw-108,42,patternName.c_str(),18,Ink(),true);
            }
            Text(right,323,rw,24,u8"导入自定义图案 · 输入图片文件名",13,Muted());
            if(!m_patternOverride||m_patternChanged)Text(right,397,rw,24,u8"支持 PNG、TGA、BMP 图片",12,Muted());
            static const char *names[4]={u8"摆动幅度（度）",u8"挂件长度",u8"回弹强度",u8"摆动阻尼"};
            const float values[4]={m_draft.amplitude,m_draft.length,m_draft.stiffness,m_draft.damping};
            for(int i=0;i<4;++i)
            {
                std::snprintf(value,sizeof(value),"%s  %.2f",names[i],values[i]);
                Text(right,430+i*56,rw,24,value,14,Ink());
            }
        }
        Fill(64,704,w-64,64,PackRGBA(9,14,20,125));
        Text(98,678,w-132,24,m_message.c_str(),13,m_error?PackRGBA(246,133,119,255):Ink());
        if(m_dirty)Text(w-310,75,276,22,u8"有未保存的更改",13,Accent());
        UpdatePreview();m_chrome.Draw();CMenuFramework::Draw();
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
        Con_Printf("accessory_profile: weapon=%d skin=%d\n",m_weapon,m_skin);
        Con_Printf("accessory_pattern_source: override=%d source=%s\n",m_patternOverride,m_patternOverride<0?"unequipped":m_patternOverride?"skin_profile":"accessories");
        Con_Printf("accessory_pattern_draft: changed=%d applies_with_settings=1\n",int(m_patternChanged));
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
    int Left()const{return (uiStatic.width-164)*59/100;}
    int Right()const{return 122+Left();}
    Point Pixel(int x,int y)const{return GetRenderPosition()+Point(x,y).Scale();}
    void Fill(int x,int y,int w,int h,unsigned color){UI_FillRect(Pixel(x,y),Size(w,h).Scale(),color);}
    void Text(int x,int y,int w,int h,const char *text,int height,unsigned color,bool bold=false,unsigned align=QM_CENTER)
    {
        oc2_panorama::DrawText(Pixel(x,y),Size(w,h).Scale(),text,height,color,bold,align);
    }
    static void DumpItem(CMenuBaseItem &item,bool visible)
    {
        const Point p=item.GetRenderPosition();const Size s=item.GetRenderSize();
        Con_Printf("frontend_item: tag=%s x=%d y=%d w=%d h=%d visible=%d enabled=%d focus=%d units=screen_pixels\n",
            item.szTag?item.szTag:"none",p.x,p.y,s.w,s.h,int(visible),int(!(item.iFlags&(QMF_GRAYED|QMF_INACTIVE))),int(item.IsCurrentSelected()));
    }
    void Button(CMenuAction &b,const char *name,const char *tag)
    {
        b.SetNameAndStatus(name,nullptr,tag);oc2_panorama::CenterButton(b);
        b.eFocusAnimation=QM_HIGHLIGHTIFFOCUS;b.bIgnoreColorstring=true;b.colorBase=Ink();b.colorFocus=Accent();
        b.SetBackground(PackRGBA(36,48,59,228),PackRGBA(58,75,89,245));AddItem(b);
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
        oc2_local_profiles::State state;std::string text,error;
        bool valid=oc2_local_profiles::Load(state,error);
        m_skinCatalog.clear();
        valid=oc2_local_inventory::LoadCatalog(m_skinCatalog,error)&&valid;
        if(m_skin<0){const auto *equipped=oc2_inventory::Find(state.loadout,m_weapon);m_skin=equipped?equipped->skin:0;}
        const auto profile=oc2_local_profiles::Resolve(state,m_weapon,m_skin);
        m_configValid=valid;m_revision=state.loadout.revision;m_draft=profile.accessories;m_appearance=profile.appearance;
        m_dirty=false;m_patternChanged=false;
        oc2_accessories::Counts counts=oc2_accessories::DefaultCounts();
        m_countsValid=true;
        if(Read(oc2_accessories::kCountsPath,text))m_countsValid=oc2_accessories::ParseCounts(text.c_str(),counts,error);
        else if(EngFuncs::FileExists(oc2_accessories::kCountsPath,true))m_countsValid=false;
        m_count=counts.values[oc2_accessories::Index(m_weapon)];
        m_catalog=oc2_accessories::DefaultCatalog();m_catalogValid=true;m_catalogDirty=false;m_importStatus="idle";
        if(Read(oc2_accessories::kCatalogPath,text))m_catalogValid=oc2_accessories::ParseCatalog(text.c_str(),m_catalog,error);
        else if(EngFuncs::FileExists(oc2_accessories::kCatalogPath,true))m_catalogValid=false;
        ScanStickers();
        m_patternOverride=m_appearance.charm;
        m_inventoryPatternFile.clear();m_inventoryPatternName.clear();
        ResolvePattern();
        Message(!valid||!m_catalogValid?u8"存档无法读取，请先恢复有效文件。":
            !m_countsValid?u8"击杀记录无法读取，已保留原文件。":u8"已读取保存的附件设置。",
            !valid||!m_catalogValid||!m_countsValid);
    }
    void ResolvePattern()
    {
        if(m_patternOverride>0)if(const auto *entry=oc2_inventory::FindEntry(m_skinCatalog,m_patternOverride))
        {
            m_inventoryPatternFile=entry->file;
            m_inventoryPatternName=oc2_inventory_labels::Sticker(m_skinCatalog,m_patternOverride);
        }
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
    std::string PatternFile()const
    {
        if(!m_patternChanged&&m_patternOverride>0&&!m_inventoryPatternFile.empty())return m_inventoryPatternFile;
        const auto *sticker=SelectedSticker();return sticker?std::string(oc2_accessories::kStickerDirectory)+sticker->file:std::string();
    }
    std::string PatternName()const
    {
        if(!m_patternChanged&&m_patternOverride>0&&!m_inventoryPatternName.empty())return m_inventoryPatternName;
        for(std::size_t i=0;i<m_catalog.size();++i)if(m_catalog[i].id==m_draft.sticker)
            return std::string(u8"挂件图案 · 序号")+std::to_string(i+1);
        return u8"未选择图案";
    }
    void _Init() override
    {
        m_chrome.Init(*this,1);
        Button(m_statTab,u8"计数器位置","accessory_tab_stat");m_statTab.onReleased=VoidCb(&CMenuAccessoriesEditor::StatTab);
        Button(m_charmTab,u8"挂件位置与图案","accessory_tab_charm");m_charmTab.onReleased=VoidCb(&CMenuAccessoriesEditor::CharmTab);
        Button(m_statToggle,u8"计数器：关闭","accessory_stat_toggle");m_statToggle.onReleased=VoidCb(&CMenuAccessoriesEditor::ToggleStat);
        Button(m_charmToggle,u8"挂件：关闭","accessory_charm_toggle");m_charmToggle.onReleased=VoidCb(&CMenuAccessoriesEditor::ToggleCharm);
        Button(m_apply,u8"应用并保存","accessory_apply");m_apply.onReleased=VoidCb(&CMenuAccessoriesEditor::Apply);
        Button(m_revert,u8"还原","accessory_revert");m_revert.onReleased=VoidCb(&CMenuAccessoriesEditor::Revert);
        Button(m_defaults,u8"恢复默认","accessory_defaults");m_defaults.onReleased=VoidCb(&CMenuAccessoriesEditor::UseDefaults);
        Button(m_back,u8"返回检视","accessory_back");m_back.onReleased=VoidCb(&CMenuAccessoriesEditor::Back);
        Button(m_reset,u8"重置当前武器计数","accessory_reset");m_reset.onReleased=VoidCb(&CMenuAccessoriesEditor::ConfirmReset);
        Button(m_prevSticker,u8"< 上一款","accessory_sticker_previous");m_prevSticker.onReleased=VoidCb(&CMenuAccessoriesEditor::PreviousSticker);
        Button(m_nextSticker,u8"下一款 >","accessory_sticker_next");m_nextSticker.onReleased=VoidCb(&CMenuAccessoriesEditor::NextSticker);
        Button(m_import,u8"添加图片","accessory_import");m_import.onReleased=VoidCb(&CMenuAccessoriesEditor::ImportSticker);
        Button(m_useAccessoryPattern,u8"改用本页挂件图案","accessory_use_pattern");m_useAccessoryPattern.onReleased=VoidCb(&CMenuAccessoriesEditor::UseAccessoryPattern);
        Button(m_choosePattern,u8"选择挂件图案","accessory_choose_pattern");m_choosePattern.onReleased=VoidCb(&CMenuAccessoriesEditor::ChoosePattern);
        m_apply.colorBase=PackRGBA(20,26,31,255);m_apply.colorFocus=PackRGBA(8,12,17,255);
        m_apply.SetBackground(Accent(),PackRGBA(239,216,160,255));
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
        m_resetDialog.Link(this);m_resetDialog.SetMessage(u8"将当前武器的累计击杀重置为零？");
        m_resetDialog.SetPositiveButton(u8"确认重置",PC_OK);m_resetDialog.SetNegativeButton(u8"取消",PC_CANCEL);
        m_resetDialog.yes.SetPicture("");m_resetDialog.no.SetPicture("");
        m_resetDialog.yes.eTextAlignment=m_resetDialog.no.eTextAlignment=QM_CENTER;
        m_resetDialog.onPositive=VoidCb(&CMenuAccessoriesEditor::ResetCount);
        m_resetDialog.yes.szTag="accessory_reset_yes";m_resetDialog.no.szTag="accessory_reset_no";
        m_discardDialog.Link(this);m_discardDialog.SetMessage(u8"放弃当前武器尚未保存的更改？");
        m_discardDialog.SetPositiveButton(u8"放弃更改",PC_OK);m_discardDialog.SetNegativeButton(u8"继续编辑",PC_CANCEL);
        m_discardDialog.yes.SetPicture("");m_discardDialog.no.SetPicture("");
        m_discardDialog.yes.eTextAlignment=m_discardDialog.no.eTextAlignment=QM_CENTER;
        m_discardDialog.onPositive=VoidCb(&CMenuAccessoriesEditor::Discard);
        m_discardDialog.yes.szTag="accessory_discard_yes";m_discardDialog.no.szTag="accessory_discard_no";
    }
    void _VidInit() override
    {
        m_chrome.Layout();const int w=uiStatic.width,left=Left(),right=Right(),rw=w-right-34;
        m_statTab.SetRect(98,112,(left-10)/2,42);m_charmTab.SetRect(108+(left-10)/2,112,(left-10)/2,42);
        m_statToggle.SetRect(right,112,(rw-10)/2,42);m_charmToggle.SetRect(right+(rw+10)/2,112,(rw-10)/2,42);
        m_pad.SetRect(98,224,left,276);m_sliders[0].SetRect(114,587,left-32,20);m_sliders[1].SetRect(114,637,left-32,20);
        m_reset.SetRect(right+24,330,rw-48,42);
        m_choosePattern.SetRect(right+108,274,rw-108,42);
        m_prevSticker.SetRect(right+108,274,(rw-120)/2,42);
        m_nextSticker.SetRect(right+120+(rw-120)/2,274,(rw-120)/2,42);
        m_filename.SetRect(right,358,rw-120,30);m_import.SetRect(w-144,352,110,38);
        m_useAccessoryPattern.SetRect(right,394,rw,28);
        for(int i=0;i<4;++i)m_sliders[i+2].SetRect(right,466+i*56,rw,20);
        m_back.SetRect(98,718,134,36);m_revert.SetRect(w-310,718,112,36);m_apply.SetRect(w-184,718,150,36);
        m_defaults.SetRect(w-454,718,130,36);
        Sync();
    }
    void SyncPositionSliders()
    {
        m_sliders[0].SetCurrentValue(100*(m_tab?m_draft.charmU:m_draft.statU));
        m_sliders[1].SetCurrentValue(100*(m_tab?m_draft.charmV:m_draft.statV));
    }
    void SyncEnableLabels()
    {
        m_statToggle.SetText(m_draft.stattrak?u8"计数器：开启":u8"计数器：关闭");
        m_charmToggle.SetText(m_draft.charm?u8"挂件：开启":u8"挂件：关闭");
    }
    void EditedAttachment(bool charm)
    {
        (charm?m_draft.charm:m_draft.stattrak)=true;
        m_dirty=true;SyncEnableLabels();
        Message(charm?u8"挂件已启用，点击应用保存。":u8"计数器已启用，点击应用保存。");
    }
    void PositionEdited(){EditedAttachment(m_tab!=0);SyncPositionSliders();}
    void PreviewSelected(){m_tab=m_pad.selected;Sync();}
    void UpdatePreview()
    {
        m_pad.draft=&m_draft;m_pad.count=m_count;m_pad.selected=m_tab;
        m_appearance.charm=m_patternChanged?0:m_patternOverride;m_pad.appearance=&m_appearance;
        m_pad.sticker=PatternFile();
    }
    void Sync()
    {
        SyncEnableLabels();
        m_statTab.colorBase=m_tab?Ink():Accent();m_charmTab.colorBase=m_tab?Accent():Ink();
        m_pad.u=m_tab?&m_draft.charmU:&m_draft.statU;m_pad.v=m_tab?&m_draft.charmV:&m_draft.statV;m_pad.weapon=m_weapon;
        m_pad.domains=&m_domains;
        UpdatePreview();
        m_reset.SetVisibility(!m_tab);m_prevSticker.SetVisibility(false);m_nextSticker.SetVisibility(false);
        m_choosePattern.SetVisibility(m_tab);m_choosePattern.SetGrayed(!m_patternPicker||!m_configValid);
        m_filename.SetVisibility(m_tab);m_import.SetVisibility(m_tab);
        m_useAccessoryPattern.SetVisibility(m_tab&&m_patternOverride!=0&&!m_patternChanged);
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
    void ToggleCharm()
    {
        m_draft.charm=!m_draft.charm;
        if(m_draft.charm&&m_patternOverride<0){m_patternOverride=0;m_appearance.charm=0;}
        m_dirty=true;Sync();
    }
    void SelectSticker(int direction)
    {
        if(m_catalog.empty())return;
        int i=0;for(std::size_t n=0;n<m_catalog.size();++n)if(m_catalog[n].id==m_draft.sticker)i=int(n);
        i=(i+direction+int(m_catalog.size()))%int(m_catalog.size());m_draft.sticker=m_catalog[i].id;
        m_patternChanged=true;EditedAttachment(true);Sync();
    }
    void PreviousSticker(){SelectSticker(-1);}
    void NextSticker(){SelectSticker(1);}
    void ChoosePattern()
    {
        m_pad.dragging=false;
        if(m_patternPicker)m_patternPicker->OpenCharmPattern(m_weapon,m_skin,m_patternChanged?0:m_patternOverride,this);
    }
    void ImportSticker()
    {
        const std::string file=m_filename.GetBuffer();
        if(!m_catalogValid){m_importStatus="catalog_invalid";Message(u8"图案目录无法读取。",true);return;}
        if(!AddSticker(file,true)){Message(u8"图片无法载入，请检查文件名和格式。",true);return;}
        for(const auto &s:m_catalog)if(s.file==file)m_draft.sticker=s.id;
        m_patternChanged=true;m_importStatus="registered";EditedAttachment(true);Sync();Message(u8"已选择新图片，点击应用保存图案与附件设置。");
    }
    void UseAccessoryPattern()
    {
        m_patternChanged=true;m_dirty=true;Sync();Message(u8"已选择本页图案，点击应用保存。");
    }
    void Apply()
    {
        if(!m_configValid||!m_catalogValid||!oc2_accessories::Valid(m_draft))
            {Message(u8"设置无效，未保存更改。",true);return;}
        oc2_local_profiles::State latest;std::string error;
        if(!oc2_local_profiles::Load(latest,error))
            {Message(u8"皮肤设置存档无法读取，未保存更改。",true);return;}
        auto profile=oc2_local_profiles::Resolve(latest,m_weapon,m_skin);
        profile.accessories=m_draft;
        profile.appearance.charm=m_patternChanged?0:m_patternOverride;
        if(m_catalogDirty&&!Write(oc2_accessories::kCatalogPath,oc2_accessories::SerializeCatalog(m_catalog)))
            {Message(u8"无法保存图案目录。",true);return;}
        if(!oc2_local_profiles::SaveProfile(profile,m_skinCatalog,error))
            {Message(u8"保存失败，当前皮肤的草稿仍保留。",true);return;}
        m_appearance=profile.appearance;
        if(m_patternChanged)m_patternOverride=0;
        m_patternChanged=false;
        ++m_revision;m_dirty=false;m_catalogDirty=false;
        char status[160];std::snprintf(status,sizeof(status),u8"已保存：计数器%s / 挂件%s。",
            m_draft.stattrak?u8"开启":u8"关闭",m_draft.charm?u8"开启":u8"关闭");Message(status);
        Sync();
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
        m_pad.dragging=false;m_dirty=true;Sync();Message(u8"已恢复默认位置并启用附件，点击应用保存。");
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
        if(!m_countsValid){Message(u8"击杀记录无法读取，请先恢复有效存档。",true);return;}
        m_resetDialog.Show();m_resetDialog.SetCursorToItem(m_resetDialog.no);
    }
    void ResetCount()
    {
        oc2_accessories::Counts latest=oc2_accessories::DefaultCounts();std::string text,error;
        const bool loaded=Read(oc2_accessories::kCountsPath,text);
        if((loaded&&!oc2_accessories::ParseCounts(text.c_str(),latest,error))||
            (!loaded&&EngFuncs::FileExists(oc2_accessories::kCountsPath,true)))
            {m_countsValid=false;Message(u8"击杀存档无法读取，未重置计数。",true);return;}
        latest.values[oc2_accessories::Index(m_weapon)]=0;
        if(!Write(oc2_accessories::kCountsPath,oc2_accessories::SerializeCounts(latest)))
            {Message(u8"无法保存击杀记录。",true);return;}
        m_count=0;m_countsValid=true;Message(u8"已重置当前武器的累计击杀。");
    }
    int m_weapon=1,m_skin=-1,m_tab=0,m_patternOverride=0;unsigned m_count=0,m_revision=1;
    bool m_dirty=false,m_error=false,m_configValid=true,m_countsValid=true,m_catalogValid=true,m_catalogDirty=false,m_patternChanged=false;
    std::string m_message,m_inventoryPatternFile,m_inventoryPatternName;
    const char *m_importStatus="idle";
    oc2_accessories::WeaponConfig m_draft;
    oc2_inventory::WeaponState m_appearance;
    oc2_inventory::Catalog m_skinCatalog;
    oc2_accessories::Catalog m_catalog;
    oc2_accessory_domains::Domains m_domains;
    CMenuAction m_statTab,m_charmTab,m_statToggle,m_charmToggle,m_apply,m_revert,m_defaults,m_back,m_reset;
    CMenuAction m_prevSticker,m_nextSticker,m_import,m_useAccessoryPattern,m_choosePattern;
    CMenuSlider m_sliders[6];CMenuField m_filename;CMenuAccessoryPad m_pad;CMenuPanoramaChrome m_chrome;
    CMenuYesNoMessageBox m_resetDialog,m_discardDialog;
    IMenuCharmPatternPicker *m_patternPicker=nullptr;
};
