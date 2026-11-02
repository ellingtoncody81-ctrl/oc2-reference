#include "PanoramaSettings.h"
#include "keydefs.h"
#include "../../../../common/oc2_crosshair.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace
{
unsigned Ink(){return PackRGBA(235,239,242,255);}
unsigned Muted(){return PackRGBA(166,176,185,255);}
unsigned AccentColor(){return PackRGBA(115,180,217,255);}
Point Pixel(CMenuBaseItem &item,int x,int y){return item.GetRenderPosition()+Point(x,y).Scale();}
void Fill(CMenuBaseItem &item,int x,int y,int w,int h,unsigned c)
{UI_FillRect(Pixel(item,x,y),Size(w,h).Scale(),c);}
void Text(CMenuBaseItem &item,int x,int y,int w,int h,const char *s,int height,unsigned c,bool bold=false,unsigned align=QM_LEFT)
{oc2_panorama::DrawText(Pixel(item,x,y),Size(w,h).Scale(),s,height,c,bold,align);}
const char *KeyLabel(const char *key)
{
    if(!key||!*key)return "未知按键";
    static const struct {const char *key,*label;} names[]={
        {"MOUSE1","鼠标左键"},{"MOUSE2","鼠标右键"},{"MOUSE3","鼠标中键"},
        {"MOUSE4","鼠标侧键一"},{"MOUSE5","鼠标侧键二"},
        {"MWHEELUP","滚轮向上"},{"MWHEELDOWN","滚轮向下"},{"SPACE","空格"},
        {"TAB","制表键"},{"ENTER","回车"},{"BACKSPACE","退格"},{"PAUSE","暂停键"},
        {"UPARROW","上方向键"},{"DOWNARROW","下方向键"},{"LEFTARROW","左方向键"},{"RIGHTARROW","右方向键"},
        {"INS","插入键"},{"DEL","删除键"},{"PGDN","下一页键"},{"PGUP","上一页键"},
        {"HOME","起始键"},{"END","结束键"},{"CAPSLOCK","大写锁定"},{"NUMLOCK","数字锁定"}
    };
    for(const auto &name:names)if(!stricmp(name.key,key))return name.label;
    return key;
}
std::vector<std::string> Split(const char *s)
{
    std::vector<std::string> parts;if(!s)return parts;
    const char *start=s;for(;;++s)if(*s=='|'||!*s){parts.emplace_back(start,s-start);if(!*s)break;start=s+1;}
    return parts;
}
const char *Categories[]={"键盘 / 鼠标","游戏设置","音频设置","视频设置","自定义准星"};
bool ValidPlayerName(const std::string &name)
{
    if(name.empty()||name.size()>31)return false;
    bool visible=false;
    for(size_t i=0;i<name.size();)
    {
        const unsigned char c=name[i++];
        if(c<32||c==127||c=='"'||c=='\\'||c==';'||c=='%')return false;
        if(c>32)visible=true;
        if(c<128)continue;
        int extra=c>=0xc2&&c<=0xdf?1:c>=0xe0&&c<=0xef?2:c>=0xf0&&c<=0xf4?3:-1;
        if(extra<0||i+extra>name.size())return false;
        unsigned code=c&((1u<<(6-extra))-1);
        for(int n=0;n<extra;++n){unsigned char next=name[i++];if((next&0xc0)!=0x80)return false;code=(code<<6)|(next&63);}
        if((extra==1&&code<0x80)||(extra==2&&code<0x800)||(extra==3&&code<0x10000)||code>0x10ffff||(code>=0xd800&&code<=0xdfff))return false;
    }
    return visible;
}
}

