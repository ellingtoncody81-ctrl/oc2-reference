#pragma once
#include "../../../../common/oc2_skin_inventory.h"
#include "Action.h"
#include <cstdio>
#include <cstring>

namespace oc2_local_inventory
{
inline bool Read(const char *path,std::string &text)
{
    int length=0;byte *buffer=EngFuncs::COM_LoadFile(path,&length);
    if(!buffer)return false;
    const bool valid=length>=0&&length<=4*1024*1024&&std::memchr(buffer,0,std::size_t(length))==nullptr;
    if(valid)text.assign(reinterpret_cast<const char*>(buffer),std::size_t(length));
    EngFuncs::COM_FreeFile(buffer);return valid;
}
inline bool LoadConfig(oc2_inventory::Config &config,std::string &error)
{
    config=oc2_inventory::DefaultConfig();std::string text;
    if(Read(oc2_inventory::kConfigPath,text))return oc2_inventory::ParseConfig(text.c_str(),config,error);
    if(!EngFuncs::FileExists(oc2_inventory::kConfigPath,true))return true;
    error="Saved loadout could not be read.";return false;
}
inline bool LoadCatalog(oc2_inventory::Catalog &catalog,std::string &error)
{
    std::string text;
    if(!Read(oc2_inventory::kCatalogPath,text)){error="Local skin catalog could not be read.";return false;}
    return oc2_inventory::ParseCatalog(text.c_str(),catalog,error);
}
inline bool SaveConfig(const oc2_inventory::Config &config)
{
    const std::string text=oc2_inventory::SerializeConfig(config);
    EngFuncs::COM_SaveFile(oc2_inventory::kConfigPath,text.data(),int(text.size()));
    std::string saved;return Read(oc2_inventory::kConfigPath,saved)&&saved==text;
}
inline std::string Fold(std::string value)
{
    for(char &c:value)if(c>='A'&&c<='Z')c=char(c+'a'-'A');
    return value;
}
inline std::string SkinName(const oc2_inventory::Catalog &catalog,int id)
{
    if(!id)return "Original";
    const auto *entry=oc2_inventory::FindEntry(catalog,id);
    return entry?entry->name:"Saved skin";
}
inline void DrawImage(const std::string &path,Point p,Size box)
{
    if(path.empty())return;
    const HIMAGE image=EngFuncs::PIC_Load(path.c_str());if(!image)return;
    const int width=EngFuncs::PIC_Width(image),height=EngFuncs::PIC_Height(image);
    if(width<=0||height<=0)return;
    const float scale=Q_min(float(box.w)/width,float(box.h)/height);
    const Size size(Q_max(1,int(width*scale)),Q_max(1,int(height*scale)));
    p.x+=(box.w-size.w)/2;p.y+=(box.h-size.h)/2;
    EngFuncs::PIC_Set(image,255,255,255,255);EngFuncs::PIC_DrawTrans(p,size);
}
}

class CMenuLocalInventoryCard : public CMenuAction
{
public:
    std::string title,detail,image;
    bool equipped=false,texture=false;
    void Draw() override
    {
        CMenuAction::Draw();const Point p=GetRenderPosition();const Size z=GetRenderSize();
        const int inset=Q_max(1,int(10*uiStatic.scaleY));
        const unsigned ink=PackRGBA(234,239,243,255),accent=PackRGBA(221,193,124,255);
        if(equipped)UI_FillRect(p,Size(z.w,Q_max(1,int(3*uiStatic.scaleY))),accent);
        UI_DrawString(uiStatic.hBoldFont,p+Point(inset,inset),Size(z.w-inset*2,int(23*uiStatic.scaleY)),
            title.c_str(),ink,Q_max(1,int(15*uiStatic.scaleY)),QM_LEFT,ETF_FORCECOL|ETF_NO_WRAP);
        oc2_local_inventory::DrawImage(image,p+Point(inset,int(35*uiStatic.scaleY)),Size(z.w-inset*2,int(49*uiStatic.scaleY)));
        UI_DrawString(uiStatic.hDefaultFont,p+Point(inset,int(91*uiStatic.scaleY)),Size(z.w-inset*2,int(22*uiStatic.scaleY)),
            detail.c_str(),equipped?accent:ink,Q_max(1,int(12*uiStatic.scaleY)),QM_LEFT,ETF_FORCECOL|ETF_NO_WRAP);
        if(texture)UI_DrawString(uiStatic.hDefaultFont,p+Point(inset,int(75*uiStatic.scaleY)),Size(z.w-inset*2,int(14*uiStatic.scaleY)),
            "TEXTURE",PackRGBA(159,174,187,255),Q_max(1,int(9*uiStatic.scaleY)),QM_RIGHT,ETF_FORCECOL|ETF_NO_WRAP);
    }
};
