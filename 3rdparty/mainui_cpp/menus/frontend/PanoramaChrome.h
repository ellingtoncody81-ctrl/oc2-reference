#pragma once
#include "Framework.h"
#include "Action.h"
#include <cmath>

void UI_PanoramaOpenPlay();
void UI_PanoramaOpenInventory();
void UI_PanoramaOpenSettings();
void UI_PanoramaQuit();
void UI_PanoramaHome();

namespace oc2_panorama
{
inline void DrawBackground()
{
    for(int i=0;i<48;++i)
    {
        const float t=float(i)/47.0f;
        const int y=int(ScreenHeight*i/48),next=int(ScreenHeight*(i+1)/48);
        UI_FillRect(0,y,ScreenWidth,next-y,PackRGBA(int(119-78*t),int(141-95*t),int(157-111*t),255));
    }
}
}

class CMenuPanoramaNav : public CMenuAction
{
public:
    int icon=0;bool selected=false;
    void Draw() override
    {
        const Point p=GetRenderPosition();const Size s=GetRenderSize();
        const bool hot=IsCurrentSelected();
        if(hot||selected)UI_FillRect(p,s,PackRGBA(255,255,255,selected?48:24));
        if(selected)UI_FillRect(p,Size(Q_max(2,int(3*uiStatic.scaleX)),s.h),PackRGBA(255,255,255,240));
        const int cx=p.x+s.w/2,cy=p.y+s.h/2;
        const float k=Q_max(1.0f,uiStatic.scaleY);
        const unsigned ink=PackRGBA(250,252,255,255);
        auto rect=[&](float x,float y,float w,float h,unsigned c)
        {UI_FillRect(cx+int(x*k),cy+int(y*k),Q_max(1,int(std::ceil(w*k))),Q_max(1,int(std::ceil(h*k))),c);};
        auto disk=[&](int radius,unsigned c)
        {for(int y=-radius;y<=radius;++y){float x=std::sqrt(float(radius*radius-y*y));rect(-x,float(y),2*x+1,1,c);}};
        if(icon==0)
        {
            disk(17,ink);
            for(int y=-8;y<=8;++y)rect(-5,float(y),float(12-std::abs(y)),1,PackRGBA(113,137,155,255));
        }
        else if(icon==1)
        {
            rect(-14,-8,28,17,ink);rect(-10,-4,21,5,PackRGBA(106,127,143,255));
            rect(-3,0,7,12,PackRGBA(106,127,143,255));rect(10,-8,6,5,ink);
        }
        else if(icon==2)
        {
            for(int a=0;a<8;++a){float f=a*0.78539816f;rect(std::cos(f)*13-3,std::sin(f)*13-3,6,6,ink);}
            disk(12,ink);disk(5,PackRGBA(106,127,143,255));
        }
        else
        {
            for(int a=40;a<=320;a+=4){float f=a*0.0174532925f;rect(std::sin(f)*13-1,-std::cos(f)*13-1,2.5f,2.5f,ink);}
            rect(-1.5f,-17,3,18,ink);
        }
    }
};

class CMenuPanoramaChrome
{
public:
    void Init(CMenuFramework &owner,int active)
    {
        static const char *names[]={"\xE6\xB8\xB8\xE7\x8E\xA9","\xE7\x89\xA9\xE5\x93\x81\xE6\xA0\x8F","\xE8\xAE\xBE\xE7\xBD\xAE","\xE9\x80\x80\xE5\x87\xBA"};
        static const char *tags[]={"nav_play","nav_inventory","nav_settings","nav_exit"};
        void (*callbacks[])()={UI_PanoramaOpenPlay,UI_PanoramaOpenInventory,UI_PanoramaOpenSettings,UI_PanoramaQuit};
        for(int i=0;i<4;++i)
        {
            m_nav[i].icon=i;m_nav[i].selected=i==active;
            m_nav[i].SetNameAndStatus(names[i],names[i],tags[i]);
            m_nav[i].onReleased=callbacks[i];owner.AddItem(m_nav[i]);
        }
    }
    void Layout()
    {
        m_nav[0].SetRect(0,76,64,64);m_nav[1].SetRect(0,145,64,64);
        m_nav[2].SetRect(0,214,64,64);m_nav[3].SetRect(0,696,64,64);
    }
    void Draw()
    {
        UI_FillRect(Point(0,0),Size(64,768).Scale(),PackRGBA(22,29,35,110));
        UI_DrawString(uiStatic.hBoldFont,Point(6,23).Scale(),Size(52,30).Scale(),"OC2",PackRGBA(255,255,255,255),int(16*uiStatic.scaleY),QM_CENTER,ETF_FORCECOL|ETF_NO_WRAP);
    }
private:
    CMenuPanoramaNav m_nav[4];
};