// These are registration defaults, never read back from config.cfg as defaults.
// The user sees the live engine values; unavailable client cvars stay disabled.
const CMenuPanoramaSettings::Setting *CMenuPanoramaSettings::Definitions(int &count)
{
    static const Setting settings[]={
        {0,Number,"鼠标灵敏度","sensitivity","3",0.1f,20,0.1f,nullptr,nullptr},
        {0,Pitch,"反转鼠标垂直方向","m_pitch","0.022",0,1,1,"关闭|开启",nullptr},
        {0,Choice,"鼠标平滑","look_filter","0",0,1,1,"关闭|开启","0|1"},
        {0,Binding,"向前移动","+forward","w",0,0,0,nullptr,nullptr},
        {0,Binding,"向后移动","+back","s",0,0,0,nullptr,nullptr},
        {0,Binding,"向左移动","+moveleft","a",0,0,0,nullptr,nullptr},
        {0,Binding,"向右移动","+moveright","d",0,0,0,nullptr,nullptr},
        {0,Binding,"跳跃","+jump","SPACE",0,0,0,nullptr,nullptr},
        {0,Binding,"蹲下","+duck","SHIFT",0,0,0,nullptr,nullptr},
        {0,Binding,"静步行走","+speed","MOUSE4",0,0,0,nullptr,nullptr},
        {0,Binding,"使用","+use","e",0,0,0,nullptr,nullptr},
        {0,Binding,"开火","+attack","MOUSE1",0,0,0,nullptr,nullptr},
        {0,Binding,"第二攻击 / 开镜","+attack2","MOUSE2",0,0,0,nullptr,nullptr},
        {0,Binding,"装填弹药","+reload","r",0,0,0,nullptr,nullptr},
        {0,Binding,"检视武器","+lookatweapon","f",0,0,0,nullptr,nullptr},
        {0,Binding,"机械瞄准","ironsight","MOUSE3",0,0,0,nullptr,nullptr},
        {0,Binding,"丢弃武器","drop","g",0,0,0,nullptr,nullptr},
        {0,Binding,"上一个武器","invprev","MWHEELUP",0,0,0,nullptr,nullptr},
        {0,Binding,"下一个武器","invnext","MWHEELDOWN",0,0,0,nullptr,nullptr},
        {0,Binding,"切换至上次武器","lastinv","q",0,0,0,nullptr,nullptr},
        {0,Binding,"购买菜单","buy","b",0,0,0,nullptr,nullptr},
        {0,Binding,"主武器","slot1","1",0,0,0,nullptr,nullptr},
        {0,Binding,"副武器","slot2","2",0,0,0,nullptr,nullptr},
        {0,Binding,"匕首","slot3","3",0,0,0,nullptr,nullptr},
        {0,Binding,"投掷物","slot4","4",0,0,0,nullptr,nullptr},
        {0,Binding,"C4 炸弹","slot5","5",0,0,0,nullptr,nullptr},
        {0,Binding,"计分板","+showscores","TAB",0,0,0,nullptr,nullptr},
        {0,Binding,"全体聊天","messagemode","y",0,0,0,nullptr,nullptr},
        {0,Binding,"队伍聊天","messagemode2","u",0,0,0,nullptr,nullptr},
        {0,Binding,"语音通话","+voicerecord","k",0,0,0,nullptr,nullptr},
        {0,Binding,"选择队伍","chooseteam","m",0,0,0,nullptr,nullptr},
        {1,PlayerName,"玩家 ID / 昵称","name","",0,0,0,nullptr,nullptr},
        {1,Choice,"快速切换武器","hud_fastswitch","0",0,1,1,"关闭|开启","0|1"},
        {1,Choice,"持枪手","cl_righthand","1",0,1,1,"左手|右手","0|1"},
        {1,Number,"持枪视野","viewmodel_fov","54",54,68,1,nullptr,nullptr},
        {1,Number,"持枪位置：左右","viewmodel_offset_x","0",-2,2.5f,0.1f,nullptr,nullptr},
        {1,Number,"持枪位置：前后","viewmodel_offset_y","0",-2,2,0.1f,nullptr,nullptr},
        {1,Number,"持枪位置：上下","viewmodel_offset_z","0",-2,2,0.1f,nullptr,nullptr},
        {4,Choice,"显示准星","crosshair","1",0,1,1,"关闭|开启","0|1"},
        {4,Choice,"准星样式","cl_crosshairstyle","2",0,5,1,"默认动态|默认静态|经典分离动态|经典动态|经典静态|经典射击反馈","0|1|2|3|4|5"},
        {4,Number,"线段长度","cl_crosshairsize","5",0,15,.5f,nullptr,nullptr},
        {4,Number,"线段粗细","cl_crosshairthickness","0.5",0,5,.1f,nullptr,nullptr},
        {4,Number,"准星间距","cl_crosshairgap","1",-15,20,.5f,nullptr,nullptr},
        {4,Choice,"中心点","cl_crosshairdot","1",0,1,1,"关闭|开启","0|1"},
        {4,Choice,"T 形准星","cl_crosshair_t","0",0,1,1,"关闭|开启","0|1"},
        {4,Choice,"颜色","cl_crosshaircolor","1",0,5,1,"红|绿|黄|蓝|青|自定义","0|1|2|3|4|5"},
        {4,Number,"自定义：红","cl_crosshaircolor_r","50",0,255,1,nullptr,nullptr},
        {4,Number,"自定义：绿","cl_crosshaircolor_g","250",0,255,1,nullptr,nullptr},
        {4,Number,"自定义：蓝","cl_crosshaircolor_b","50",0,255,1,nullptr,nullptr},
        {4,Number,"不透明度","cl_crosshairalpha","200",0,255,1,nullptr,nullptr},
        {4,Choice,"透明混合","cl_crosshairusealpha","1",0,1,1,"加法高亮|透明混合","0|1"},
        {4,Choice,"黑色描边","cl_crosshair_drawoutline","1",0,1,1,"关闭|开启","0|1"},
        {4,Number,"描边粗细","cl_crosshair_outlinethickness","1",.1f,3,.1f,nullptr,nullptr},
        {4,Choice,"使用武器基础间距","cl_crosshairgap_useweaponvalue","0",0,1,1,"关闭|开启","0|1"},
        {4,Number,"动态分离距离","cl_crosshair_dynamic_splitdist","7",0,30,.5f,nullptr,nullptr},
        {4,Number,"分离内段不透明度","cl_crosshair_dynamic_splitalpha_innermod","1",0,1,.05f,nullptr,nullptr},
        {4,Number,"分离外段不透明度","cl_crosshair_dynamic_splitalpha_outermod","0.5",.3f,1,.05f,nullptr,nullptr},
        {4,Number,"分离外段长度比例","cl_crosshair_dynamic_maxdist_splitratio","0.35",0,1,.05f,nullptr,nullptr},
        {4,Number,"默认静态间距","cl_fixedcrosshairgap","3",-5,20,.5f,nullptr,nullptr},
        {1,Choice,"允许下载服务器资源","cl_allowdownload","1",0,1,1,"关闭|开启","0|1"},
        {1,Choice,"显示天气效果","cl_weather","1",0,1,1,"关闭|开启","0|1"},
        {1,Choice,"简化玩家模型","cl_minmodels","0",0,1,1,"关闭|开启","0|1"},
        {2,Number,"主音量","volume","0.7",0,1,0.05f,nullptr,nullptr},
        {2,Number,"音乐音量","MP3Volume","1.0",0,1,0.05f,nullptr,nullptr},
        {2,Choice,"失去焦点时静音","snd_mute_losefocus","1",0,1,1,"关闭|开启","0|1"},
        {2,Choice,"环境音效处理","room_off","0",0,1,1,"开启|关闭","0|1"},
        {2,Choice,"环境音效算法","dsp_coeff_table","0",0,1,1,"关闭|开启","0|1"},
        {3,Resolution,"分辨率与显示模式","","",0,0,0,nullptr,nullptr},
        {3,Choice,"垂直同步","gl_vsync","1",0,1,1,"关闭|开启","0|1"},
        {3,Number,"最高帧率","fps_max","72",30,500,10,nullptr,nullptr},
        {3,Number,"伽马","gamma","2.5",1.8f,3,0.05f,nullptr,nullptr},
        {3,Number,"亮度","brightness","0",0,3,0.1f,nullptr,nullptr},
        {3,Choice,"细节纹理","r_detailtextures","1",0,1,1,"关闭|开启","0|1"},
        {3,Choice,"纹理过滤","gl_texture_nearest","0",0,1,1,"开启|关闭","0|1"},
        {3,Number,"各向异性过滤","gl_anisotropy","8",1,16,1,nullptr,nullptr},
        {3,Choice,"水面波纹","r_ripple","0",0,1,1,"关闭|开启","0|1"},
        {3,Choice,"高亮渲染","gl_overbright","1",0,1,1,"关闭|开启","0|1"}
    };
    count=sizeof(settings)/sizeof(settings[0]);return settings;
}

