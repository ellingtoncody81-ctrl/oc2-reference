#include "PanoramaPlay.h"
#include "keydefs.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace
{
unsigned Ink(){return PackRGBA(235,239,242,255);}
unsigned Muted(){return PackRGBA(164,174,183,255);}
unsigned AccentColor(){return PackRGBA(115,180,217,255);}
Point Pixel(CMenuBaseItem &item,int x,int y){return item.GetRenderPosition()+Point(x,y).Scale();}
void Fill(CMenuBaseItem &item,int x,int y,int w,int h,unsigned c)
{ UI_FillRect(Pixel(item,x,y),Size(w,h).Scale(),c); }
void Text(CMenuBaseItem &item,int x,int y,int w,int h,const char *s,int height,unsigned c,bool bold=false,unsigned align=QM_LEFT)
{ UI_DrawString(bold?uiStatic.hBoldFont:uiStatic.hDefaultFont,Pixel(item,x,y),Size(w,h).Scale(),s,c,Q_max(1,int(height*uiStatic.scaleY)),align,ETF_FORCECOL|ETF_NO_WRAP); }
const char *Difficulties[]={"无机器人","无害机器人（静止）","简单机器人","中等机器人","困难机器人","专家机器人"};
const char *Title(const std::string &name)
{
    struct Named { const char *name,*title; };
    static const Named titles[]={
        {"de_dust2","炙热沙城 II"},{"de_dust","炙热沙城"},{"de_inferno","炼狱小镇"},
        {"de_nuke","核子危机"},{"de_train","列车停放站"},{"de_aztec","阿兹特克"},
        {"de_cbble","古堡激战"},{"de_vertigo","殒命大厦"},{"cs_office","办公室"},
        {"cs_italy","意大利"},{"cs_assault","仓库突击"},{"cs_militia","乡村别墅"},
        {"cs_747","747 客机"},{"cs_estate","庄园"},{"cs_havana","哈瓦那"},
        {"cs_siege","围困之城"},{"cs_backalley","后巷"},{"de_airstrip","机场"},
        {"de_chateau","古堡"},{"de_piranesi","皮拉内西"},{"de_prodigy","研究所"},
        {"de_storm","暴风"},{"de_survivor","幸存者"},{"de_torn","破败之城"}
    };
    for(const auto &n:titles)if(name==n.name)return n.title;
    return nullptr;
}
}

CMenuPanoramaPlay::CMenuPanoramaPlay():CMenuFramework("CMenuPanoramaPlay"),m_page(0),m_selected(-1),m_difficulty(3),m_drawChrome(nullptr) {}

void CMenuPanoramaPlay::Button(CMenuAction &item,const char *label,const char *tag)
{
    item.SetNameAndStatus(label,nullptr,tag);item.SetCharSize(QM_SMALLFONT);
    item.colorBase=Ink();item.colorFocus=AccentColor();item.SetTextInsetX(16);
    item.eTextAlignment=QM_LEFT;item.bIgnoreColorstring=true;
    item.SetBackground(PackRGBA(255,255,255,12),PackRGBA(255,255,255,26));AddItem(item);
}

void CMenuPanoramaPlay::_Init()
{
    m_chrome.Init(*this,0);
    Button(m_back,"返回","play_back");m_back.onReleased=VoidCb(&CMenuPanoramaPlay::Back);
    Button(m_difficultyButton,"","play_difficulty");m_difficultyButton.onReleased=VoidCb(&CMenuPanoramaPlay::Difficulty);
    Button(m_previous,"上一页","play_previous");m_previous.onReleased=VoidCb(&CMenuPanoramaPlay::Previous);
    Button(m_next,"下一页","play_next");m_next.onReleased=VoidCb(&CMenuPanoramaPlay::Next);
    Button(m_start,"开始游戏","play_start");m_start.onReleased=VoidCb(&CMenuPanoramaPlay::Start);
    m_start.SetBackground(PackRGBA(62,111,55,245),PackRGBA(80,140,68,255));
    for(int i=0;i<8;++i)
    {
        std::snprintf(m_cardTags[i],sizeof(m_cardTags[i]),"play_map_%d",i);
        Button(m_cards[i],"",m_cardTags[i]);
        SET_EVENT_MULTI(m_cards[i].onReleased,{static_cast<CMenuPanoramaPlay*>(pSelf->Parent())->Choose(pSelf);});
    }
}

void CMenuPanoramaPlay::_VidInit()
{
    m_chrome.Layout();
    const int width=uiStatic.width,left=104,right=width-40;
    m_back.SetRect(left,690,100,40);m_difficultyButton.SetRect(right-276,126,276,44);
    m_previous.SetRect(left,638,110,36);m_next.SetRect(left+120,638,110,36);
    m_start.SetRect(right-250,686,250,52);
    const int gap=16,cardW=(right-left-gap*3)/4;
    for(int i=0;i<8;++i)m_cards[i].SetRect(left+(i%4)*(cardW+gap),196+(i/4)*208,cardW,192);
    Refresh();
}

