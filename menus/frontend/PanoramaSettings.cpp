#include "PanoramaSettings.h"
#include "keydefs.h"
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
{UI_DrawString(bold?uiStatic.hBoldFont:uiStatic.hDefaultFont,Pixel(item,x,y),Size(w,h).Scale(),s,c,Q_max(1,int(height*uiStatic.scaleY)),align,ETF_FORCECOL|ETF_NO_WRAP);}
std::vector<std::string> Split(const char *s)
{
    std::vector<std::string> parts;if(!s)return parts;
    const char *start=s;for(;;++s)if(*s=='|'||!*s){parts.emplace_back(start,s-start);if(!*s)break;start=s+1;}
    return parts;
}
const char *Categories[]={"键盘 / 鼠标","游戏设置","音频设置","视频设置"};
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
        {1,Choice,"快速切换武器","hud_fastswitch","0",0,1,1,"关闭|开启","0|1"},
        {1,Choice,"持枪手","cl_righthand","1",0,1,1,"左手|右手","0|1"},
        {1,Number,"持枪视野","viewmodel_fov","54",54,68,1,nullptr,nullptr},
        {1,Number,"持枪位置：左右","viewmodel_offset_x","0",-2,2.5f,0.1f,nullptr,nullptr},
        {1,Number,"持枪位置：前后","viewmodel_offset_y","0",-2,2,0.1f,nullptr,nullptr},
        {1,Number,"持枪位置：上下","viewmodel_offset_z","0",-2,2,0.1f,nullptr,nullptr},
        {1,Choice,"动态准星","cl_dynamiccrosshair","1",0,1,1,"关闭|开启","0|1"},
        {1,Choice,"准星大小","cl_crosshair_size","auto",0,3,1,"自动|小|中|大","auto|small|medium|large"},
        {1,Choice,"半透明准星","cl_crosshair_translucent","1",0,1,1,"关闭|开启","0|1"},
        {1,Choice,"允许下载服务器资源","cl_allowdownload","1",0,1,1,"关闭|开启","0|1"},
        {1,Choice,"显示天气效果","cl_weather","1",0,1,1,"关闭|开启","0|1"},
        {1,Choice,"简化玩家模型","cl_minmodels","0",0,1,1,"关闭|开启","0|1"},
        {2,Number,"主音量","volume","0.7",0,1,0.05f,nullptr,nullptr},
        {2,Number,"音乐音量","MP3Volume","1.0",0,1,0.05f,nullptr,nullptr},
        {2,Choice,"失去焦点时静音","snd_mute_losefocus","1",0,1,1,"关闭|开启","0|1"},
        {2,Choice,"环境音效处理","room_off","0",0,1,1,"开启|关闭","0|1"},
        {2,Choice,"Alpha 环境音效算法","dsp_coeff_table","0",0,1,1,"关闭|开启","0|1"},
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

CMenuPanoramaSettings::CMenuPanoramaSettings():CMenuFramework("CMenuPanoramaSettings"),m_category(0),m_scroll(0),m_capture(-1),m_captureKey(-1),m_dirty(false),m_drawChrome(nullptr)
{for(bool &b:m_touchedBindings)b=false;}

void CMenuPanoramaSettings::Button(CMenuAction &item,const char *label,const char *tag)
{
    item.SetNameAndStatus(label,nullptr,tag);item.SetCharSize(QM_SMALLFONT);
    item.colorBase=Ink();item.colorFocus=AccentColor();item.eTextAlignment=QM_LEFT;
    item.SetTextInsetX(18);item.bIgnoreColorstring=true;
    item.SetBackground(PackRGBA(255,255,255,8),PackRGBA(255,255,255,22));AddItem(item);
}