CMenuPanoramaSettings::CMenuPanoramaSettings():CMenuFramework("CMenuPanoramaSettings"),m_category(0),m_scroll(0),m_capture(-1),m_captureKey(-1),m_dirty(false),m_drawChrome(nullptr),m_backCallback(nullptr)
{for(bool &b:m_touchedBindings)b=false;}

void CMenuPanoramaSettings::Button(CMenuAction &item,const char *label,const char *tag)
{
    item.SetNameAndStatus(label,nullptr,tag);oc2_panorama::CenterButton(item);
    item.colorBase=Ink();item.colorFocus=AccentColor();item.bIgnoreColorstring=true;
    item.SetBackground(PackRGBA(255,255,255,8),PackRGBA(255,255,255,22));AddItem(item);
}

void CMenuPanoramaSettings::_Init()
{
    m_chrome.Init(*this,2);
    const char *tags[]={"settings_keyboard","settings_game","settings_audio","settings_video","settings_crosshair"};
    for(int i=0;i<5;++i)
    {
        Button(m_tabs[i],Categories[i],tags[i]);
        SET_EVENT_MULTI(m_tabs[i].onReleased,{static_cast<CMenuPanoramaSettings*>(pSelf->Parent())->ChooseTab(pSelf);});
    }
    Button(m_back,"返回","settings_back");m_back.onReleased=VoidCb(&CMenuPanoramaSettings::Back);
    Button(m_save,"保存更改","settings_save");m_save.onReleased=VoidCb(&CMenuPanoramaSettings::Save);
    Button(m_revert,"撤销本次更改","settings_revert");m_revert.onReleased=VoidCb(&CMenuPanoramaSettings::Revert);
    Button(m_crosshairDefaults,"恢复准星默认","crosshair_defaults");m_crosshairDefaults.onReleased=VoidCb(&CMenuPanoramaSettings::CrosshairDefaults);
    const char *demoNames[]={"静止","跑动","射击"},*demoTags[]={"crosshair_demo_0","crosshair_demo_1","crosshair_demo_2"};
    for(int i=0;i<3;++i){Button(m_crosshairDemo[i],demoNames[i],demoTags[i]);SET_EVENT_MULTI(m_crosshairDemo[i].onReleased,{static_cast<CMenuPanoramaSettings*>(pSelf->Parent())->CrosshairDemo(pSelf);});}
    Button(m_up,"上移","settings_up");SET_EVENT_MULTI(m_up.onReleased,{static_cast<CMenuPanoramaSettings*>(pSelf->Parent())->Scroll(-1);});
    Button(m_down,"下移","settings_down");SET_EVENT_MULTI(m_down.onReleased,{static_cast<CMenuPanoramaSettings*>(pSelf->Parent())->Scroll(1);});
    for(int i=0;i<9;++i)
    {
        std::snprintf(m_rowTags[i],sizeof(m_rowTags[i]),"settings_row_%d",i);
        Button(m_rows[i],"",m_rowTags[i]);
        SET_EVENT_MULTI(m_rows[i].onReleased,{static_cast<CMenuPanoramaSettings*>(pSelf->Parent())->Activate(pSelf);});
    }
    Button(m_displayMode,"","settings_display_resolution");
    SET_EVENT_MULTI(m_displayMode.onReleased,{static_cast<CMenuPanoramaSettings*>(pSelf->Parent())->CycleDisplayMode();});
    Button(m_displayWindow,"","settings_display_window");
    SET_EVENT_MULTI(m_displayWindow.onReleased,{static_cast<CMenuPanoramaSettings*>(pSelf->Parent())->CycleDisplayWindow();});
    Button(m_displayApply,"应用显示设置","settings_display_apply");m_displayApply.onReleased=VoidCb(&CMenuPanoramaSettings::ApplyDisplay);
    Button(m_displayCancel,"取消","settings_display_cancel");m_displayCancel.onReleased=VoidCb(&CMenuPanoramaSettings::CancelDisplay);
    m_playerName.SetNameAndStatus("",nullptr,"settings_player_name");
    m_playerName.iMaxLength=128;m_playerName.bAllowColorstrings=false;
    m_playerName.SetCharSize(QM_SMALLFONT);m_playerName.colorBase=Ink();
    m_playerName.onCvarChange=VoidCb(&CMenuPanoramaSettings::NameChanged);AddItem(m_playerName);
}

void CMenuPanoramaSettings::_VidInit()
{
    m_chrome.Layout();
    const int width=uiStatic.width,left=104,right=width-50;
    const int tabWidth=(right-left)/5;
    for(int i=0;i<5;++i)m_tabs[i].SetRect(left+i*tabWidth,112,tabWidth-4,46);
    for(int i=0;i<9;++i)m_rows[i].SetRect(left,185+i*51,right-left-20,48);
    m_up.SetRect(right-115,650,55,30);m_down.SetRect(right-55,650,55,30);
    m_back.SetRect(left,696,100,42);m_save.SetRect(right-190,690,190,48);
    m_revert.SetRect(right-400,690,195,48);Refresh();
    m_crosshairDefaults.SetRect(right-310,590,292,40);
    for(int i=0;i<3;++i)m_crosshairDemo[i].SetRect(right-310+i*99,503,94,38);
    const int displayLeft=(width-700)/2;
    m_displayMode.SetRect(displayLeft,260,700,54);m_displayWindow.SetRect(displayLeft,330,700,54);
    m_displayApply.SetRect(displayLeft+365,450,335,50);m_displayCancel.SetRect(displayLeft,450,335,50);
}

