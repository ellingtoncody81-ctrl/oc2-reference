#pragma once
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace hud_text_layout
{
inline size_t GlyphBytes(const char *text)
{
    if (!text || !*text) return 0;
    const unsigned char lead=static_cast<unsigned char>(*text);
    size_t bytes=lead>=0xc2 && lead<=0xdf?2:lead>=0xe0 && lead<=0xef?3:lead>=0xf0 && lead<=0xf4?4:1;
    for (size_t i=1;i<bytes;++i)
        if ((static_cast<unsigned char>(text[i])&0xc0)!=0x80) return 1;
    return bytes;
}
inline std::string VisibleText(const char *text)
{
    std::string result;
    for (const char *p=text?text:"";*p;)
    {
        if (static_cast<unsigned char>(*p)>=1 && static_cast<unsigned char>(*p)<=4) { ++p;continue; }
        if (*p=='^' && p[1]>='0' && p[1]<='9') { p+=2;continue; }
        const size_t bytes=GlyphBytes(p);result.append(p,bytes);p+=bytes;
    }
    return result;
}
// Layout and drawing consume the same complete UTF-8 glyphs and font metrics.
// Explicit line breaks remain; a long word/CJK run wraps at a glyph boundary.
template<class Measure>
std::vector<std::string> Wrap(const char *text,float available,Measure measure)
{
    std::vector<std::string> lines;
    std::string line;
    for (const char *p=text?text:"";*p;)
    {
        if (*p=='\r' || *p=='\n')
        {
            const char newline=*p++;
            if (newline=='\r' && *p=='\n') ++p;
            lines.push_back(line);line.clear();continue;
        }
        const size_t bytes=GlyphBytes(p);
        const std::string glyph(p,bytes);p+=bytes;
        bool skipSpace=false;
        while (available>0 && !line.empty() && measure((line+glyph).c_str())>available)
        {
            const size_t space=line.find_last_of(' ');
            if (space!=std::string::npos && space>0)
            {
                lines.push_back(line.substr(0,space));line.erase(0,space+1);
            }
            else { lines.push_back(line);line.clear(); }
            if (line.empty() && glyph==" ") { skipSpace=true;break; }
        }
        if (skipSpace) continue;
        line+=glyph;
    }
    lines.push_back(line);
    return lines;
}
inline void Copy(char *dst,size_t capacity,const char *src)
{
    if (!dst || !capacity) return;
    size_t used=0;
    for (const char *p=src?src:"";*p;)
    {
        const size_t bytes=GlyphBytes(p);
        if (used+bytes>=capacity) break;
        std::memcpy(dst+used,p,bytes);used+=bytes;p+=bytes;
    }
    dst[used]=0;
}
}
