#pragma once
#include "Framework.h"
#include "Action.h"
#include "FontManager.h"
#include <cmath>

void UI_PanoramaOpenPlay();
void UI_PanoramaOpenInventory();
void UI_PanoramaOpenSettings();
void UI_PanoramaQuit();
void UI_PanoramaHome();
void UI_PanoramaPause();

namespace oc2_panorama
{
inline void DrawText(Point position,Size size,const char *text,int height,unsigned color,bool bold=false,unsigned align=QM_LEFT)
{
    const HFont font=height<=12?uiStatic.hTinyFont:height<=18?uiStatic.hSmallerFont:
        height<=22?uiStatic.hSmallFont:height>=34?uiStatic.hBigFont:bold?uiStatic.hBoldFont:uiStatic.hDefaultFont;
    UI_DrawString(font,position,size,text,color,Q_max(1,g_FontMgr->GetFontTall(font)),align,ETF_FORCECOL|ETF_NO_WRAP);
}
inline void CenterButton(CMenuAction &button,EFontSizes font=QM_SMALLFONT)
{
    button.SetCharSize(font);button.SetTextInsetX(0);button.eTextAlignment=QM_CENTER;
    button.bIgnoreColorstring=true;
}
inline void DrawBackground()
{
    if(CL_IsActive())
    {
        UI_FillRect(0,0,ScreenWidth,ScreenHeight,PackRGBA(15,22,29,170));
        return;
    }
    const HIMAGE splash=EngFuncs::PIC_Load("gfx/shell/splash.bmp");
    if(splash)
    {
        const int iw=EngFuncs::PIC_Width(splash),ih=EngFuncs::PIC_Height(splash);
        if(iw>0&&ih>0)
        {
            const float scale=Q_max(float(ScreenWidth)/iw,float(ScreenHeight)/ih);
            const int width=int(std::ceil(iw*scale)),height=int(std::ceil(ih*scale));
            UI_DrawPic(Point((ScreenWidth-width)/2,(ScreenHeight-height)/2),Size(width,height),PackRGBA(255,255,255,255),"gfx/shell/splash.bmp");
            UI_FillRect(0,0,ScreenWidth,ScreenHeight,PackRGBA(14,23,31,118));
            return;
        }
    }
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
        if(icon==8)
        {
            oc2_panorama::DrawText(p+Point(6,23).Scale(),Size(52,30).Scale(),"OC2",22,ink,true,QM_CENTER);
        }
        else if(icon==0)
        {
            disk(17,ink);
            for(int y=-8;y<=8;++y)rect(-5,float(y),float(12-std::abs(y)),1,PackRGBA(113,137,155,255));
        }
        else if(icon==1)
        {
            // Inventory briefcase, with a small pistol inside the outline.
            rect(-6,-16,12,3,ink);rect(-6,-13,3,4,ink);rect(3,-13,3,4,ink);
            rect(-14,-10,28,3,ink);rect(-14,-7,3,22,ink);rect(11,-7,3,22,ink);rect(-14,12,28,3,ink);
            rect(-7,-2,17,4,ink);rect(-7,2,6,8,ink);rect(7,-4,3,3,ink);
        }
        else if(icon==2)
        {
            for(int a=0;a<8;++a){float f=a*0.78539816f;rect(std::cos(f)*13-3,std::sin(f)*13-3,6,6,ink);}
            disk(12,ink);disk(5,PackRGBA(106,127,143,255));
        }
        else if(icon==3)
        {
            for(int a=40;a<=320;a+=4){float f=a*0.0174532925f;rect(std::sin(f)*13-1,-std::cos(f)*13-1,2.5f,2.5f,ink);}
            rect(-1.5f,-17,3,18,ink);
        }
        else if(icon==4)
        {
            for(int a=35;a<325;a+=5){float f=a*.0174532925f;rect(std::cos(f)*13-1,std::sin(f)*13-1,3,3,ink);}
            rect(7,-13,9,3,ink);rect(13,-13,3,10,ink);
        }
        else if(icon==5)
        {
            for(int i=0;i<9;++i)rect(-13+i,-1+i,3,3,ink);
            for(int i=0;i<18;++i)rect(-5+i,7-i,3,3,ink);
        }
        else if(icon==6)
        {
            rect(-10,-5,21,3,ink);rect(9,-4,3,14,ink);
            for(int i=0;i<9;++i){rect(-13+i,-4-i,3,3,ink);rect(-13+i,-4+i,3,3,ink);}
        }
        else
        {
            disk(13,ink);
            const unsigned inner=PackRGBA(65,55,50,255);
            for(int i=-6;i<=6;++i){rect(float(i),float(i),3,3,inner);rect(float(i),float(-i),3,3,inner);}
        }
    }
};

class CMenuPanoramaChrome
{
public:
    void Init(CMenuFramework &owner,int active)
    {
        m_home.icon=8;
        m_home.SetNameAndStatus("大厅",nullptr,"nav_home");
        m_home.onReleased=UI_PanoramaHome;owner.AddItem(m_home);
        static const char *names[]={"\xE6\xB8\xB8\xE7\x8E\xA9","\xE7\x89\xA9\xE5\x93\x81\xE6\xA0\x8F","\xE8\xAE\xBE\xE7\xBD\xAE","\xE9\x80\x80\xE5\x87\xBA"};
        static const char *tags[]={"nav_play","nav_inventory","nav_settings","nav_exit"};
        void (*callbacks[])()={UI_PanoramaOpenPlay,UI_PanoramaOpenInventory,UI_PanoramaOpenSettings,UI_PanoramaQuit};
        for(int i=0;i<4;++i)
        {
            m_nav[i].icon=i;m_nav[i].selected=i==active;
            m_nav[i].SetNameAndStatus(names[i],nullptr,tags[i]);
            m_nav[i].onReleased=callbacks[i];owner.AddItem(m_nav[i]);
        }
    }
    void Layout()
    {
        const bool game=CL_IsActive();
        m_home.SetRect(0,0,64,68);m_home.SetVisibility(m_visible&&!game);
        m_nav[0].icon=game?6:0;m_nav[3].icon=game?7:3;
        m_nav[0].SetRect(0,game?0:76,64,64);m_nav[1].SetRect(0,game?214:145,64,64);
        m_nav[2].SetRect(0,game?283:214,64,64);m_nav[3].SetRect(0,696,64,64);
    }
    void SetVisible(bool visible){m_visible=visible;m_home.SetVisibility(visible&&!CL_IsActive());for(auto &nav:m_nav)nav.SetVisibility(visible);}
    void Draw()
    {
        m_home.SetVisibility(m_visible&&!CL_IsActive());
        UI_FillRect(Point(0,0),Size(64,768).Scale(),PackRGBA(22,29,35,110));
    }
private:
    bool m_visible=true;
    CMenuPanoramaNav m_home,m_nav[4];
};