void CMenuPanoramaSettings::Show()
{
    CMenuFramework::Show();m_original.clear();m_dirty=false;m_capture=-1;m_captureKey=-1;
    m_nameLoading=true;m_playerName.SetBuffer(EngFuncs::GetCvarString("name"));m_nameLoading=false;m_nameInvalid=false;
    for(int key=0;key<256;++key){const char *b=EngFuncs::KEY_GetBinding(key);m_originalBindings[key]=b?b:"";m_touchedBindings[key]=false;}
    Refresh();SetCursorToItem(m_tabs[m_category]);
}
void CMenuPanoramaSettings::Hide()
{
    if(m_displayOpen)CancelDisplay();
    Revert();m_capture=-1;m_captureKey=-1;for(auto &row:m_rows)row.dragging=false;
    CMenuFramework::Hide();
}

void CMenuPanoramaSettings::SelectCategory(int category)
{m_category=bound(0,category,4);m_scroll=0;m_capture=-1;Refresh();}
void CMenuPanoramaSettings::ChooseTab(CMenuBaseItem *item)
{for(int i=0;i<5;++i)if(item==&m_tabs[i])SelectCategory(i);}
void CMenuPanoramaSettings::Refresh()
{
    int count;const Setting *settings=Definitions(count);m_visible.clear();
    m_playerName.SetVisibility(false);
    for(int i=0;i<count;++i)if(settings[i].category==m_category)m_visible.push_back(i);
    m_scroll=bound(0,m_scroll,Q_max(0,int(m_visible.size())-9));
    for(int i=0;i<9;++i)
    {
        const int row=m_scroll+i;m_rows[i].index=row<int(m_visible.size())?m_visible[row]:-1;
        if(m_category==4)m_rows[i].SetRect(122,222+i*46,uiStatic.width-540,44);
        else m_rows[i].SetRect(104,185+i*51,uiStatic.width-174,48);
        m_rows[i].VidInit();
        m_rows[i].SetVisibility(!m_displayOpen&&m_rows[i].index>=0);m_rows[i].SetGrayed(m_rows[i].index>=0&&!Available(settings[m_rows[i].index]));
        if(m_rows[i].index>=0&&settings[m_rows[i].index].kind==PlayerName)
        {m_playerName.SetRect(uiStatic.width-400,191+i*51,310,34);m_playerName.VidInit();m_playerName.SetVisibility(!m_displayOpen);}
    }
    for(int i=0;i<5;++i){m_tabs[i].colorBase=i==m_category?AccentColor():Ink();m_tabs[i].SetVisibility(!m_displayOpen);}
    m_crosshairDefaults.SetVisibility(m_category==4&&!m_displayOpen);
    for(int i=0;i<3;++i){m_crosshairDemo[i].SetVisibility(m_category==4&&!m_displayOpen);m_crosshairDemo[i].colorBase=i==m_crosshairDemoMode?AccentColor():Muted();m_crosshairDemo[i].SetBackground(PackRGBA(115,180,217,i==m_crosshairDemoMode?38:10),PackRGBA(115,180,217,55));}
    m_back.SetVisibility(!m_displayOpen);m_save.SetVisibility(!m_displayOpen);m_revert.SetVisibility(!m_displayOpen);
    m_up.SetGrayed(m_scroll==0);m_down.SetGrayed(m_scroll+9>=int(m_visible.size()));
    m_up.SetVisibility(!m_displayOpen&&m_visible.size()>9);m_down.SetVisibility(!m_displayOpen&&m_visible.size()>9);
    m_save.SetGrayed(!m_dirty);m_revert.SetGrayed(!m_dirty);
    m_chrome.SetVisible(!CL_IsActive()&&!m_displayOpen);RefreshDisplay();
}
void CMenuPanoramaSettings::Scroll(int rows)
{m_scroll+=rows;Refresh();}

bool CMenuPanoramaSettings::Available(const Setting &setting) const
{
    if(setting.category==4)
    {
        const auto config=oc2_crosshair::ReadConfig([](const char *key){return EngFuncs::GetCvarFloat(key);});
        const int style=config.style;
        const std::string key=setting.key;
        if(key.find("cl_crosshair_dynamic_")==0&&style!=2)return false;
        if(key=="cl_fixedcrosshairgap"&&style!=1)return false;
        if(key.find("cl_crosshaircolor_")==0&&config.color!=5)return false;
        if(style<2&&(key=="cl_crosshairsize"||key=="cl_crosshairthickness"||key=="cl_crosshairgap"||key=="cl_crosshairdot"||key=="cl_crosshair_t"||key=="cl_crosshairgap_useweaponvalue"))return false;
    }
    if(setting.kind==Binding||setting.kind==Resolution)return true;
    const char *value=EngFuncs::GetCvarString(setting.key);
    return value&&*value&&(!EngFuncs::textfuncs.pfnIsCvarReadOnly||EngFuncs::textfuncs.pfnIsCvarReadOnly(setting.key)<=0);
}

std::string CMenuPanoramaSettings::Value(const Setting &setting) const
{
    if(setting.kind==PlayerName)return EngFuncs::GetCvarString("name");
    if(setting.kind==Binding)
    {
        std::string result;
        for(int key=0;key<256;++key)
        {
            const char *bind=EngFuncs::KEY_GetBinding(key);
            if(bind&&!std::strcmp(bind,setting.key))
            {if(!result.empty())result+=" / ";result+=KeyLabel(EngFuncs::KeynumToString(key));}
        }
        return result.empty()?"未绑定":result;
    }
    if(setting.kind==Resolution)
    {
        char text[96];int mode=int(EngFuncs::GetCvarFloat("fullscreen"));
        std::snprintf(text,sizeof(text),"%d × %d  %s  ›",int(ScreenWidth),int(ScreenHeight),mode==0?"窗口":mode==2?"无边框":"全屏");return text;
    }
    if(!Available(setting))return "当前不可用";
    if(setting.kind==Pitch)return EngFuncs::GetCvarFloat(setting.key)<0?"开启":"关闭";
    if(setting.kind==Choice)
    {
        const auto labels=Split(setting.choices),values=Split(setting.values);const char *value=EngFuncs::GetCvarString(setting.key);
        for(unsigned i=0;i<values.size();++i)if(values[i]==value||(!values[i].empty()&&values[i][0]>='0'&&values[i][0]<='9'&&std::atof(values[i].c_str())==EngFuncs::GetCvarFloat(setting.key)))return labels[i];
        return std::string("自定义 ")+value;
    }
    char number[48];std::snprintf(number,sizeof(number),"%g",double(EngFuncs::GetCvarFloat(setting.key)));return number;
}

