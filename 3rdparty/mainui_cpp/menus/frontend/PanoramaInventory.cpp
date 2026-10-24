#include "extdll_menu.h"
#include "BaseMenu.h"
#include "Utils.h"
#include "PanoramaInventory.h"
#include "keydefs.h"
#include <cmath>
#include <cstdio>

namespace
{
const double kPi=3.14159265358979323846;
unsigned WheelColor(int slot,int selected,int hover)
{
    return slot==selected?PackRGBA(173,154,109,232):slot==hover?PackRGBA(75,91,104,232):PackRGBA(39,51,61,224);
}
void DrawLabel(Point p,Size z,const char *text,int height,unsigned color,bool bold=false,unsigned align=QM_CENTER)
{
    // Desktop fonts have fixed glyph sizes; charH alone does not scale them.
    const HFont font=height<=12?uiStatic.hTinyFont:height<=18?uiStatic.hSmallerFont:
        height<=22?uiStatic.hSmallFont:bold?uiStatic.hBoldFont:uiStatic.hDefaultFont;
    const int tall=height<=12?12:height<=18?16:height<=22?20:26;
    UI_DrawString(font,p,z,text,color,Q_max(1,int(tall*uiStatic.scaleY)),align,ETF_FORCECOL|ETF_NO_WRAP);
}
}

int CMenuPanoramaWheel::Hit(int x,int y) const
{
    if(entries.empty())return -1;
    const Point p=GetRenderPosition();const Size z=GetRenderSize();
    const float dx=float(x-p.x-z.w/2),dy=float(y-p.y-z.h/2),r=float(Q_min(z.w,z.h))*.5f;
    const float distance=dx*dx+dy*dy;if(distance>r*r||distance<r*r*.19f)return -1;
    double angle=std::atan2(double(dy),double(dx))+kPi*.5+kPi/entries.size();
    if(angle<0)angle+=2*kPi;if(angle>=2*kPi)angle-=2*kPi;
    return Q_min(int(entries.size())-1,int(angle*entries.size()/(2*kPi)));
}

Point CMenuPanoramaWheel::SlotCenter(int slot) const
{
    const Point p=GetRenderPosition();const Size z=GetRenderSize();
    const double angle=-kPi*.5+2*kPi*slot/Q_max(1,int(entries.size()));
    const double radius=Q_min(z.w,z.h)*.365;
    return p+Point(z.w/2+int(std::cos(angle)*radius),z.h/2+int(std::sin(angle)*radius));
}

bool CMenuPanoramaWheel::MouseMove(int x,int y){hover=Hit(x,y);return true;}
bool CMenuPanoramaWheel::KeyDown(int key)
{
    if(UI::Key::IsLeftMouse(key))return Hit(uiStatic.cursorX,uiStatic.cursorY)>=0;
    if(UI::Key::IsEnter(key))return true;
    if(UI::Key::IsLeftArrow(key)||UI::Key::IsRightArrow(key))
    {
        if(entries.empty())return true;
        selected=(selected+(UI::Key::IsLeftArrow(key)?-1:1)+int(entries.size()))%int(entries.size());
        _Event(QM_CHANGED);return true;
    }
    return CMenuBaseItem::KeyDown(key);
}
bool CMenuPanoramaWheel::KeyUp(int key)
{
    const int hit=Hit(uiStatic.cursorX,uiStatic.cursorY);
    if(UI::Key::IsLeftMouse(key)&&hit>=0){selected=hit;_Event(QM_CHANGED);return true;}
    if(UI::Key::IsEnter(key)){_Event(QM_CHANGED);return true;}
    return CMenuBaseItem::KeyUp(key);
}

