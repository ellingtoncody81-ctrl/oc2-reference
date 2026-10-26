/*
ConnectionProgress.cpp -- connection progress window
Copyright (C) 2017 mittorn

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
#include "extdll_menu.h"
#include "BaseMenu.h"
#include "Utils.h"
#include "ItemsHolder.h"
#include "frontend/PanoramaChrome.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
bool SafeLoadingMap(const std::string &map)
{
    if(map.empty() || map.size() >= 128 || map[0] == '/' || map.find("..") != std::string::npos)
        return false;
    for(unsigned char c:map)
        if(!std::isalnum(c) && c!='_' && c!='-' && c!='/') return false;
    return true;
}

const char *LoadingStageText(const char *stage)
{
    if(!std::strcmp(stage,"server")) return "正在启动服务器";
    if(!std::strcmp(stage,"serverinfo")) return "正在读取地图信息";
    if(!std::strcmp(stage,"download")) return "正在准备地图资源";
    if(!std::strcmp(stage,"resources")) return "正在加载地图资源";
    if(!std::strcmp(stage,"renderer")) return "正在准备游戏画面";
    if(!std::strcmp(stage,"signon")) return "正在进入游戏";
    return "正在连接服务器";
}

std::string LoadingMapImage(const std::string &map, bool &overview)
{
    overview = false;
    if(!SafeLoadingMap(map)) return {};
    const char *aspect = ScreenWidth * 3 > ScreenHeight * 4 ? "_16x9" : "_4x3";
    const std::string shots[] = {"levelshots/"+map+aspect+".tga", "levelshots/"+map+aspect+".bmp",
        "levelshots/"+map+".tga", "levelshots/"+map+".bmp"};
    for(const auto &path:shots)
        if(EngFuncs::FileExists(path.c_str(),false)) return path;

    overview = true;
    const std::string card = "gfx/frontend/maps/"+map+".tga";
    if(EngFuncs::FileExists(card.c_str(),false)) return card;

    // The definition owns its image reference; custom maps need not use <map>.bmp.
    const std::string definition = "overviews/"+map+".txt";
    char *data = reinterpret_cast<char*>(EngFuncs::COM_LoadFile(definition.c_str()));
    if(!data) return {};
    char token[1024];char *cursor=data;std::string image;
    while((cursor=EngFuncs::COM_ParseFile(cursor,token,sizeof(token))) != nullptr)
    {
        if(stricmp(token,"image")) continue;
        cursor=EngFuncs::COM_ParseFile(cursor,token,sizeof(token));
        if(!cursor) break;
        std::string path=token;
        std::replace(path.begin(),path.end(),'\\','/');
        if(path.compare(0,10,"overviews/") || path.find("..")!=std::string::npos || path.find(':')!=std::string::npos)
            continue;
        if(EngFuncs::FileExists(path.c_str(),false)){image=path;break;}
    }
    EngFuncs::COM_FreeFile(data);
    return image;
}
}

class CMenuConnectionProgress : public CMenuBaseWindow
{
public:
    CMenuConnectionProgress():CMenuBaseWindow("ConnectionProgress"),m_overview(false) {}
    void _Init() override {}
    void _VidInit() override
    {
        SetRect(0,0,uiStatic.width,768);
        m_map.clear();m_image.clear();
        CalcPosition();CalcSizes();
    }
    bool DrawAnimation() override { return true; }
    bool KeyDown(int key) override
    {
        if(UI::Key::IsEscape(key))
        {
            Stop();
            EngFuncs::ClientCmd(false,"disconnect\n");
            return true;
        }
        if(UI::Key::IsConsole(key))
        {
            // Keep the loading page and its input destination behind the console.
            EngFuncs::ClientCmd(false,"toggleconsole\n");
            return true;
        }
        return true;
    }
    void Start()
    {
        if(Active()) Show();
    }
    void Stop()
    {
        if(IsVisible()) UI_CloseMenu();
        if(!CL_IsActive()) UI_SetActiveMenu(true);
    }
    bool Active() const
    {
        return EngFuncs::GetCvarFloat("scr_loading_active")!=0;
    }
    void Draw() override
    {
        const int w=ScreenWidth,h=ScreenHeight;
        const float scale=float(h)/768.0f;
        const int margin=Q_max(20,int(42*scale)), footer=Q_max(88,int(116*scale));
        const unsigned ink=PackRGBA(240,243,246,255),muted=PackRGBA(167,182,193,255);
        const unsigned accent=PackRGBA(118,184,218,255);
        UI_FillRect(0,0,w,h,PackRGBA(13,20,27,255));
        const std::string map=EngFuncs::GetCvarString("scr_loading_map");
        if(map!=m_map)
        {
            m_map=map;m_image=LoadingMapImage(map,m_overview);
        }
        const int imageTop=int(72*scale),imageBottom=h-footer-int(18*scale);
        if(!m_image.empty())
        {
            const HIMAGE image=EngFuncs::PIC_Load(m_image.c_str());
            const int iw=EngFuncs::PIC_Width(image),ih=EngFuncs::PIC_Height(image);
            if(image && iw>0 && ih>0)
            {
                // Keep the complete radar visible; photographic levelshots cover the screen.
                const float fit=m_overview?Q_min(float(w-margin*2)/iw,float(imageBottom-imageTop)/ih):
                    Q_max(float(w)/iw,float(h)/ih);
                const int dw=int(iw*fit),dh=int(ih*fit);
                const Point pos((w-dw)/2,m_overview?imageTop+(imageBottom-imageTop-dh)/2:(h-dh)/2);
                UI_DrawPic(pos,Size(dw,dh),PackRGBA(255,255,255,255),m_image.c_str(),QM_DRAWTRANS);
                if(!m_overview) UI_FillRect(0,0,w,h,PackRGBA(9,17,24,48));
            }
        }
        auto text=[&](int x,int y,int width,int height,const char *value,int size,unsigned color,unsigned align=QM_LEFT)
        {
            oc2_panorama::DrawText(Point(x,y),Size(width,height),value,size,color,false,align);
        };
        UI_FillRect(0,0,w,int(72*scale),PackRGBA(9,15,21,215));
        text(margin,int(15*scale),w-margin*2,int(40*scale),"正在加载地图",28,ink);
        if(SafeLoadingMap(map))
            text(margin,int(17*scale),w-margin*2,int(36*scale),map.c_str(),22,muted,QM_RIGHT);
        if(m_image.empty())
            text(margin,imageTop,w-margin*2,imageBottom-imageTop,SafeLoadingMap(map)?"暂无地图图像":"正在获取地图信息",24,muted,QM_CENTER);

        UI_FillRect(0,h-footer,w,footer,PackRGBA(9,15,21,245));
        const char *stage=EngFuncs::GetCvarString("scr_loading_stage");
        const int total=Q_max(0,int(EngFuncs::GetCvarFloat("scr_loading_total")));
        const int current=bound(0,int(EngFuncs::GetCvarFloat("scr_loading_current")),total);
        text(margin,h-footer+int(15*scale),w-margin*2,int(28*scale),LoadingStageText(stage),22,ink);
        char amount[96]={};
        if(total) std::snprintf(amount,sizeof(amount),"%d / %d",current,total);
        text(margin,h-footer+int(15*scale),w-margin*2,int(28*scale),amount,18,muted,QM_RIGHT);
        const int barY=h-footer+int(56*scale),barW=w-2*margin,barH=Q_max(3,int(5*scale));
        UI_FillRect(margin,barY,barW,barH,PackRGBA(65,79,90,255));
        if(total)
        {
            UI_FillRect(margin,barY,int(double(barW)*current/total),barH,accent);
        }
        else
        {
            // A moving marker means unknown work, not an invented completion percentage.
            const int marker=Q_max(1,barW/9);
            const double phase=std::fmod(Q_max(0,uiStatic.realTime)/1600.0,2.0);
            const double position=phase<=1.0?phase:2.0-phase;
            UI_FillRect(margin+int((barW-marker)*position),barY,marker,barH,accent);
        }
        const char *item=EngFuncs::GetCvarString("scr_loading_item");
        text(margin,barY+int(13*scale),Q_max(1,barW-int(260*scale)),int(25*scale),item,14,muted);
        text(margin,barY+int(13*scale),barW,int(25*scale),"Esc 取消    ~ 控制台",14,muted,QM_RIGHT);
    }
private:
    bool m_overview;
    std::string m_map,m_image;
};

ADD_MENU3(menu_connectionprogress,CMenuConnectionProgress,UI_ConnectionProgress_f);

bool UI_DrawConnectionProgress(void)
{
    if(!menu_connectionprogress->Active()) return false;
    menu_connectionprogress->Draw();
    return true;
}

void UI_ConnectionProgress_Disconnect(void) { menu_connectionprogress->Stop(); }
void UI_ConnectionProgress_Connect(const char *) { menu_connectionprogress->Start(); }
void UI_ConnectionProgress_ChangeLevel(void) { menu_connectionprogress->Start(); }
void UI_ConnectionProgress_ParseServerInfo(const char *) {}
void UI_ConnectionProgress_Precache(void) {}
void UI_ConnectionProgress_DownloadEnd(void) {}
void UI_ConnectionProgress_Download(const char *,const char *,int,int,const char *) {}

// Keep the existing menu command used by local-game launchers. Only engine
// lifecycle/progress owns completion; the menu never manufactures a percentage.
void UI_ConnectionProgress_f(void)
{
    const char *command=EngFuncs::CmdArgv(1);
    if(!std::strcmp(command,"disconnect")) menu_connectionprogress->Stop();
    else if(!std::strcmp(command,"localserver") || !std::strcmp(command,"menu") || !std::strcmp(command,"changelevel"))
        menu_connectionprogress->Start();
}