void CMenuPanoramaSettings::Remember(const char *name)
{
    for(const auto &value:m_original)if(value.name==name)return;
    m_original.push_back({name,EngFuncs::GetCvarString(name)});
}
void CMenuPanoramaSettings::Adjust(int index,int direction)
{
    int count;const Setting *settings=Definitions(count);if(index<0||index>=count)return;
    const Setting &setting=settings[index];if(!Available(setting))return;
    if(setting.kind==PlayerName){SetCursorToItem(m_playerName);return;}
    if(setting.kind==Binding){m_capture=index;m_captureKey=-1;return;}
    if(setting.kind==Resolution){OpenDisplay();return;}
    Remember(setting.key);
    if(setting.kind==Number)
    {
        float value=EngFuncs::GetCvarFloat(setting.key)+setting.step*direction;
        value=bound(setting.minimum,value,setting.maximum);EngFuncs::CvarSetValue(setting.key,value);
    }
    else if(setting.kind==Pitch)
    {
        float value=EngFuncs::GetCvarFloat(setting.key);EngFuncs::CvarSetValue(setting.key,value==0?-0.022f:-value);
    }
    else
    {
        const auto values=Split(setting.values);const std::string current=EngFuncs::GetCvarString(setting.key);int selected=-1;
        for(int i=0;i<int(values.size());++i)if(values[i]==current||((values[i][0]>='0'&&values[i][0]<='9')&&std::atof(values[i].c_str())==EngFuncs::GetCvarFloat(setting.key)))selected=i;
        selected=(selected+direction+int(values.size()))%int(values.size());EngFuncs::CvarSetString(setting.key,values[selected].c_str());
    }
    m_dirty=true;Refresh();
}
void CMenuPanoramaSettings::Activate(CMenuBaseItem *item)
{for(auto &row:m_rows)if(item==&row)Adjust(row.index,1);}

bool CMenuPanoramaSettings::Row::KeyDown(int key)
{
    auto *menu=static_cast<CMenuPanoramaSettings*>(Parent());int count;const Setting *settings=Definitions(count);
    if(index>=0&&key==K_MOUSE1&&settings[index].kind==Number&&UI_CursorInRect(GetRenderPosition(),GetRenderSize()))
    {
        const Point p=GetRenderPosition();const Size z=GetRenderSize();
        if(uiStatic.cursorX>=p.x+z.w-int(280*uiStatic.scaleX))
        {dragging=true;menu->SliderAt(*this,uiStatic.cursorX);return true;}
    }
    if(key==K_LEFTARROW||key==K_RIGHTARROW){menu->Adjust(index,key==K_LEFTARROW?-1:1);return true;}
    return CMenuAction::KeyDown(key);
}
bool CMenuPanoramaSettings::Row::KeyUp(int key)
{if(key==K_MOUSE1&&dragging){dragging=false;return true;}return CMenuAction::KeyUp(key);}
bool CMenuPanoramaSettings::Row::MouseMove(int x,int y)
{if(dragging)static_cast<CMenuPanoramaSettings*>(Parent())->SliderAt(*this,x);return true;}
void CMenuPanoramaSettings::SliderAt(Row &row,int x)
{
    int count;const Setting *settings=Definitions(count);if(row.index<0)return;const Setting &setting=settings[row.index];
    if(!Available(setting))return;const Point p=row.GetRenderPosition();const Size z=row.GetRenderSize();
    const int start=p.x+z.w-int(272*uiStatic.scaleY),end=p.x+z.w-int(60*uiStatic.scaleY);
    const float ratio=bound(0.0f,float(x-start)/Q_max(1,end-start),1.0f);
    float value=setting.minimum+std::round((setting.maximum-setting.minimum)*ratio/setting.step)*setting.step;
    Remember(setting.key);EngFuncs::CvarSetValue(setting.key,bound(setting.minimum,value,setting.maximum));m_dirty=true;Refresh();
}

void CMenuPanoramaSettings::Row::Draw(){static_cast<CMenuPanoramaSettings*>(Parent())->PaintRow(*this);}
void CMenuPanoramaSettings::PaintRow(Row &row)
{
    int count;const Setting *settings=Definitions(count);if(row.index<0||row.index>=count)return;
    const Setting &setting=settings[row.index];const bool enabled=Available(setting);const int width=row.size.w;
    const int height=row.size.h;
    if(row.IsCurrentSelected())Fill(row,0,0,width,height,PackRGBA(115,180,217,20));
    Fill(row,0,height-1,width,1,PackRGBA(255,255,255,12));
    Text(row,16,0,width-350,height,setting.label,m_category==4?16:17,enabled?Ink():Muted(),false,m_category==4?QM_LEFT:QM_CENTER);
    const std::string value=m_capture==row.index?"请按下一个按键…":Value(setting);
    if(setting.kind==PlayerName)return;
    if(setting.kind==Number&&enabled)
    {
        const float ratio=bound(0.0f,(EngFuncs::GetCvarFloat(setting.key)-setting.minimum)/(setting.maximum-setting.minimum),1.0f);
        Fill(row,width-272,22,212,3,PackRGBA(180,194,205,70));
        Fill(row,width-272,22,int(212*ratio),3,AccentColor());Fill(row,width-275+int(212*ratio),17,6,13,Ink());
        Text(row,width-54,0,48,48,value.c_str(),15,Ink(),false,QM_CENTER);
    }
    else Text(row,width-330,0,312,height,value.c_str(),m_category==4?15:16,m_capture==row.index?AccentColor():enabled?Ink():Muted(),false,m_category==4?QM_RIGHT:QM_CENTER);
}