void CMenuPanoramaWheel::Draw()
{
    const Point p=GetRenderPosition();const Size z=GetRenderSize();
    const int radius=Q_min(z.w,z.h)/2,cx=p.x+z.w/2,cy=p.y+z.h/2;
    if(radius<1||entries.empty())return;
    hover=Hit(uiStatic.cursorX,uiStatic.cursorY);
    // A native scan-converted annulus keeps the visible wedges and angular
    // mouse hit test in the same coordinate system at every screen ratio.
    const int step=Q_max(1,int(2*uiStatic.scaleY));
    if(m_cachedRadius!=radius||m_cachedCount!=int(entries.size())||m_cachedStep!=step)
    {
        m_spans.clear();m_cachedRadius=radius;m_cachedCount=int(entries.size());m_cachedStep=step;
        for(int y=-radius;y<radius;y+=step)
        {
            const int extent=int(std::sqrt(double(radius*radius-y*y)));
            int run=-extent,last=-2;
            for(int x=-extent;x<=extent;++x)
            {
                const int rr=x*x+y*y;
                int slot=-1;
                if(rr>=radius*radius*.19)
                {
                    double angle=std::atan2(double(y),double(x))+kPi*.5+kPi/entries.size();
                    if(angle<0)angle+=2*kPi;if(angle>=2*kPi)angle-=2*kPi;
                    const double sector=angle*entries.size()/(2*kPi);
                    const double edge=sector-std::floor(sector);
                    if(edge>.009&&edge<.991)slot=Q_min(int(entries.size())-1,int(sector));
                }
                if(slot!=last)
                {
                    if(last>=0)m_spans.push_back({run,y,x-run,last});
                    run=x;last=slot;
                }
            }
            if(last>=0)m_spans.push_back({run,y,extent+1-run,last});
        }
    }
    for(const auto &span:m_spans)UI_FillRect(cx+span.x,cy+span.y,span.width,step,WheelColor(span.sector,selected,hover));
    const unsigned ink=PackRGBA(237,240,243,255),muted=PackRGBA(172,183,191,255);
    for(int i=0;i<int(entries.size());++i)
    {
        const Point center=SlotCenter(i);const int box=Q_max(65,int(radius*.68));
        if(!entries[i].image.empty())oc2_local_inventory::DrawImage(entries[i].image,
            center-Point(box/2,int(52*uiStatic.scaleY)),Size(box,int(64*uiStatic.scaleY)));
        DrawLabel(center-Point(box/2,int(entries[i].image.empty()?9:-18)*uiStatic.scaleY),
            Size(box,int(30*uiStatic.scaleY)),entries[i].name.c_str(),entries.size()>7?11:13,ink,true);
    }
    DrawLabel(Point(cx-int(radius*.42),cy-int(42*uiStatic.scaleY)),Size(int(radius*.84),int(38*uiStatic.scaleY)),
        "OC2",27,ink,true);
    DrawLabel(Point(cx-int(radius*.44),cy+int(3*uiStatic.scaleY)),Size(int(radius*.88),int(22*uiStatic.scaleY)),
        title.c_str(),11,muted);
}

void CMenuPanoramaSkinCard::Draw()
{
    CMenuAction::Draw();const Point p=GetRenderPosition();const Size z=GetRenderSize();
    const int inset=Q_max(1,int(9*uiStatic.scaleY));
    const unsigned ink=PackRGBA(236,240,242,255),muted=PackRGBA(163,179,190,255),accent=PackRGBA(221,193,124,255);
    if(selected)UI_DrawRectangle(p,z,accent);
    UI_FillRect(p+Point(0,z.h-Q_max(2,int(3*uiStatic.scaleY))),Size(z.w,Q_max(2,int(3*uiStatic.scaleY))),
        equipped?accent:PackRGBA(105,130,156,255));
    model.DrawAt(p.x+inset,p.y+inset,z.w-inset*2,z.h-int(45*uiStatic.scaleY));
    DrawLabel(p+Point(inset,z.h-int(44*uiStatic.scaleY)),Size(z.w-2*inset,int(22*uiStatic.scaleY)),
        title.c_str(),16,ink,false,QM_LEFT);
    DrawLabel(p+Point(inset,z.h-int(23*uiStatic.scaleY)),Size(z.w-2*inset,int(17*uiStatic.scaleY)),
        equipped?u8"\u5df2\u88c5\u5907":collection.c_str(),12,equipped?accent:muted,false,QM_LEFT);
}

CMenuPanoramaInventory::CMenuPanoramaInventory():CMenuFramework("CMenuPanoramaInventory")
{
    for(int &id:m_cardIds)id=-1;
}
void CMenuPanoramaInventory::SetEditors(CMenuSkinEditor *skins,CMenuAccessoriesEditor *accessories,CMenuGloveEditor *gloves)
{m_skins=skins;m_accessoryEditor=accessories;m_gloveEditor=gloves;}
unsigned CMenuPanoramaInventory::Ink(){return PackRGBA(234,239,243,255);}
unsigned CMenuPanoramaInventory::Muted(){return PackRGBA(159,174,187,255);}
unsigned CMenuPanoramaInventory::Accent(){return PackRGBA(221,193,124,255);}
Point CMenuPanoramaInventory::Pixel(int x,int y)const{return GetRenderPosition()+Point(x,y).Scale();}
void CMenuPanoramaInventory::Fill(int x,int y,int w,int h,unsigned color){UI_FillRect(Pixel(x,y),Size(w,h).Scale(),color);}
void CMenuPanoramaInventory::Text(int x,int y,int w,int h,const char *text,int height,unsigned color,bool bold,unsigned align)
{DrawLabel(Pixel(x,y),Size(w,h).Scale(),text,height,color,bold,align);}
int CMenuPanoramaInventory::Right()const{return uiStatic.width*60/100;}
int CMenuPanoramaInventory::Pages()const{return Q_max(1,(int(m_options.size())+CardCount-1)/CardCount);}
int CMenuPanoramaInventory::WheelPages()const{return m_category==Knife?Q_max(1,(int(m_wheelKeys.size())+6)/7):1;}
const char *CMenuPanoramaInventory::CategoryName(int category)
{
    static const char *names[]={u8"\u624b\u67aa",u8"\u5fae\u578b\u51b2\u950b\u67aa",u8"\u6b65\u67aa",u8"\u72d9\u51fb\u6b65\u67aa",u8"\u91cd\u578b\u6b66\u5668",u8"\u8fd1\u6218\u6b66\u5668",u8"\u624b\u5957"};
    return category>=0&&category<CategoryCount?names[category]:u8"\u5e93\u5b58";
}
const char *CMenuPanoramaInventory::GloveFamily(int index)
{
    static const char *names[]={"Bloodhound","Fingerless","Full Finger","Hand Wraps","Hard Knuckle","Motorcycle","Driver","Specialist","Sport"};
    return index>=0&&index<9?names[index]:"Team Default";
}
int CMenuPanoramaInventory::WeaponCategory(int weapon)
{
    switch(weapon)
    {
    case 1:case 10:case 11:case 16:case 17:case 26:return Pistol;
    case 7:case 12:case 19:case 23:case 30:return SMG;
    case 8:case 14:case 15:case 22:case 27:case 28:return Rifle;
    case 3:case 13:case 18:case 24:return Sniper;
    default:return Heavy;
    }
}