void CMenuPanoramaSettings::_Init()
{
    m_chrome.Init(*this,2);
    const char *tags[]={"settings_keyboard","settings_game","settings_audio","settings_video"};
    for(int i=0;i<4;++i)
    {
        Button(m_tabs[i],Categories[i],tags[i]);
        SET_EVENT_MULTI(m_tabs[i].onReleased,{static_cast<CMenuPanoramaSettings*>(pSelf->Parent())->ChooseTab(pSelf);});
    }
    Button(m_back,"返回","settings_back");m_back.onReleased=VoidCb(&CMenuPanoramaSettings::Back);
    Button(m_save,"保存更改","settings_save");m_save.onReleased=VoidCb(&CMenuPanoramaSettings::Save);
    Button(m_revert,"撤销本次更改","settings_revert");m_revert.onReleased=VoidCb(&CMenuPanoramaSettings::Revert);
    Button(m_up,"上移","settings_up");SET_EVENT_MULTI(m_up.onReleased,{static_cast<CMenuPanoramaSettings*>(pSelf->Parent())->Scroll(-1);});
    Button(m_down,"下移","settings_down");SET_EVENT_MULTI(m_down.onReleased,{static_cast<CMenuPanoramaSettings*>(pSelf->Parent())->Scroll(1);});
    for(int i=0;i<9;++i)
    {
        std::snprintf(m_rowTags[i],sizeof(m_rowTags[i]),"settings_row_%d",i);
        Button(m_rows[i],"",m_rowTags[i]);
        SET_EVENT_MULTI(m_rows[i].onReleased,{static_cast<CMenuPanoramaSettings*>(pSelf->Parent())->Activate(pSelf);});
    }
}

void CMenuPanoramaSettings::_VidInit()
{
    m_chrome.Layout();
    const int width=uiStatic.width,left=104,right=width-50;
    for(int i=0;i<4;++i)m_tabs[i].SetRect(left+i*180,112,176,46);
    for(int i=0;i<9;++i)m_rows[i].SetRect(left,185+i*51,right-left-20,48);
    m_up.SetRect(right-115,650,55,30);m_down.SetRect(right-55,650,55,30);
    m_back.SetRect(left,696,100,42);m_save.SetRect(right-190,690,190,48);
    m_revert.SetRect(right-400,690,195,48);Refresh();
}

void CMenuPanoramaSettings::Show()
{
    CMenuFramework::Show();m_original.clear();m_dirty=false;m_capture=-1;m_captureKey=-1;
    for(int key=0;key<256;++key){const char *b=EngFuncs::KEY_GetBinding(key);m_originalBindings[key]=b?b:"";m_touchedBindings[key]=false;}
    Refresh();SetCursorToItem(m_tabs[m_category]);
}
void CMenuPanoramaSettings::Hide(){Save();CMenuFramework::Hide();}

void CMenuPanoramaSettings::SelectCategory(int category)
{m_category=bound(0,category,3);m_scroll=0;m_capture=-1;Refresh();}
void CMenuPanoramaSettings::ChooseTab(CMenuBaseItem *item)
{for(int i=0;i<4;++i)if(item==&m_tabs[i])SelectCategory(i);}
void CMenuPanoramaSettings::Refresh()
{
    int count;const Setting *settings=Definitions(count);m_visible.clear();
    for(int i=0;i<count;++i)if(settings[i].category==m_category)m_visible.push_back(i);
    m_scroll=bound(0,m_scroll,Q_max(0,int(m_visible.size())-9));
    for(int i=0;i<9;++i)
    {
        const int row=m_scroll+i;m_rows[i].index=row<int(m_visible.size())?m_visible[row]:-1;
        m_rows[i].SetVisibility(m_rows[i].index>=0);m_rows[i].SetGrayed(m_rows[i].index>=0&&!Available(settings[m_rows[i].index]));
    }
    for(int i=0;i<4;++i)m_tabs[i].colorBase=i==m_category?AccentColor():Ink();
    m_up.SetGrayed(m_scroll==0);m_down.SetGrayed(m_scroll+9>=int(m_visible.size()));
    m_up.SetVisibility(m_visible.size()>9);m_down.SetVisibility(m_visible.size()>9);
    m_save.SetGrayed(!m_dirty);m_revert.SetGrayed(!m_dirty);
}
void CMenuPanoramaSettings::Scroll(int rows)
{m_scroll+=rows;Refresh();}

bool CMenuPanoramaSettings::Available(const Setting &setting) const
{
    if(setting.kind==Binding||setting.kind==Resolution)return true;
    const char *value=EngFuncs::GetCvarString(setting.key);
    return value&&*value&&(!EngFuncs::textfuncs.pfnIsCvarReadOnly||EngFuncs::textfuncs.pfnIsCvarReadOnly(setting.key)<=0);
}