bool CMenuPanoramaSettings::KeyDown(int key)
{
    if(m_displayOpen)
    {
        if(UI::Key::IsEscape(key)){CancelDisplay();return true;}
        if(!m_displayTesting&&(key==K_LEFTARROW||key==K_RIGHTARROW))
        {
            const int direction=key==K_LEFTARROW?-1:1;
            if(ItemAtCursor()==&m_displayMode){CycleDisplayMode(direction);return true;}
            if(ItemAtCursor()==&m_displayWindow){CycleDisplayWindow(direction);return true;}
        }
        return CMenuFramework::KeyDown(key);
    }
    if(m_capture>=0){m_captureKey=key;return true;}
    for(const auto &row:m_rows)if(row.dragging)return true;
    if(key==K_MWHEELDOWN){Scroll(3);return true;}if(key==K_MWHEELUP){Scroll(-3);return true;}
    if(key==K_PGDN){Scroll(9);return true;}if(key==K_PGUP){Scroll(-9);return true;}
    if(UI::Key::IsEscape(key)){Back();return true;}
    if(key==K_DEL)
    {
        int count;const Setting *settings=Definitions(count);
        for(auto &row:m_rows)if(ItemAtCursor()==&row&&row.index>=0&&settings[row.index].kind==Binding)
        {
            for(int k=0;k<256;++k){const char *b=EngFuncs::KEY_GetBinding(k);if(b&&!std::strcmp(b,settings[row.index].key)){m_touchedBindings[k]=true;EngFuncs::KEY_SetBinding(k,"");}}
            m_dirty=true;Refresh();return true;
        }
    }
    return CMenuFramework::KeyDown(key);
}
bool CMenuPanoramaSettings::KeyUp(int key)
{
    if(m_capture>=0)
    {
        if(key!=m_captureKey)return true;
        if(key!=K_ESCAPE&&!UI::Key::IsConsole(key)&&key>=0&&key<256)
        {
            int count;const Setting *settings=Definitions(count);
            // Retain other bindings for the action; the selected physical key
            // changes to the requested action exactly as in the native binder.
            m_touchedBindings[key]=true;EngFuncs::KEY_SetBinding(key,settings[m_capture].key);m_dirty=true;
        }
        m_capture=-1;m_captureKey=-1;Refresh();return true;
    }
    if(key==K_MOUSE1)
    {
        bool dragged=false;for(auto &row:m_rows)if(row.dragging){row.dragging=false;dragged=true;}
        if(dragged)return true;
    }
    return CMenuFramework::KeyUp(key);
}
bool CMenuPanoramaSettings::MouseMove(int x,int y)
{
    if(m_capture>=0)return true;
    for(auto &row:m_rows)if(row.dragging){SliderAt(row,x);return true;}
    return CMenuFramework::MouseMove(x,y);
}
void CMenuPanoramaSettings::Save()
{
    if(std::strcmp(m_playerName.GetBuffer(),EngFuncs::GetCvarString("name")))
    {
        const std::string name=m_playerName.GetBuffer();m_nameInvalid=!ValidPlayerName(name);
        if(m_nameInvalid){SelectCategory(1);SetCursorToItem(m_playerName);return;}
        EngFuncs::CvarSetString("name",name.c_str());
    }
    if(!m_dirty)return;EngFuncs::ClientCmd(false,"host_writeconfig\n");m_original.clear();m_dirty=false;
    for(int key=0;key<256;++key){const char *b=EngFuncs::KEY_GetBinding(key);m_originalBindings[key]=b?b:"";m_touchedBindings[key]=false;}
    Refresh();
}
void CMenuPanoramaSettings::NameChanged()
{
    if(m_nameLoading)return;
    m_nameInvalid=false;m_dirty=true;m_save.SetGrayed(false);m_revert.SetGrayed(false);
}
void CMenuPanoramaSettings::Revert()
{
    for(const auto &original:m_original)EngFuncs::CvarSetString(original.name.c_str(),original.value.c_str());
    for(int key=0;key<256;++key)if(m_touchedBindings[key]){EngFuncs::KEY_SetBinding(key,m_originalBindings[key].c_str());m_touchedBindings[key]=false;}
    m_nameLoading=true;m_playerName.SetBuffer(EngFuncs::GetCvarString("name"));m_nameLoading=false;m_nameInvalid=false;
    m_original.clear();m_dirty=false;m_capture=-1;Refresh();
}
void CMenuPanoramaSettings::Back()
{
    Revert();
    if(CL_IsActive())
    {
        Hide();if(m_backCallback)m_backCallback();
    }
    else UI_PanoramaHome();
}

void CMenuPanoramaSettings::OpenDisplay()
{
    m_modes.clear();m_displayOriginalMode=int(EngFuncs::GetCvarFloat("vid_mode"));
    m_displayOriginalWindow=bound(0,int(EngFuncs::GetCvarFloat("fullscreen")),2);
    m_displayOriginalWidth=int(ScreenWidth);m_displayOriginalHeight=int(ScreenHeight);
    m_displaySelectedMode=-1;
    for(int i=0;;++i)
    {
        const char *mode=EngFuncs::GetModeString(i);if(!mode)break;
        int width=0,height=0;
        if(std::sscanf(mode,"%d%*[^0-9]%d",&width,&height)!=2||width<=0||height<=0)continue;
        if(width==m_displayOriginalWidth&&height==m_displayOriginalHeight)m_displaySelectedMode=int(m_modes.size());
        m_modes.push_back({i,width,height});
    }
    if(m_displaySelectedMode<0)
    {
        m_displaySelectedMode=int(m_modes.size());
        m_modes.push_back({-1,m_displayOriginalWidth,m_displayOriginalHeight});
    }
    m_displayInitialMode=m_displaySelectedMode;m_displaySelectedWindow=m_displayOriginalWindow;
    m_displayOpen=true;m_displayTesting=false;m_displayTestUntil=0;m_capture=-1;
    Refresh();SetCursorToItem(m_displayWindow);
}

