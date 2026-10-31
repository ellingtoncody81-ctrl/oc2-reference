#pragma once
#include <cstddef>
#include <cstdio>

namespace hostage_hint
{
inline void Format(char *out, size_t capacity, const char *binding)
{
    if (!out || !capacity) return;
    char key[32]={};
    if(binding) for(size_t i=0;binding[i] && i<sizeof(key)-1;++i)
    {
        const unsigned char c=(unsigned char)binding[i];
        if(c<32 || c>126) break;
        key[i]=c>='a' && c<='z' ? char(c-'a'+'A') : char(c);
    }
    if(key[0]) std::snprintf(out,capacity,"按住 [%s] 解救人质",key);
    else std::snprintf(out,capacity,"按住使用键解救人质");
}
}