void CMenuPanoramaPlay::ReloadMaps()
{
    std::string previous=m_selected>=0&&m_selected<int(m_maps.size())?m_maps[m_selected].name:"de_dust2";
    m_maps.clear();
    int count=0;char **files=EngFuncs::GetFilesList("maps/*.bsp",&count,true);
    // Copy names before subsequent filesystem calls: the engine owns this list.
    std::vector<std::string> names;
    for(int i=0;files&&i<count;++i)
    {
        std::string name=files[i];const auto slash=name.find_last_of("/\\");
        if(slash!=std::string::npos)name.erase(0,slash+1);
        if(name.size()>4)name.resize(name.size()-4);
        if(name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos)names.push_back(name);
    }
    std::sort(names.begin(),names.end());names.erase(std::unique(names.begin(),names.end()),names.end());
    for(const auto &name:names)
    {
        if(!EngFuncs::IsMapValid(name.c_str()))continue;
        Map map;map.name=name;const char *title=Title(name);map.title=title?title:name;
        const std::string cardImage="gfx/frontend/maps/"+name+".tga";
        if(EngFuncs::FileExists(cardImage.c_str(),true))map.radar=cardImage;
        const char *extensions[]={".bmp",".tga",".png"};
        for(const char *ext:extensions)
        {
            if(!map.radar.empty())break;
            const std::string image="overviews/"+name+ext;
            if(EngFuncs::FileExists(image.c_str(),true)){map.radar=image;break;}
        }
        m_maps.push_back(map);
    }
    m_selected=m_maps.empty()?-1:0;
    for(int i=0;i<int(m_maps.size());++i)if(m_maps[i].name==previous)m_selected=i;
    m_page=m_selected>=0?m_selected/8:0;
}

void CMenuPanoramaPlay::Show()
{
    CMenuFramework::Show();ReloadMaps();Refresh();
    SetCursorToItem(m_selected>=0?static_cast<CMenuBaseItem&>(m_cards[m_selected%8]):static_cast<CMenuBaseItem&>(m_back));
}

void CMenuPanoramaPlay::Refresh()
{
    const int pages=Q_max(1,(int(m_maps.size())+7)/8);m_page=bound(0,m_page,pages-1);
    for(int i=0;i<8;++i){m_cards[i].index=m_page*8+i;m_cards[i].SetVisibility(m_cards[i].index<int(m_maps.size()));}
    m_previous.SetGrayed(m_page==0);m_next.SetGrayed(m_page+1>=pages);
    m_start.SetGrayed(m_selected<0);m_difficultyButton.SetText(Difficulties[m_difficulty]);
}

void CMenuPanoramaPlay::Previous(){if(m_page>0)--m_page;Refresh();}
void CMenuPanoramaPlay::Next(){if((m_page+1)*8<int(m_maps.size()))++m_page;Refresh();}
void CMenuPanoramaPlay::Difficulty(){m_difficulty=(m_difficulty+1)%6;Refresh();}
void CMenuPanoramaPlay::Back(){UI_PanoramaHome();}
void CMenuPanoramaPlay::Choose(CMenuBaseItem *item)
{ for(auto &card:m_cards)if(item==&card&&card.index<int(m_maps.size()))m_selected=card.index; }
void CMenuPanoramaPlay::SelectMap(const char *name)
{ if(!IsVisible())Show();for(int i=0;i<int(m_maps.size());++i)if(m_maps[i].name==name){m_selected=i;m_page=i/8;Refresh();break;} }

bool CMenuPanoramaPlay::KeyDown(int key)
{
    if(UI::Key::IsEscape(key)){Back();return true;}
    if(key==K_MWHEELDOWN||key==K_PGDN){Next();return true;}
    if(key==K_MWHEELUP||key==K_PGUP){Previous();return true;}
    if(ItemAtCursor()==&m_difficultyButton&&(key==K_LEFTARROW||key==K_RIGHTARROW))
    {m_difficulty=(m_difficulty+(key==K_LEFTARROW?5:1))%6;Refresh();return true;}
    return CMenuFramework::KeyDown(key);
}

void CMenuPanoramaPlay::Card::Draw(){static_cast<CMenuPanoramaPlay*>(Parent())->PaintCard(*this);}
void CMenuPanoramaPlay::PaintCard(Card &card)
{
    if(card.index<0||card.index>=int(m_maps.size()))return;
    const Map &map=m_maps[card.index];const Point p=card.GetRenderPosition();const Size z=card.GetRenderSize();
    const bool selected=card.index==m_selected;
    UI_FillRect(p,z,PackRGBA(15,21,26,205));
    if(!map.radar.empty())
    {
        const int side=Q_min(z.w-8,int(154*uiStatic.scaleY));
        UI_DrawPic(p.x+(z.w-side)/2,p.y+int(5*uiStatic.scaleY),side,side,PackRGBA(210,218,224,255),map.radar.c_str());
    }
    else Text(card,12,46,card.size.w-24,66,"暂无雷达底图",17,Muted(),false,QM_CENTER);
    if(card.IsCurrentSelected())UI_FillRect(p,z,PackRGBA(210,230,250,18));
    const unsigned edge=selected?AccentColor():PackRGBA(220,230,240,card.IsCurrentSelected()?125:35);
    UI_FillRect(p,Size(z.w,2),edge);UI_FillRect(p+Point(0,z.h-2),Size(z.w,2),edge);
    UI_FillRect(p,Size(2,z.h),edge);UI_FillRect(p+Point(z.w-2,0),Size(2,z.h),edge);
    Text(card,12,158,card.size.w-24,25,map.title.c_str(),18,selected?AccentColor():Ink(),true);
}