void CMenuPanoramaSettings::CycleDisplayMode(int direction)
{
    if(m_displayTesting||m_modes.empty()||m_displaySelectedWindow==2)return;
    m_displaySelectedMode=(m_displaySelectedMode+direction+int(m_modes.size()))%int(m_modes.size());RefreshDisplay();
}

void CMenuPanoramaSettings::CycleDisplayWindow(int direction)
{
    if(m_displayTesting)return;
    m_displaySelectedWindow=(m_displaySelectedWindow+direction+3)%3;RefreshDisplay();
}

void CMenuPanoramaSettings::RefreshDisplay()
{
    m_displayMode.SetVisibility(m_displayOpen&&!m_displayTesting);m_displayWindow.SetVisibility(m_displayOpen&&!m_displayTesting);
    m_displayApply.SetVisibility(m_displayOpen);m_displayCancel.SetVisibility(m_displayOpen);
    if(!m_displayOpen)return;
    char mode[96];
    if(m_modes.empty())std::snprintf(mode,sizeof(mode),"分辨率：%d × %d",m_displayOriginalWidth,m_displayOriginalHeight);
    else std::snprintf(mode,sizeof(mode),"分辨率：%d × %d",m_modes[m_displaySelectedMode].width,m_modes[m_displaySelectedMode].height);
    m_displayMode.SetText(mode);m_displayMode.SetGrayed(m_modes.empty()||m_displaySelectedWindow==2);
    const char *windowModes[]={"显示模式：窗口","显示模式：全屏","显示模式：无边框"};
    m_displayWindow.SetText(windowModes[m_displaySelectedWindow]);
    m_displayApply.SetText(m_displayTesting?"保留并保存":"应用显示设置");
    m_displayCancel.SetText(m_displayTesting?"恢复原设置":"取消");
}

void CMenuPanoramaSettings::ApplyDisplay()
{
    if(!m_displayOpen)return;
    if(m_displayTesting)
    {
        m_displayTesting=false;m_displayOpen=false;m_displayTestUntil=0;m_dirty=true;
        Save();Refresh();SetCursorToItem(m_tabs[3]);return;
    }
    const bool modeChanged=!m_modes.empty()&&m_displaySelectedWindow!=2&&m_displaySelectedMode!=m_displayInitialMode;
    const bool windowChanged=m_displaySelectedWindow!=m_displayOriginalWindow;
    if(!modeChanged&&!windowChanged){CancelDisplay();return;}
    // Match the native VideoModes menu: apply the window style before the
    // requested resolution, and retain dimensions for an exact timed restore.
    m_displayTesting=true;m_displayTestUntil=uiStatic.realTime+10000;
    if(windowChanged)EngFuncs::CvarSetValue("fullscreen",m_displaySelectedWindow);
    if(modeChanged)
    {
        const VideoMode &mode=m_modes[m_displaySelectedMode];
        if(mode.index>=0){EngFuncs::ClientCmdF(true,"vid_setmode %i\n",mode.index);EngFuncs::CvarSetValue("vid_mode",mode.index);}
        else EngFuncs::ClientCmdF(true,"vid_setmode %i %i\n",mode.width,mode.height);
    }
    RefreshDisplay();SetCursorToItem(m_displayApply);
}

void CMenuPanoramaSettings::CancelDisplay()
{
    if(!m_displayOpen)return;
    if(m_displayTesting)
    {
        const bool windowFirst=m_displayOriginalWindow==0&&EngFuncs::GetCvarFloat("fullscreen")!=0;
        if(windowFirst)EngFuncs::CvarSetValue("fullscreen",m_displayOriginalWindow);
        EngFuncs::ClientCmdF(true,"vid_setmode %i %i\n",m_displayOriginalWidth,m_displayOriginalHeight);
        if(!windowFirst)EngFuncs::CvarSetValue("fullscreen",m_displayOriginalWindow);
        EngFuncs::CvarSetValue("vid_mode",m_displayOriginalMode);
    }
    m_displayOpen=false;m_displayTesting=false;m_displayTestUntil=0;
    Refresh();SetCursorToItem(m_tabs[3]);
}

void CMenuPanoramaSettings::PaintDisplay()
{
    const int left=(uiStatic.width-760)/2;
    Fill(*this,left,185,760,375,PackRGBA(14,22,29,235));
    Text(*this,left+30,200,700,42,"显示设置",28,Ink(),false,QM_CENTER);
    if(m_displayTesting)
    {
        char text[128];std::snprintf(text,sizeof(text),"是否保留显示设置？%d 秒后自动恢复。",Q_max(0,(m_displayTestUntil-uiStatic.realTime+999)/1000));
        Text(*this,left+30,280,700,64,text,22,Ink(),false,QM_CENTER);
        Text(*this,left+30,356,700,40,"保留后将保存当前设置。",16,Muted(),false,QM_CENTER);
    }
    else Text(*this,left+30,395,700,34,"点击切换，方向键微调；应用后可在十秒内恢复。",16,Muted(),false,QM_CENTER);
}