void CMenuPanoramaInventory::Button(CMenuAction &b,const char *name,const char *tag,bool primary)
{
    b.SetNameAndStatus(name,nullptr,tag);b.SetCharSize(QM_SMALLFONT);b.SetTextInsetX(13);
    b.eFocusAnimation=QM_HIGHLIGHTIFFOCUS;b.bIgnoreColorstring=true;
    b.colorBase=primary?PackRGBA(20,26,31,255):Ink();b.colorFocus=primary?PackRGBA(8,12,17,255):Accent();
    b.SetBackground(primary?Accent():PackRGBA(36,48,59,228),primary?PackRGBA(239,216,160,255):PackRGBA(58,75,89,245));AddItem(b);
}
void CMenuPanoramaInventory::_Init()
{
    m_chrome.Init(*this,1);
    for(int i=0;i<CategoryCount;++i)
    {
        std::snprintf(m_categoryTags[i],sizeof(m_categoryTags[i]),"pano_category_%d",i);
        Button(m_categories[i],CategoryName(i),m_categoryTags[i]);
        SET_EVENT_MULTI(m_categories[i].onReleased,{static_cast<CMenuPanoramaInventory*>(pSelf->Parent())->Category(pSelf);});
    }
    m_wheel.SetNameAndStatus("",nullptr,"pano_wheel");AddItem(m_wheel);m_wheel.onChanged=VoidCb(&CMenuPanoramaInventory::WheelSelected);
    Button(m_back,u8"\u8fd4\u56de","pano_back");m_back.onReleased=VoidCb(&CMenuPanoramaInventory::Back);
    Button(m_refresh,u8"\u5237\u65b0","pano_refresh");m_refresh.onReleased=VoidCb(&CMenuPanoramaInventory::Refresh);
    Button(m_previous,"<","pano_previous");m_previous.onReleased=VoidCb(&CMenuPanoramaInventory::Previous);
    Button(m_next,">","pano_next");m_next.onReleased=VoidCb(&CMenuPanoramaInventory::Next);
    Button(m_wheelPrevious,"<","pano_wheel_previous");m_wheelPrevious.onReleased=VoidCb(&CMenuPanoramaInventory::PreviousWheel);
    Button(m_wheelNext,">","pano_wheel_next");m_wheelNext.onReleased=VoidCb(&CMenuPanoramaInventory::NextWheel);
    for(int i=0;i<CardCount;++i)
    {
        std::snprintf(m_cardTags[i],sizeof(m_cardTags[i]),"pano_skin_%d",i);Button(m_cards[i],"",m_cardTags[i]);
        m_cards[i].model.SetInteractive(false);
        SET_EVENT_MULTI(m_cards[i].onReleased,{static_cast<CMenuPanoramaInventory*>(pSelf->Parent())->SelectCard(pSelf);});
    }
    m_search.SetNameAndStatus("",nullptr,"pano_search");m_search.iMaxLength=80;m_search.bAllowColorstrings=false;AddItem(m_search);
    m_inspect.SetNameAndStatus("",nullptr,"pano_model");m_inspect.SetInteractive(true);AddItem(m_inspect);
    Button(m_closeInspect,u8"< \u8fd4\u56de\u76ae\u80a4\u5217\u8868","pano_close_inspect");m_closeInspect.onReleased=VoidCb(&CMenuPanoramaInventory::CloseInspect);
    Button(m_equip,u8"\u88c5\u5907","pano_equip",true);m_equip.onReleased=VoidCb(&CMenuPanoramaInventory::Equip);
    Button(m_patterns,u8"\u76ae\u80a4\u4e0e\u8d34\u7eb8\u56fe\u6848","pano_patterns");m_patterns.onReleased=VoidCb(&CMenuPanoramaInventory::OpenPatterns);
    Button(m_accessories,u8"\u6302\u4ef6\u4e0e\u8ba1\u6570\u5668","pano_attachments");m_accessories.onReleased=VoidCb(&CMenuPanoramaInventory::OpenAccessories);
    for(int i=0;i<4;++i)
    {
        std::snprintf(m_stickerTags[i],sizeof(m_stickerTags[i]),"pano_sticker_%d",i);
        Button(m_stickers[i],"",m_stickerTags[i]);
        SET_EVENT_MULTI(m_stickers[i].onReleased,{static_cast<CMenuPanoramaInventory*>(pSelf->Parent())->OpenSticker(pSelf);});
    }
}
void CMenuPanoramaInventory::_VidInit()
{
    m_chrome.Layout();
    const int w=uiStatic.width,right=Right(),rw=w-right-34;
    const int tabGap=7,tabWidth=(w-132-tabGap*(CategoryCount-1))/CategoryCount;
    for(int i=0;i<CategoryCount;++i)m_categories[i].SetRect(98+i*(tabWidth+tabGap),112,tabWidth,42);
    const int wheelSize=Q_min(500,right-140),wheelX=98+(right-140-wheelSize)/2;
    m_wheel.SetRect(wheelX,186,wheelSize,wheelSize);
    m_wheelPrevious.SetRect(112,676,44,30);m_wheelNext.SetRect(right-74,676,44,30);
    m_search.SetRect(right,228,rw,28);
    const int gap=10,cw=(rw-gap*2)/3,ch=126;
    for(int i=0;i<CardCount;++i)m_cards[i].SetRect(right+(i%3)*(cw+gap),272+(i/3)*(ch+gap),cw,ch);
    m_previous.SetRect(right,686,42,30);m_next.SetRect(w-76,686,42,30);
    m_back.SetRect(98,722,132,32);m_refresh.SetRect(right-114,31,80,32);
    m_inspect.SetRect(92,172,w*73/100-110,480);
    const int side=w*73/100,sideWidth=w-side-40;
    m_closeInspect.SetRect(98,112,186,36);m_equip.SetRect(side,266,sideWidth,48);
    m_patterns.SetRect(side,580,sideWidth,38);m_accessories.SetRect(side,624,sideWidth,38);
    for(int i=0;i<4;++i)m_stickers[i].SetRect(side,358+i*48,sideWidth,40);
    ToggleInspect(m_inspecting);
}

