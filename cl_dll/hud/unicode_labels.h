#pragma once
#include "hud.h"
#include "match_ui.h"
#include "cl_dll/IGameMenuExports.h"
#include "utflib.h"
#include "hud/text_layout.h"
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
inline int GlyphCount(const char *text)
{
    int count=0;if(text)for(const unsigned char *p=(const unsigned char *)text;*p;++p)if((*p&0xc0)!=0x80)++count;
    return count;
}
inline float SpacedLabelWidth(const char *text,float height,float spacing)
{
    return FixedLabelWidth(text,height)+(std::max)(0,GlyphCount(text)-1)*spacing;
}
inline void SpacedLabel(const char *text,float x,float y,float height,match_ui::Color color,float spacing)
{
    const HFont font=LabelFont(height);if(font<=0||!text||color.a<=0)return;
    const float scale=LabelScale();int drawX=int(std::floor(x*scale+0.5f));
    const int drawY=int(std::floor(y*scale+0.5f)),tall=g_pMenu->GetFontTall(font);
    const auto channel=[](int v){return unsigned(v<0?0:v>255?255:v);};
    const unsigned rgba=(channel(color.a)<<24)|(channel(color.r)<<16)|(channel(color.g)<<8)|channel(color.b);
    const int gap=(std::max)(1,int(std::floor(spacing*scale+0.5f)));
    utfstate_t state={0};
    for(const unsigned char *p=(const unsigned char*)text;*p;++p)
    {
        const uint32_t ch=Q_DecodeUTF8(&state,*p);
        if(ch)drawX+=g_pMenu->DrawCharacter(font,int(ch),drawX,drawY,tall,rgba)+gap;
    }
}
inline float LineHeight(float height)
{
    const HFont font=LabelFont(height);if(font<=0)return height;
    int wide=0,tall=0;g_pMenu->GetTextSize(font,"M",&wide,&tall);
    return (std::max)(height,float(tall)/LabelScale());
}
inline std::vector<std::string> WrapLines(const char *text,float height,float available,bool colors=false)
{
    return hud_text_layout::Wrap(text,available,[height,colors](const char *line){
        const std::string visible=colors?hud_text_layout::VisibleText(line):line;
        return FixedLabelWidth(visible.c_str(),height);
    });
}
inline void FixedLabel(const char *input,float x,float y,float height,match_ui::Color color,bool rightAlign=false,float maxWidth=0)
{
    const HFont font=LabelFont(height);if(font<=0||!input||color.a<=0)return;
    const float scale=LabelScale();std::string text=input;
    int width=0;g_pMenu->GetTextSize(font,text.c_str(),&width);
    if(maxWidth>0&&width>maxWidth*scale)
    {
        const int available=int(maxWidth*scale);const char *ellipsis=u8"\u2026";int suffix=0;
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