void CMenuPanoramaPlay::Draw()
{
    if(CL_IsActive())return;
    oc2_panorama::DrawBackground();
    Text(*this,104,43,600,48,"开始游戏",34,Ink());
    Text(*this,104,112,500,34,"单人本地对局",23,Ink());
    Text(*this,104,153,620,22,m_difficulty==1?"无害机器人保持静止，不会攻击。":"选择地图和机器人难度，然后开始游戏。",15,Muted());
    Fill(*this,104,99,uiStatic.width-144,1,PackRGBA(255,255,255,35));
    if(m_maps.empty())Text(*this,104,270,700,80,"未找到已安装的有效地图。",24,Ink());
    char info[256];std::snprintf(info,sizeof(info),"%d / %d     %u 张已安装地图",m_page+1,Q_max(1,(int(m_maps.size())+7)/8),unsigned(m_maps.size()));
    Text(*this,350,642,450,28,info,14,Muted());
    if(m_selected>=0)
    {
        std::string selected="已选择："+m_maps[m_selected].title;
        Text(*this,230,696,uiStatic.width-560,30,selected.c_str(),17,Ink());
    }
    m_chrome.Draw();CMenuFramework::Draw();if(m_drawChrome)m_drawChrome(this);
}

void CMenuPanoramaPlay::Start()
{
    if(m_selected<0||m_selected>=int(m_maps.size()))return;
    const std::string name=m_maps[m_selected].name;
    if(!EngFuncs::IsMapValid(name.c_str())){ReloadMaps();Refresh();return;}
    if(EngFuncs::GetCvarFloat("host_serverstate"))EngFuncs::HostEndGame("starting local game");
    EngFuncs::CvarSetValue("deathmatch",1);
    EngFuncs::CvarSetValue("sv_lan",1);EngFuncs::CvarSetValue("sv_nat",0);
    EngFuncs::CvarSetString("sv_password","");
    const char *cfg=EngFuncs::GetCvarString("lservercfgfile");
    if(cfg&&*cfg){char escaped[256];Com_EscapeCommand(escaped,cfg,sizeof(escaped));EngFuncs::ClientCmdF(true,"exec %s\n",escaped);}
    EngFuncs::PlayBackgroundTrack(nullptr,nullptr);
    const int quota=m_difficulty?9:0,difficulty=Q_max(0,m_difficulty-2),zombie=m_difficulty==1;
    // Reapply bot values after map/server cfg processing. The server registers
    // these cvars when its DLL loads; main-menu cvar writes alone lose them.
    EngFuncs::ClientCmdF(false,
        "disconnect;menu_connectionprogress localserver;wait;wait;wait;maxplayers 10;latch;map %s;wait;wait;bot_quota_mode normal;bot_join_after_player 1;bot_mimic 0;bot_zombie %d;bot_difficulty %d;bot_quota %d\n",
        name.c_str(),zombie,difficulty,quota);
}

void CMenuPanoramaPlay::Dump()
{
    Con_Printf("panorama_play: visible=%d installed=%u page=%d selected=%s difficulty=%d bot_quota=%d bot_zombie=%d bot_difficulty=%d\n",int(IsVisible()),unsigned(m_maps.size()),m_page,
        m_selected>=0?m_maps[m_selected].name.c_str():"none",m_difficulty,m_difficulty?9:0,m_difficulty==1,Q_max(0,m_difficulty-2));
    for(const auto &map:m_maps)Con_Printf("panorama_map: name=%s radar=%s installed=1\n",map.name.c_str(),map.radar.empty()?"none":map.radar.c_str());
    for(int i=0;i<ItemCount();++i)
    {
        CMenuBaseItem *item=GetItemByIndex(i);if(!item||!item->szTag)continue;
        const Point p=item->GetRenderPosition();const Size s=item->GetRenderSize();
        Con_Printf("frontend_item: tag=%s x=%d y=%d w=%d h=%d visible=%d enabled=%d focus=%d units=screen_pixels\n",item->szTag,p.x,p.y,s.w,s.h,int(IsVisible()&&item->IsVisible()),int(!(item->iFlags&(QMF_GRAYED|QMF_INACTIVE))),int(item->IsCurrentSelected()));
    }
}