std::string CMenuPanoramaSettings::Value(const Setting &setting) const
{
    if(setting.kind==Binding)
    {
        std::string result;
        for(int key=0;key<256;++key)
        {
            const char *bind=EngFuncs::KEY_GetBinding(key);
            if(bind&&!std::strcmp(bind,setting.key))
            {if(!result.empty())result+=" / ";result+=EngFuncs::KeynumToString(key);}
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
    if(setting.kind==Binding){m_capture=index;m_captureKey=-1;return;}
    if(setting.kind==Resolution){UI_VidModes_Menu();return;}
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
    if(row.IsCurrentSelected())Fill(row,0,0,width,48,PackRGBA(255,255,255,17));
    Fill(row,0,47,width,1,PackRGBA(255,255,255,18));
    Text(row,16,10,width-350,30,setting.label,17,enabled?Ink():Muted());
    const std::string value=m_capture==row.index?"请按下一个按键…":Value(setting);
    if(setting.kind==Number&&enabled)
    {
        const float ratio=bound(0.0f,(EngFuncs::GetCvarFloat(setting.key)-setting.minimum)/(setting.maximum-setting.minimum),1.0f);
        Fill(row,width-272,22,212,3,PackRGBA(180,194,205,70));
        Fill(row,width-272,22,int(212*ratio),3,AccentColor());Fill(row,width-275+int(212*ratio),17,6,13,Ink());
        Text(row,width-54,9,48,30,value.c_str(),15,Ink(),false,QM_CENTER);
    }
    else Text(row,width-330,10,312,30,value.c_str(),16,m_capture==row.index?AccentColor():enabled?Ink():Muted(),false,QM_RIGHT);
}

bool CMenuPanoramaSettings::KeyDown(int key)
{
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
    if(!m_dirty)return;EngFuncs::ClientCmd(false,"host_writeconfig\n");m_original.clear();m_dirty=false;
    for(int key=0;key<256;++key){const char *b=EngFuncs::KEY_GetBinding(key);m_originalBindings[key]=b?b:"";m_touchedBindings[key]=false;}
    Refresh();
}
void CMenuPanoramaSettings::Revert()
{
    for(const auto &original:m_original)EngFuncs::CvarSetString(original.name.c_str(),original.value.c_str());
    for(int key=0;key<256;++key)if(m_touchedBindings[key]){EngFuncs::KEY_SetBinding(key,m_originalBindings[key].c_str());m_touchedBindings[key]=false;}
    m_original.clear();m_dirty=false;m_capture=-1;Refresh();
}
void CMenuPanoramaSettings::Back(){Save();UI_PanoramaHome();}

void CMenuPanoramaSettings::Draw()
{
    if(CL_IsActive())return;
    oc2_panorama::DrawBackground();
    Text(*this,104,43,600,48,"设置",34,Ink());
    Fill(*this,104,99,uiStatic.width-154,1,PackRGBA(255,255,255,35));
    Fill(*this,104+m_category*180,157,176,3,AccentColor());
    if(m_visible.size()>9)
    {
        const int height=459,thumb=Q_max(26,height*9/int(m_visible.size()));
        Fill(*this,uiStatic.width-55,185,3,height,PackRGBA(255,255,255,20));
        Fill(*this,uiStatic.width-55,185+(height-thumb)*m_scroll/Q_max(1,int(m_visible.size())-9),3,thumb,PackRGBA(210,219,226,135));
    }
    Text(*this,104,654,uiStatic.width-400,26,m_category==0?"滚轮滚动  ·  点击键位后按新键  ·  Delete 清除键位":"滚轮滚动  ·  点击或拖动修改  ·  方向键微调",13,Muted());
    Text(*this,225,704,uiStatic.width-760,25,m_dirty?"更改已生效，返回时保存。":"设置已保存",13,Muted());
    m_chrome.Draw();CMenuFramework::Draw();
    if(m_capture>=0)
    {
        Fill(*this,104,176,uiStatic.width-154,482,PackRGBA(0,0,0,178));
        Text(*this,180,325,uiStatic.width-330,60,"请按下新的按键",28,Ink(),false,QM_CENTER);
        Text(*this,180,391,uiStatic.width-330,35,"Esc 取消；该按键原有操作将被替换。",16,Muted(),false,QM_CENTER);
    }
    if(m_drawChrome)m_drawChrome(this);
}

void CMenuPanoramaSettings::Dump()
{
    Con_Printf("panorama_settings: visible=%d category=%d scroll=%d rows=%u dirty=%d capture=%d\n",int(IsVisible()),m_category,m_scroll,unsigned(m_visible.size()),int(m_dirty),m_capture);
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