void CMenuPanoramaInventory::Reload()
{
    std::string error,configError,gloveError,gloveConfigError;
    m_catalog.clear();m_gloveCatalog.clear();
    m_catalogValid=oc2_local_inventory::LoadCatalog(m_catalog,error);
    m_configValid=oc2_local_inventory::LoadConfig(m_config,configError);
    if(m_catalogValid&&m_configValid)m_configValid=oc2_inventory::ValidateConfig(m_config,m_catalog,configError);
    m_gloveCatalogValid=oc2_local_gloves::LoadCatalog(m_gloveCatalog,gloveError);
    m_gloveConfigValid=oc2_local_gloves::LoadConfig(m_gloveConfig,gloveConfigError);
    if(m_gloveCatalogValid&&m_gloveConfigValid)m_gloveConfigValid=oc2_gloves::ValidateConfig(m_gloveConfig,m_gloveCatalog,gloveConfigError);
    m_error=m_category==Glove?(!m_gloveCatalogValid||!m_gloveConfigValid):(!m_catalogValid||!m_configValid);
    m_message=m_error?(m_category==Glove?(!m_gloveCatalogValid?gloveError:gloveConfigError):(!m_catalogValid?error:configError)):"";
}
void CMenuPanoramaInventory::Show()
{
    if(CL_IsActive())return;
    Reload();m_page=0;m_inspecting=false;m_returnEditor=false;m_selection=SavedSkin();m_query.clear();
    CMenuFramework::Show();m_search.SetBuffer("");BuildWheel();RefreshCards();ToggleInspect(false);SetCursorToItem(m_categories[m_category]);
}
void CMenuPanoramaInventory::OpenWeapon(int weapon)
{
    if(CL_IsActive()||oc2_accessories::Index(weapon)<0)return;
    m_category=WeaponCategory(weapon);m_weapon=weapon;m_wheelPage=0;Show();
}
void CMenuPanoramaInventory::OpenKnife(int definition)
{
    if(CL_IsActive()||definition<0)return;
    m_category=Knife;m_definition=definition;m_wheelPage=0;Show();
    const auto found=std::find(m_wheelKeys.begin(),m_wheelKeys.end(),definition);
    if(found!=m_wheelKeys.end()){m_wheelPage=int(found-m_wheelKeys.begin())/7;BuildWheel();}
}
void CMenuPanoramaInventory::OpenGloves()
{
    if(CL_IsActive())return;
    m_category=Glove;Reload();
    if(const auto *entry=oc2_gloves::FindEntry(m_gloveCatalog,m_gloveConfig.resource))
        for(int i=0;i<9;++i)if(entry->collection==GloveFamily(i))m_family=i;
    m_wheelPage=0;Show();
}
int CMenuPanoramaInventory::SavedSkin()const
{
    if(m_category==Glove)return m_gloveConfig.resource;
    if(m_category==Knife){const auto *state=oc2_inventory::FindKnife(m_config,m_definition);return state?state->skin:0;}
    const auto *state=oc2_inventory::Find(m_config,m_weapon);return state?state->skin:0;
}
std::string CMenuPanoramaInventory::ItemName()const
{
    if(m_category==Glove)return GloveFamily(m_family);
    if(m_category!=Knife)return oc2_accessories::Name(m_weapon);
    const auto *entry=oc2_inventory::FindKnifeEntry(m_catalog,m_definition);return entry?entry->name:"Default T Knife";
}
std::string CMenuPanoramaInventory::FinishName(int id)const
{
    if(!id)return m_category==Glove?u8"\u9635\u8425\u9ed8\u8ba4":u8"\u539f\u7248";
    if(m_category==Glove){const auto *e=oc2_gloves::FindEntry(m_gloveCatalog,id);return e?e->name:"Unavailable";}
    return oc2_local_inventory::SkinName(m_catalog,id);
}
std::string CMenuPanoramaInventory::CollectionName(int id)const
{
    if(!id)return m_category==Glove?u8"\u9635\u8425\u9ed8\u8ba4 / CT\u9884\u89c8":u8"\u539f\u7248\u6b66\u5668\u5916\u89c2";
    if(m_category==Glove){const auto *e=oc2_gloves::FindEntry(m_gloveCatalog,id);return e?e->collection:"";}
    const auto *e=oc2_inventory::FindEntry(m_catalog,id);return e?e->collection:"";
}
bool CMenuPanoramaInventory::IsEquipped(int id)const
{return SavedSkin()==id&&(m_category!=Knife||m_config.knife==m_definition);}