void CMenuPanoramaSettings::Draw()
{
    if(m_displayTesting&&uiStatic.realTime>=m_displayTestUntil)CancelDisplay();
    oc2_panorama::DrawBackground();
    const bool inGame=CL_IsActive();
    if(inGame)Fill(*this,88,28,uiStatic.width-122,718,PackRGBA(15,21,27,208));
    Text(*this,104,43,uiStatic.width-154,48,"设置",34,Ink(),false,QM_CENTER);
    Fill(*this,104,99,uiStatic.width-154,1,PackRGBA(255,255,255,35));
    if(m_displayOpen){PaintDisplay();CMenuFramework::Draw();return;}
    const int tabWidth=(uiStatic.width-154)/5;
    Fill(*this,104+m_category*tabWidth,157,tabWidth-4,3,AccentColor());
    if(m_category==4)
    {
        Fill(*this,104,185,uiStatic.width-502,464,PackRGBA(17,25,33,242));
        Fill(*this,104,185,3,464,PackRGBA(115,180,217,160));
        Text(*this,138,192,420,26,"准星参数",18,Ink(),true);
        Text(*this,uiStatic.width-590,192,170,26,"实时生效",12,Muted(),false,QM_RIGHT);
    }
    if(m_visible.size()>9)
    {
        const int height=459,thumb=Q_max(26,height*9/int(m_visible.size()));
        const int scrollbar=uiStatic.width-(m_category==4?406:55);
        Fill(*this,scrollbar,185,3,height,PackRGBA(255,255,255,20));
        Fill(*this,scrollbar,185+(height-thumb)*m_scroll/Q_max(1,int(m_visible.size())-9),3,thumb,PackRGBA(210,219,226,135));
    }
    Text(*this,104,650,uiStatic.width-400,30,m_nameInvalid?"请输入有效昵称（最多 31 字节）；不支持控制符或引号等特殊符号":m_category==1?"昵称支持中文  ·  点击保存更改后生效  ·  返回放弃未保存修改":m_category==0?"滚轮滚动  ·  点击键位后按新键  ·  删除键清除键位":"滚轮滚动  ·  点击或拖动修改  ·  方向键微调",13,Muted(),false,QM_CENTER);
    Text(*this,225,690,uiStatic.width-760,48,m_dirty?"已修改":"已保存",13,Muted(),false,QM_CENTER);
    if(m_category==4)PaintCrosshair();
    if(!inGame)m_chrome.Draw();CMenuFramework::Draw();
    if(m_capture>=0)
    {
        Fill(*this,104,176,uiStatic.width-154,482,PackRGBA(0,0,0,178));
        Text(*this,180,325,uiStatic.width-330,60,"请按下新的按键",28,Ink(),false,QM_CENTER);
        Text(*this,180,391,uiStatic.width-330,35,"Esc 取消；该按键原有操作将被替换。",16,Muted(),false,QM_CENTER);
    }
    if(!inGame&&m_drawChrome)m_drawChrome(this);
}

void CMenuPanoramaSettings::CrosshairDefaults()
{
 for(const auto &v:oc2_crosshair::variables){Remember(v.name);EngFuncs::CvarSetString(v.name,v.value);}
 Remember("crosshair");EngFuncs::CvarSetValue("crosshair",1);m_dirty=true;Refresh();
}
void CMenuPanoramaSettings::CrosshairDemo(CMenuBaseItem *item)
{
 for(int i=0;i<3;++i)if(item==&m_crosshairDemo[i])m_crosshairDemoMode=i;
 Refresh();
}
void CMenuPanoramaSettings::PaintCrosshair()
{
 const int x=uiStatic.width-378;
 Fill(*this,x,185,328,464,PackRGBA(17,25,33,247));
 Fill(*this,x,185,328,2,PackRGBA(115,180,217,150));
 Text(*this,x+20,201,250,30,"准星预览",22,Ink(),true);
 Text(*this,x+20,235,288,24,"在实景中查看颜色、间距与动态",12,Muted());
 const auto preview=Pixel(*this,x+18,274);const auto previewSize=Size(292,164).Scale();
 UI_DrawPic(preview,previewSize,PackRGBA(255,255,255,255),"gfx/shell/splash.bmp");
 UI_FillRect(preview,previewSize,PackRGBA(13,20,27,35));
 Fill(*this,x+18,274,292,1,PackRGBA(180,205,220,70));Fill(*this,x+18,438,292,1,PackRGBA(180,205,220,70));
 const auto config=oc2_crosshair::ReadConfig([](const char *key){return EngFuncs::GetCvarFloat(key);});
 oc2_crosshair::Input input;input.width=ScreenWidth;input.height=ScreenHeight;
 const auto center=Pixel(*this,x+164,356);input.centerX=center.x;input.centerY=center.y;
 input.spread=m_crosshairDemoMode==1?.045f:m_crosshairDemoMode==2?.012f+.04f*(.5f+.5f*std::sin(gpGlobals->time*8)):.007f;
 input.speed=m_crosshairDemoMode==1?220:0;
 input.oldGap=m_crosshairDemoMode==2?4+12*(.5f+.5f*std::sin(gpGlobals->time*8)):4;
 if(EngFuncs::GetCvarFloat("crosshair"))for(const auto &r:oc2_crosshair::Build(config,input))
 {
  const auto white=EngFuncs::PIC_Load("*white");EngFuncs::PIC_Set(white,r.r,r.g,r.b,r.a);
  if(r.additive)EngFuncs::PIC_DrawAdditive(r.x,r.y,r.w,r.h);else EngFuncs::PIC_DrawTrans(r.x,r.y,r.w,r.h);
 }
 Text(*this,x+20,453,288,28,"演示状态",13,Muted());
 Text(*this,x+20,551,288,24,"预览按游戏中的实际尺寸显示",12,Muted(),false,QM_CENTER);
}
void CMenuPanoramaSettings::Dump()
{
    Con_Printf("panorama_settings: visible=%d category=%d scroll=%d rows=%u dirty=%d capture=%d display_open=%d display_testing=%d display_mode=%d display_window=%d\n",int(IsVisible()),m_category,m_scroll,unsigned(m_visible.size()),int(m_dirty),m_capture,int(m_displayOpen),int(m_displayTesting),m_displaySelectedMode,m_displaySelectedWindow);
    int count;const Setting *settings=Definitions(count);
    for(int i=0;i<count;++i)
    {
        const Setting &s=settings[i];const std::string value=Value(s);
        Con_Printf("panorama_setting: index=%d category=%d type=%d key=%s value=%s default=%s available=%d\n",i,s.category,int(s.kind),s.key,value.c_str(),s.defaultValue,int(Available(s)));
    }
    for(int i=0;i<ItemCount();++i)
    {
        CMenuBaseItem *item=GetItemByIndex(i);if(!item||!item->szTag)continue;
        const Point p=item->GetRenderPosition();const Size s=item->GetRenderSize();
        Con_Printf("frontend_item: tag=%s x=%d y=%d w=%d h=%d visible=%d enabled=%d focus=%d units=screen_pixels\n",item->szTag,p.x,p.y,s.w,s.h,int(IsVisible()&&item->IsVisible()),int(!(item->iFlags&(QMF_GRAYED|QMF_INACTIVE))),int(item->IsCurrentSelected()));
    }
}
