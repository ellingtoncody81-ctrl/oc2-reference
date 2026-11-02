#pragma once
#include <algorithm>
#include <cmath>
#include <array>
#include <string>
#include "spectator_layout.h"

namespace match_notice
{
struct Area {float x,y,width,height,line;};
inline float Scale(int w,int h){return (std::max)(.25f,(std::min)(w/1920.f,h/1080.f));}
inline float Fade(float age,float duration,float fadeIn=.15f,float fadeOut=.5f)
{
    if(!std::isfinite(age)||age<0||age>=duration)return 0;
    const float t=(std::min)(1.f,(std::min)(age/fadeIn,(duration-age)/fadeOut));
    return t*t*(3-2*t);
}
inline Area Feed(int w,int h,bool observer)
{
    const float s=Scale(w,h),height=(std::max)(12.f,20*s);
    Area a={24*s,h-156*s,(std::min)(680*s,w*.42f),height,height+7*s};
    // The health/money panel scales with screen height, including on 4:3.
    a.y=(std::min)(a.y,h-119.f*(h/1080.f)*1.2f-8*s);
    if(observer)
    {
        const auto layout=spectator_layout::Make(w,h);
        const float edge=layout.margin+layout.rowWidth+20*layout.scale;
        a.width=(std::min)(680*s,w-2*edge);
        a.x=(w-a.width)*.5f;
        a.y=h-200*s;
    }
    return a;
}
struct Banner
{
    std::string text;int priority=0;
    float created=0,start=0,end=0,cutStart=-1,cutAlpha=0;
};
struct BannerQueue
{
    Banner current;std::array<Banner,2> waiting;int count=0;float last=-1;
    std::array<std::string,16> seen;int seenCount=0;
    void Clear(){current={};for(auto &item:waiting)item={};for(auto &key:seen)key.clear();seenCount=0;count=0;last=-1;}
    void Remember(const std::string &key){if(seenCount==16){for(int i=1;i<16;++i)seen[i-1]=seen[i];--seenCount;}seen[seenCount++]=key;}
    void NewRound(float now)
    {
        // The server sends HudRound before this round's stage notices.
        Clear();last=now;
    }
    void Remove(int index){for(int i=index+1;i<count;++i)waiting[i-1]=waiting[i];waiting[--count]={};}
    float Alpha(float now) const
    {
        if(current.text.empty())return 0;
        if(current.cutStart>=0)
        {
            const float t=(std::max)(0.f,(std::min)(1.f,(now-current.cutStart)/.15f));
            return current.cutAlpha*(1-t*t*(3-2*t));
        }
        return Fade(now-current.start,current.end-current.start,.25f,.6f);
    }
    void Update(float now)
    {
        if(last>=0&&now<last)Clear();last=now;
        for(int i=count-1;i>=0;--i)if(now-waiting[i].created>=8)Remove(i);
        if(!current.text.empty()&&now<current.end)return;
        if(count)
        {
            current=waiting[0];Remove(0);current.start=now;current.end=now+4.8f;
        }
        // Retain the last record for diagnostics; Alpha is zero after expiry.
    }
    void Push(const char *text,int priority,float now)
    {
        Update(now);
        if(!text||!*text)return;
        for(int i=0;i<seenCount;++i)if(seen[i]==text)return;
        if(current.priority>=4&&priority<4)return; // remains terminal until NewRound/Clear
        if(now<current.end&&current.text==text)return;
        for(int i=0;i<count;++i)if(waiting[i].text==text||waiting[i].priority>priority)return;
        Banner item;item.text=text;item.priority=priority;item.created=now;
        if(current.text.empty()||now>=current.end)
        {current=item;current.start=now;current.end=now+4.8f;Remember(item.text);return;}
        if(priority>=current.priority)
        {
            for(int i=count-1;i>=0;--i)if(waiting[i].priority<=priority)Remove(i);
            if(current.cutStart<0)
            {
                const float alpha=Alpha(now);
                current.cutStart=now;current.cutAlpha=alpha;current.end=(std::min)(current.end,now+.15f);
            }
        }
        if(count==2)Remove(count-1); // bounded, retain the newest useful candidate
        int at=count;while(at>0&&waiting[at-1].priority<priority){waiting[at]=waiting[at-1];--at;}
        waiting[at]=item;++count;Remember(item.text);
    }
};
}