void CMenuPanoramaInventory::BuildWheel()
{
    m_wheelKeys.clear();m_wheel.entries.clear();m_wheel.title=CategoryName(m_category);
    if(m_category<Knife)
        for(int weapon:oc2_accessories::kWeaponIds)if(WeaponCategory(weapon)==m_category)m_wheelKeys.push_back(weapon);
    if(m_category==Knife){m_wheelKeys.push_back(0);for(const auto &e:m_catalog)if(e.kind==oc2_inventory::Kind::Knife)m_wheelKeys.push_back(e.definition);}
    if(m_category==Glove)for(int i=0;i<9;++i)m_wheelKeys.push_back(i);
    m_wheelPage=Q_min(m_wheelPage,WheelPages()-1);
    const int start=m_category==Knife?m_wheelPage*7:0,finish=m_category==Knife?Q_min(start+7,int(m_wheelKeys.size())):int(m_wheelKeys.size());
    for(int i=start;i<finish;++i)
    {
        CMenuPanoramaWheel::Item item;item.key=m_wheelKeys[i];
        if(m_category<Knife)
        {
            item.name=oc2_accessories::Name(item.key);char path[96];std::snprintf(path,sizeof(path),"gfx/inventory/previews/weapon_%d.tga",item.key);item.image=path;
        }
        else if(m_category==Knife)
        {
            const auto *e=oc2_inventory::FindKnifeEntry(m_catalog,item.key);item.name=e?e->name:"Default T";
            // Knife names identify the model; skin cards below render its mesh.
        }
        else item.name=GloveFamily(item.key);
        m_wheel.entries.push_back(item);
    }
    m_wheel.selected=0;
    const int key=m_category<Knife?m_weapon:m_category==Knife?m_definition:m_family;
    for(int i=0;i<int(m_wheel.entries.size());++i)if(m_wheel.entries[i].key==key)m_wheel.selected=i;
    m_wheelPrevious.SetGrayed(m_wheelPage==0);m_wheelNext.SetGrayed(m_wheelPage+1>=WheelPages());
}
void CMenuPanoramaInventory::Preview(CMenuPanoramaModelView &model,int skin)
{
    if(m_category==Glove)model.SetGloves(skin);
    else if(m_category==Knife)model.SetKnife(m_definition,skin);
    else
    {
        model.SetWeapon(m_weapon,skin);
        if(const auto *saved=oc2_inventory::Find(m_config,m_weapon))
        {auto state=*saved;state.skin=skin;model.SetAppearance(state);}
    }
}
void CMenuPanoramaInventory::RefreshCards()
{
    m_options.clear();
    const auto matches=[this](const std::string &text){return m_query.empty()||oc2_local_inventory::Fold(text).find(m_query)!=std::string::npos;};
    if(matches(m_category==Glove?u8"Team default Original \u9635\u8425\u9ed8\u8ba4 \u539f\u7248":u8"Original \u539f\u7248"))m_options.push_back(0);
    if(m_category==Glove)
    {
        for(const auto &e:m_gloveCatalog)if(e.collection==GloveFamily(m_family)&&matches(e.name+" "+e.collection))m_options.push_back(e.id);
    }
    else for(const auto &e:m_catalog)
    {
        const bool kind=m_category==Knife?(e.kind==oc2_inventory::Kind::KnifeSkin&&e.definition==m_definition):
            (e.kind==oc2_inventory::Kind::GunSkin&&e.weapon==m_weapon);
        if(kind&&matches(e.name+" "+e.collection))m_options.push_back(e.id);
    }
    m_page=Q_min(m_page,Pages()-1);
    for(int i=0;i<CardCount;++i)
    {
        const int index=m_page*CardCount+i;const bool visible=!m_inspecting&&index<int(m_options.size());
        m_cards[i].SetVisibility(visible);m_cardIds[i]=index<int(m_options.size())?m_options[index]:-1;
        if(m_cardIds[i]<0)continue;
        const int id=m_cardIds[i];m_cards[i].title=FinishName(id);m_cards[i].collection=CollectionName(id);
        m_cards[i].equipped=IsEquipped(id);m_cards[i].selected=id==m_selection;Preview(m_cards[i].model,id);
    }
    for(int i=0;i<CategoryCount;++i)m_categories[i].colorBase=m_category==i?Accent():Ink();
    m_previous.SetGrayed(!m_page);m_next.SetGrayed(m_page+1>=Pages());SyncInspect();
}
void CMenuPanoramaInventory::SetCategory(int category)
{
    m_category=category;m_wheelPage=m_page=0;m_search.SetBuffer("");m_query.clear();
    if(category<Knife&&WeaponCategory(m_weapon)!=category)
        for(int weapon:oc2_accessories::kWeaponIds)if(WeaponCategory(weapon)==category){m_weapon=weapon;break;}
    Reload();m_selection=SavedSkin();BuildWheel();RefreshCards();ToggleInspect(false);
}
void CMenuPanoramaInventory::Category(CMenuBaseItem *button)
{for(int i=0;i<CategoryCount;++i)if(button==&m_categories[i]){SetCategory(i);return;}}
void CMenuPanoramaInventory::ChooseWheelKey(int key)
{
    if(m_category<Knife)m_weapon=key;else if(m_category==Knife)m_definition=key;else m_family=key;
    m_page=0;m_selection=SavedSkin();m_search.SetBuffer("");m_query.clear();RefreshCards();
}
void CMenuPanoramaInventory::WheelSelected()
{if(m_wheel.selected>=0&&m_wheel.selected<int(m_wheel.entries.size()))ChooseWheelKey(m_wheel.entries[m_wheel.selected].key);}
void CMenuPanoramaInventory::PreviousWheel()
{if(m_wheelPage>0){--m_wheelPage;BuildWheel();if(!m_wheel.entries.empty())ChooseWheelKey(m_wheel.entries[0].key);}}
void CMenuPanoramaInventory::NextWheel()
{if(m_wheelPage+1<WheelPages()){++m_wheelPage;BuildWheel();if(!m_wheel.entries.empty())ChooseWheelKey(m_wheel.entries[0].key);}}
void CMenuPanoramaInventory::Previous(){if(m_page>0)--m_page;RefreshCards();}
void CMenuPanoramaInventory::Next(){if(m_page+1<Pages())++m_page;RefreshCards();}
void CMenuPanoramaInventory::SelectCard(CMenuBaseItem *button)
{
    for(int i=0;i<CardCount;++i)if(button==&m_cards[i]&&m_cardIds[i]>=0)
    {m_selection=m_cardIds[i];InspectSelected();return;}
}
void CMenuPanoramaInventory::SyncInspect()
{
    const bool valid=m_category==Glove?(m_gloveCatalogValid&&m_gloveConfigValid):(m_catalogValid&&m_configValid);
    m_equip.SetGrayed(!valid);m_equip.SetText(IsEquipped(m_selection)?u8"\u5df2\u88c5\u5907":u8"\u88c5\u5907\u5230\u53cc\u65b9\u9635\u8425");
    const auto *weapon=oc2_inventory::Find(m_config,m_weapon);
    for(int i=0;i<4;++i)
    {
        const int id=weapon?weapon->stickers[i]:0;char line[160];
        const std::string name=id<0?u8"\u672a\u5b89\u88c5":id?oc2_local_inventory::SkinName(m_catalog,id):u8"\u539f\u8d34\u7eb8";
        std::snprintf(line,sizeof(line),u8"\u8d34\u7eb8 %c   %s",'A'+i,name.c_str());m_stickers[i].SetText(line);
    }
}
void CMenuPanoramaInventory::ToggleInspect(bool inspect)
{
    m_inspecting=inspect;
    for(int i=0;i<CategoryCount;++i)m_categories[i].SetVisibility(!inspect);
    m_wheel.SetVisibility(!inspect);m_search.SetVisibility(!inspect);m_previous.SetVisibility(!inspect);m_next.SetVisibility(!inspect);
    m_wheelPrevious.SetVisibility(!inspect&&WheelPages()>1);m_wheelNext.SetVisibility(!inspect&&WheelPages()>1);m_refresh.SetVisibility(!inspect);
    for(int i=0;i<CardCount;++i)m_cards[i].SetVisibility(!inspect&&m_cardIds[i]>=0);
    m_inspect.SetVisibility(inspect);m_closeInspect.SetVisibility(inspect);m_equip.SetVisibility(inspect);
    m_patterns.SetVisibility(inspect&&Gun());m_accessories.SetVisibility(inspect&&Gun());
    for(auto &button:m_stickers)button.SetVisibility(inspect&&Gun());
}
void CMenuPanoramaInventory::InspectSelected(){Preview(m_inspect,m_selection);SyncInspect();ToggleInspect(true);SetCursorToItem(m_equip);}
void CMenuPanoramaInventory::CloseInspect(){ToggleInspect(false);RefreshCards();SetCursorToItem(m_cards[0]);}
void CMenuPanoramaInventory::Refresh(){Reload();m_selection=SavedSkin();BuildWheel();RefreshCards();if(m_inspecting)Preview(m_inspect,m_selection);}

