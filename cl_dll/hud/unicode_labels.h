#pragma once
#include "hud.h"
#include "match_ui.h"
#include "cl_dll/IGameMenuExports.h"
#include "utflib.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <string>

// A small adapter for fixed Chinese HUD labels. Existing HUD numbers, player
// names and ASCII weapon model names retain their own drawing paths.
namespace hud_unicode
{
namespace detail
{
inline std::map<int,HFont> &Fonts(){static std::map<int,HFont> fonts;return fonts;}
}
inline void ClearFonts(){detail::Fonts().clear();}
inline float LabelScale(){return gHUD.m_flScale>0?gHUD.m_flScale:1.0f;}
inline bool UnicodeLabel(const char *text)
{
    if(text)for(const unsigned char *ch=reinterpret_cast<const unsigned char*>(text);*ch;++ch)if(*ch>=0x80)return true;
    return false;
}
inline HFont LabelFont(float height)
{
    if(!g_pMenu)return 0;
    const int pixels=(std::max)(4,int(std::floor(height*LabelScale()+0.5f)));
    auto &fonts=detail::Fonts();const auto found=fonts.find(pixels);
    // WinAPI fonts add six pixels of atlas padding to GetFontTall(). The
    // requested size is the cache key; comparing it to that padded height
    // would rebuild every glyph atlas on every label of every frame.
    if(found!=fonts.end())return found->second;
    CFontBuilder builder("Microsoft YaHei UI",pixels,500);
    return fonts[pixels]=g_pMenu->BuildFont(builder);
}
inline float FixedLabelWidth(const char *text,float height)
{
    const HFont font=LabelFont(height);if(font<=0||!text)return 0;
    int wide=0;g_pMenu->GetTextSize(font,text,&wide);return wide/LabelScale();
}
inline void FixedLabel(const char *input,float x,float y,float height,match_ui::Color color,bool rightAlign=false,float maxWidth=0)
{
    const HFont font=LabelFont(height);if(font<=0||!input||color.a<=0)return;
    const float scale=LabelScale();std::string text=input;
    int width=0;g_pMenu->GetTextSize(font,text.c_str(),&width);
    if(maxWidth>0&&width>maxWidth*scale)
    {
        const int available=int(maxWidth*scale);const char *ellipsis=u8"…";int suffix=0;
        g_pMenu->GetTextSize(font,ellipsis,&suffix);
        while(!text.empty()&&width+suffix>available)
        {
            size_t end=text.size()-1;while(end>0&&(static_cast<unsigned char>(text[end])&0xc0)==0x80)--end;
            text.resize(end);g_pMenu->GetTextSize(font,text.c_str(),&width);
        }
        if(suffix<=available){text+=ellipsis;width+=suffix;}else{text.clear();width=0;}
    }
    int drawX=int(std::floor(x*scale+0.5f));if(rightAlign)drawX-=width;
    const int drawY=int(std::floor(y*scale+0.5f)),tall=g_pMenu->GetFontTall(font);
    const auto channel=[](int value){return unsigned(value<0?0:value>255?255:value);};
    const unsigned rgba=(channel(color.a)<<24)|(channel(color.r)<<16)|(channel(color.g)<<8)|channel(color.b);
    utfstate_t state={0};
    for(unsigned char byte:text)
    {
        const uint32_t ch=Q_DecodeUTF8(&state,byte);
        if(ch)drawX+=g_pMenu->DrawCharacter(font,int(ch),drawX,drawY,tall,rgba);
    }
}
}