void CMenuPanoramaInventory::Equip()
{
    std::string error;
    if(m_category==Glove)
    {
        if(!m_gloveCatalogValid||!m_gloveConfigValid)return;
        oc2_gloves::Config latest;
        if(!oc2_local_gloves::LoadConfig(latest,error)||!oc2_gloves::ValidateConfig(latest,m_gloveCatalog,error))
        {m_error=true;m_message=error;return;}
        oc2_gloves::Equip(latest,m_selection);latest.revision=latest.revision==std::numeric_limits<unsigned>::max()?1:latest.revision+1;
        if(!oc2_local_gloves::SaveConfig(latest)){m_error=true;m_message="Gloves could not be saved.";return;}
        m_gloveConfig=latest;
    }
    else
    {
        if(!m_catalogValid||!m_configValid)return;
        oc2_inventory::Config latest;
        if(!oc2_local_inventory::LoadConfig(latest,error)||!oc2_inventory::ValidateConfig(latest,m_catalog,error))
        {m_error=true;m_message=error;return;}
        if(m_category==Knife)
        {
            if(!oc2_inventory::EquipKnife(latest,m_definition))return;
            if(m_definition){auto *knife=oc2_inventory::FindKnife(latest,m_definition);if(knife)knife->skin=m_selection;}
        }
        else {auto *weapon=oc2_inventory::Find(latest,m_weapon);if(!weapon)return;weapon->skin=m_selection;}
        latest.revision=latest.revision==std::numeric_limits<unsigned>::max()?1:latest.revision+1;
        if(!oc2_inventory::ValidateConfig(latest,m_catalog,error)){m_error=true;m_message=error;return;}
        if(!oc2_local_inventory::SaveConfig(latest)){m_error=true;m_message="Equipment could not be saved.";return;}
        m_config=latest;
    }
    m_error=false;m_message=u8"\u5df2\u4e3a\u53cc\u65b9\u9635\u8425\u88c5\u5907\u3002";RefreshCards();m_inspect.ReloadConfiguration();Preview(m_inspect,m_selection);
}
void CMenuPanoramaInventory::OpenPatterns()
{if(Gun()&&m_skins){m_returnEditor=true;m_skins->OpenWeapon(m_weapon);}}
void CMenuPanoramaInventory::OpenSticker(CMenuBaseItem *button)
{
    if(!Gun()||!m_skins)return;
    for(int i=0;i<4;++i)if(button==&m_stickers[i]){m_returnEditor=true;m_skins->OpenSticker(m_weapon,i);return;}
}
void CMenuPanoramaInventory::OpenAccessories()
{if(Gun()&&m_accessoryEditor){m_returnEditor=true;m_accessoryEditor->Open(m_weapon);}}
void CMenuPanoramaInventory::Back(){if(m_inspecting)CloseInspect();else UI_PanoramaHome();}
bool CMenuPanoramaInventory::KeyDown(int key)
{
    if(UI::Key::IsEscape(key)){Back();return true;}
    if(m_inspecting&&UI_CursorInRect(m_inspect.GetRenderPosition(),m_inspect.GetRenderSize())&&m_inspect.KeyDown(key))return true;
    return CMenuFramework::KeyDown(key);
}
bool CMenuPanoramaInventory::KeyUp(int key)
{if(m_inspecting&&m_inspect.KeyUp(key))return true;return CMenuFramework::KeyUp(key);}
bool CMenuPanoramaInventory::MouseMove(int x,int y)
{
    if(m_inspecting&&!UI_CursorInRect(m_inspect.GetRenderPosition(),m_inspect.GetRenderSize()))m_inspect.MouseMove(x,y);
    return CMenuFramework::MouseMove(x,y);
}
void CMenuPanoramaInventory::Think()
{
    if(m_returnEditor&&WindowStack()->Current()==this)
    {
        m_returnEditor=false;Reload();RefreshCards();m_inspect.ReloadConfiguration();Preview(m_inspect,m_selection);
    }
    const std::string query=oc2_local_inventory::Fold(m_search.GetBuffer());
    if(!m_inspecting&&query!=m_query){m_query=query;m_page=0;RefreshCards();}
    CMenuFramework::Think();
}

void CMenuPanoramaInventory::Draw()
{
    if(CL_IsActive())return;
    const int w=uiStatic.width,right=Right(),rw=w-right-34;
    oc2_panorama::DrawBackground();
    Fill(64,0,w-64,96,PackRGBA(12,18,25,80));Fill(98,95,w-132,1,PackRGBA(190,206,218,80));
    Text(98,30,w-140,42,m_inspecting?u8"\u68c0\u89c6\u7269\u54c1":u8"\u5e93\u5b58",28,Ink(),true);
    Fill(64,712,w-64,56,PackRGBA(9,14,20,125));
    if(!m_inspecting)
    {
        Fill(right-14,163,rw+28,552,PackRGBA(19,29,38,150));
        Text(98,165,right-132,25,u8"\u9009\u62e9\u6b66\u5668",13,Ink(),true);
        Text(right,171,rw,32,ItemName().c_str(),24,Ink(),true);
        Text(right,207,rw,18,u8"\u641c\u7d22\u76ae\u80a4\u6216\u6536\u85cf",11,Ink());
        char count[160];std::snprintf(count,sizeof(count),u8"%u \u6b3e\u76ae\u80a4  /  %d - %d  /  \u7b2c %d / %d \u9875",unsigned(m_options.size()),
            m_options.empty()?0:m_page*CardCount+1,Q_min((m_page+1)*CardCount,int(m_options.size())),m_page+1,Pages());
        Text(right+54,689,rw-108,22,count,12,Muted(),false,QM_CENTER);
        Text(174,691,right-272,18,u8"\u70b9\u51fb\u76ae\u80a4\u8fdb\u884c\u4e09\u7ef4\u68c0\u89c6\u4e0e\u88c5\u5907",11,Ink(),false,QM_CENTER);
        if(m_options.empty())Text(right+20,385,rw-40,56,u8"\u6ca1\u6709\u5339\u914d\u7684\u76ae\u80a4",22,Muted(),false,QM_CENTER);
    }
    else
    {
        const int side=w*73/100,sw=w-side-40;
        Text(312,113,w-354,38,ItemName().c_str(),25,Ink(),true);
        Fill(side-18,173,sw+36,505,PackRGBA(18,28,37,220));
        Text(side,188,sw,44,FinishName(m_selection).c_str(),22,Ink(),true);
        Text(side,233,sw,23,CollectionName(m_selection).c_str(),12,Muted());
        Text(104,669,w*73/100-130,23,u8"\u62d6\u52a8\u65cb\u8f6c     \u6eda\u8f6e\u7f29\u653e     R \u91cd\u7f6e\u89c6\u89d2",13,Ink(),false,QM_CENTER);
        if(Gun())Text(side,328,sw,22,u8"\u56db\u4e2a\u8d34\u7eb8\u69fd",12,Accent(),true);
        else Text(side,345,sw,110,m_category==Knife?u8"\u5f53\u524d\u5200\u578b\u4e0e\u76ae\u80a4\u7531\u53cc\u65b9\u9635\u8425\u5171\u7528\u3002":
            u8"\u624b\u5957\u4f5c\u4e3a\u73a9\u5bb6\u88c5\u5907\uff0c\u53cc\u65b9\u9635\u8425\u5171\u7528\u3002",16,Muted());
    }
    if(!m_message.empty())Text(250,724,w-450,26,m_message.c_str(),13,m_error?PackRGBA(246,133,119,255):Accent());
    Text(w-170,725,134,24,u8"ESC  \u8fd4\u56de",13,Muted(),false,QM_RIGHT);
    m_chrome.Draw();CMenuFramework::Draw();
}
void CMenuPanoramaInventory::Dump()
{
    m_inspect.Dump("inventory_inspect");
    m_cards[0].model.Dump("inventory_card0");
    Con_Printf("panorama_inventory: visible=%d inspecting=%d category=%d weapon=%d definition=%d family=%d selection=%d equipped=%d options=%u page=%d pages=%d wheel_page=%d wheel_pages=%d catalog_valid=%d config_valid=%d source=local_configuration\n",
        int(IsVisible()),int(m_inspecting),m_category,m_weapon,m_definition,m_family,m_selection,int(IsEquipped(m_selection)),unsigned(m_options.size()),
        m_page,Pages(),m_wheelPage,WheelPages(),int(m_category==Glove?m_gloveCatalogValid:m_catalogValid),int(m_category==Glove?m_gloveConfigValid:m_configValid));
    for(int i=0;i<int(m_wheel.entries.size());++i)
    {
        const Point center=m_wheel.SlotCenter(i);Con_Printf("panorama_wheel: slot=%d key=%d name=%s x=%d y=%d selected=%d units=screen_pixels\n",
            i,m_wheel.entries[i].key,m_wheel.entries[i].name.c_str(),center.x,center.y,int(m_wheel.selected==i));
    }
    for(int i=0;i<CardCount;++i)if(m_cardIds[i]>=0)Con_Printf("panorama_finish: card=%d id=%d name=%s equipped=%d preview=studio_model\n",
        i,m_cardIds[i],m_cards[i].title.c_str(),int(m_cards[i].equipped));
    for(int i=0;i<ItemCount();++i)
    {
        CMenuBaseItem *item=GetItemByIndex(i);if(!item||!item->szTag)continue;
        const Point p=item->GetRenderPosition();const Size z=item->GetRenderSize();
        Con_Printf("frontend_item: tag=%s x=%d y=%d w=%d h=%d visible=%d enabled=%d focus=%d units=screen_pixels\n",
            item->szTag,p.x,p.y,z.w,z.h,int(IsVisible()&&item->IsVisible()),int(!(item->iFlags&(QMF_GRAYED|QMF_INACTIVE))),int(item->IsCurrentSelected()));
    }
}
