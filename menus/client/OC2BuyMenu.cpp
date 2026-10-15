#include "BaseMenu.h"
#include "ClientWindow.h"
#include "BuyModelPreview.h"
#include "cl_dll/IOc2BuyMenu.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern IOc2BuyMenu *g_pOc2BuyMenu;

namespace
{
struct Rect
{
	int x,y,w,h;
	bool Contains(int px,int py) const { return px>=x && py>=y && px<x+w && py<y+h; }
};

Rect Area(float x,float y,float w,float h)
{
	return {int(x*ScreenWidth),int(y*ScreenHeight),int(w*ScreenWidth),int(h*ScreenHeight)};
}

const float columnX[] = {.02f,.13f,.24f,.39f,.55f};
const float columnW[] = {.10f,.10f,.14f,.15f,.10f};
const char *columnName[] = {"EQUIPMENT","PISTOLS","MID-TIER","RIFLES","GRENADES"};

class COc2BuyMenu : public CClientWindow
{
public:
	COc2BuyMenu() : CClientWindow("OC2BuyMenu") { bAllowDrag=false; }
	void VidInit() override
	{
		pos=Point(0,0); size=Size(int(ScreenWidth/uiStatic.scaleX),int(ScreenHeight/uiStatic.scaleY));
		CMenuItemsHolder::VidInit();
		pictures.clear();
		if (fontScreenHeight!=ScreenHeight) { fonts.clear(); fontScreenHeight=ScreenHeight; }
	}
	void Show() override
	{
		if (IsVisible()) return;
		selection=-1; hover=-1; pressed=-1; enterHeld=false;
		for (int &p:page) p=0;
		Refresh();
		for (int col=0; col<5 && selection<0; ++col)
			if (!columns[col].empty()) selection=columns[col][0];
		CClientWindow::Show(); DisableTransition();
		savedViewModel=EngFuncs::GetCvarFloat("r_drawviewmodel");
		viewModelHidden=true;
		EngFuncs::CvarSetValue("r_drawviewmodel",0);
		g_pOc2BuyMenu->SetOpen(true);
		Con_Printf("buymenu_ui: open team=%d selection=%d\n",state.team,selection);
	}
	void Hide() override
	{
		if (g_pOc2BuyMenu) g_pOc2BuyMenu->SetOpen(false);
		UI_CloseClientMenu(); DisableTransition();
		pressed=-1; enterHeld=false;
		Con_Printf("buymenu_ui: closed\n");
	}
	void Draw() override
	{
		if (!Refresh() || !state.canBuy) { Hide(); return; }
		// APK touch/buy_[t|ct].cfg gives the normalized five-column layout.
		// CS:GO RefreshItems supplies the model: actual price, eligibility and
		// owned state; the server alone applies a successful purchase.
		Fill(Area(0,0,1,1),PackRGBA(12,15,18,100));
		oc2_buy::Item selected;
		bool hasSelection=selection>=0 && g_pOc2BuyMenu->GetItem(selection,selected) && selected.visible;
		if (!OC2_DrawBuyPreview(state.team,hasSelection?selected.id:0,
			int(ScreenWidth*.67f),int(ScreenHeight*.12f),int(ScreenWidth*.33f),int(ScreenHeight*.88f)))
			Text("COUNTER-STRIKE",Area(.70f,.45f,.27f,.07f),28,PackRGBA(220,220,220,255),QM_CENTER);

		Fill(Area(.01f,.133333f,.65f,.711111f),PackRGBA(0,0,0,100));
		char info[128];
		std::snprintf(info,sizeof(info),"%s   $%d",state.team==2?"COUNTER-TERRORISTS":"TERRORISTS",state.money);
		Text(info,Area(.02f,.065f,.43f,.04f),28,PackRGBA(255,255,255,255));
		if (state.seconds>=0) std::snprintf(info,sizeof(info),"BUY TIME  %d:%02d",state.seconds/60,state.seconds%60);
		else std::snprintf(info,sizeof(info),"%s",state.seconds==-1?"BUY TIME  UNLIMITED":"BUY MENU");
		Text(info,Area(.44f,.065f,.21f,.04f),20,PackRGBA(220,220,220,255),QM_RIGHT);

		for (int col=0;col<5;++col)
		{
			char heading[64];
			int pages=PageCount(col);
			std::snprintf(heading,sizeof(heading),pages>1?"%s  %d/%d":"%s",columnName[col],page[col]+1,pages);
			Text(heading,Area(columnX[col],.115f,columnW[col],.035f),18,PackRGBA(230,230,230,255));
			for (int row=0;row<5;++row)
			{
				int offset=page[col]*5+row;
				if (offset>=int(columns[col].size())) break;
				int index=columns[col][offset];
				oc2_buy::Item item;
				if (!g_pOc2BuyMenu->GetItem(index,item)) continue;
				DrawCard(index,item,Card(col,row));
			}
			if (pages>1)
				Text("<   SCROLL / CLICK   >",Area(columnX[col],.845f,columnW[col],.028f),15,
					PackRGBA(215,215,215,255),QM_CENTER);
		}

		Fill(Area(.43f,.866667f,.23f,.111111f),PackRGBA(0,0,0,110));
		DrawButton(-2,"gfx/weaponbuy/close.tga",Area(.44f,.888889f,.10f,.066667f),true);
		DrawButton(-3,"gfx/weaponbuy/buy.tga",Area(.55f,.888889f,.10f,.066667f),hasSelection&&selected.enabled);
		if (hasSelection)
		{
			std::snprintf(info,sizeof(info),"%s   $%d",selected.name,selected.price);
			Text(info,Area(.02f,.89f,.39f,.035f),22,PackRGBA(255,255,255,255));
			Text(selected.enabled?"Click BUY or press Enter":selected.reason,Area(.02f,.93f,.39f,.028f),18,
				selected.enabled?PackRGBA(210,210,210,255):PackRGBA(230,170,110,255));
		}
	}
	bool MouseMove(int x,int y) override
	{
		hover=Hit(x,y); return true;
	}
	bool KeyDown(int key) override
	{
		if (UI::Key::IsEscape(key)) { Hide(); return true; }
		if (key=='b' || key=='B') return true; // close once, on physical release
		if (UI::Key::IsConsole(key)) return CClientWindow::KeyDown(key);
		if (key==K_MOUSE1) { pressed=Hit(uiStatic.cursorX,uiStatic.cursorY); return true; }
		if (key==K_ENTER || key==K_KP_ENTER) { if(!enterHeld) { enterHeld=true; Confirm(); } return true; }
		if (key==K_MWHEELUP || key==K_MWHEELDOWN)
		{
			for (int col=0;col<5;++col)
				if (Area(columnX[col],.11f,columnW[col],.77f).Contains(uiStatic.cursorX,uiStatic.cursorY))
					ChangePage(col,key==K_MWHEELUP?-1:1);
			return true;
		}
		if (key>= '1' && key<='5')
		{
			int col=key-'1';
			if (!columns[col].empty()) selection=columns[col][page[col]*5];
			return true;
		}
		if (key==K_UPARROW || key==K_DOWNARROW || key==K_LEFTARROW || key==K_RIGHTARROW)
		{
			Navigate(key); return true;
		}
		return true;
	}
	bool KeyUp(int key) override
	{
		if (key==K_ENTER || key==K_KP_ENTER) enterHeld=false;
		if (key=='b' || key=='B') { Hide(); return true; }
		if (key==K_MOUSE1)
		{
			int hit=Hit(uiStatic.cursorX,uiStatic.cursorY);
			if (hit==pressed)
			{
				if (hit>=0) Select(hit);
				else if (hit==-2) Hide();
				else if (hit==-3) Confirm();
				else if (hit<=-10 && hit>=-14) ChangePage(-10-hit,1);
			}
			pressed=-1;
		}
		return true;
	}
	void SelectId(int id)
	{
		if (!IsVisible() || !Refresh()) return;
		for (int col=0;col<5;++col)
			for (int row=0;row<int(columns[col].size());++row)
			{
				oc2_buy::Item item;
				if (g_pOc2BuyMenu->GetItem(columns[col][row],item) && item.id==id)
				{ page[col]=row/5; Select(columns[col][row]); return; }
			}
	}
	void Confirm()
	{
		if (!IsVisible() || !Refresh() || !state.canBuy || selection<0) return;
		oc2_buy::Item item;
		if (!g_pOc2BuyMenu->GetItem(selection,item) || !item.visible || !item.enabled) return;
		bool sent=g_pOc2BuyMenu->Purchase(selection);
		Con_Printf("buymenu_ui: confirm id=%d requested=%d money=%d\n",item.id,int(sent),state.money);
	}
	void DebugKey(int key,bool down) { if (IsVisible()) { if(down) KeyDown(key); else KeyUp(key); } }
	void RestoreViewModel()
	{
		if (viewModelHidden) EngFuncs::CvarSetValue("r_drawviewmodel",savedViewModel);
		viewModelHidden=false;
	}

private:
	oc2_buy::State state;
	std::vector<int> columns[5];
	int page[5]={},selection=-1,hover=-1,pressed=-1;
	bool enterHeld=false;
	bool viewModelHidden=false;
	float savedViewModel=1;
	std::map<std::string,HIMAGE> pictures;
	std::map<int,HFont> fonts;
	int fontScreenHeight=0;
	void _Init() override {}
	bool Refresh()
	{
		if (!g_pOc2BuyMenu || !g_pOc2BuyMenu->GetState(state)) return false;
		for (auto &column:columns) column.clear();
		for (int i=0;i<g_pOc2BuyMenu->ItemCount();++i)
		{
			oc2_buy::Item item;
			if (g_pOc2BuyMenu->GetItem(i,item) && item.visible && item.column>=0 && item.column<5)
				columns[item.column].push_back(i);
		}
		for (int col=0;col<5;++col) page[col]=std::min(page[col],PageCount(col)-1);
		return true;
	}
	int PageCount(int col) const { return std::max(1,int(columns[col].size()+4)/5); }
	Rect Card(int col,int row) const { return Area(columnX[col],.155556f+row*.133333f,columnW[col],.133333f); }
	void Fill(Rect rect,unsigned int color) { UI_FillRect(rect.x,rect.y,rect.w,rect.h,color); }
	void Text(const char *text,Rect rect,int height,unsigned int color,uint align=QM_LEFT)
	{
		const int pixels=std::max(10,int(height*ScreenHeight/1080.0f));
		if (!fonts.count(pixels) || g_FontMgr->GetFontTall(fonts[pixels])<=0)
			fonts[pixels]=CFontBuilder("Trebuchet MS",pixels,400).Create();
		UI_DrawString(fonts[pixels],rect.x,rect.y,rect.w,rect.h,text,color,pixels,align,ETF_NO_WRAP);
	}
	HIMAGE Picture(const char *name)
	{
		if (!name || !name[0]) return 0;
		auto it=pictures.find(name);
		if (it!=pictures.end()) return it->second;
		return pictures[name]=EngFuncs::PIC_Load(name);
	}
	void DrawCard(int index,const oc2_buy::Item &item,Rect r)
	{
		Fill(r,PackRGBA(5,5,5,245));
		HIMAGE pic=Picture(item.image);
		int tint=item.enabled?255:125;
		if (pic)
		{
			// Names/prices in the APK are baked CS2 data. Crop them out;
			// preserve the original central silhouette and radial background.
			int tw=EngFuncs::PIC_Width(pic),th=EngFuncs::PIC_Height(pic);
			wrect_t crop={0,tw,int(th*.19f),int(th*.82f)};
			EngFuncs::PIC_Set(pic,tint,tint,tint,255);
			EngFuncs::PIC_DrawTrans(r.x,r.y+int(r.h*.19f),r.w,int(r.h*.63f),&crop);
		}
		else
		{
			const char *fallback=item.id==1004?"gfx/vgui/nightvision.tga":item.id==99?"gfx/vgui/shield.tga":
				(item.id==1005 || item.id==1006)?"gfx/vgui/sammo.tga":"";
			HIMAGE icon=Picture(fallback);
			if (icon)
			{
				const float fit=std::min(r.w*.65f/std::max(1,EngFuncs::PIC_Width(icon)),r.h*.55f/std::max(1,EngFuncs::PIC_Height(icon)));
				const int w=int(EngFuncs::PIC_Width(icon)*fit),h=int(EngFuncs::PIC_Height(icon)*fit);
				EngFuncs::PIC_Set(icon,tint,tint,tint,255);
				EngFuncs::PIC_DrawTrans(r.x+(r.w-w)/2,r.y+(r.h-h)/2,w,h);
			}
		}
		unsigned int textColor=PackRGBA(tint,tint,tint,255);
		int pad=std::max(5,int(ScreenHeight/135.0f));
		Text(item.name,{r.x+pad,r.y+2,r.w-pad*2,int(r.h*.19f)},18,textColor,QM_RIGHT);
		char cost[32]; std::snprintf(cost,sizeof(cost),"$%d",item.price);
		Text(cost,{r.x+pad,r.y+int(r.h*.81f),r.w-pad*2,int(r.h*.18f)},18,textColor,QM_RIGHT);
		if (item.owned || item.count>0)
		{
			char owned[32];
			if (item.limit>1) std::snprintf(owned,sizeof(owned),"%d / %d",item.count,item.limit);
			else std::snprintf(owned,sizeof(owned),"OWNED");
			Text(owned,{r.x+pad,r.y+int(r.h*.81f),int(r.w*.65f),int(r.h*.18f)},14,PackRGBA(225,198,126,255));
		}
		if (selection==index || hover==index)
			UI_DrawRectangleExt(r.x,r.y,r.w,r.h,selection==index?PackRGBA(237,199,97,255):PackRGBA(210,210,210,210),std::max(1,int(ScreenHeight/540.0f)));
	}
	void DrawButton(int id,const char *name,Rect r,bool enabled)
	{
		HIMAGE pic=Picture(name); int value=enabled?255:100;
		Fill(r,PackRGBA(0,0,0,230));
		if (pic) { EngFuncs::PIC_Set(pic,value,value,value,255); EngFuncs::PIC_DrawTrans(r.x,r.y,r.w,r.h); }
		else Text(id==-2?"CLOSE":"BUY",r,28,PackRGBA(value,value,value,255),QM_CENTER);
		if (hover==id && enabled) UI_DrawRectangleExt(r.x,r.y,r.w,r.h,PackRGBA(237,199,97,255),2);
	}
	int Hit(int x,int y) const
	{
		if (Area(.44f,.888889f,.10f,.066667f).Contains(x,y)) return -2;
		if (Area(.55f,.888889f,.10f,.066667f).Contains(x,y)) return -3;
		for (int col=0;col<5;++col)
		{
			if (PageCount(col)>1 && Area(columnX[col],.845f,columnW[col],.028f).Contains(x,y)) return -10-col;
			for (int row=0;row<5;++row)
				if (page[col]*5+row<int(columns[col].size()) && Card(col,row).Contains(x,y)) return columns[col][page[col]*5+row];
		}
		return -1;
	}
	void Select(int index)
	{
		selection=index;
		oc2_buy::Item item;
		if (g_pOc2BuyMenu->GetItem(index,item)) Con_Printf("buymenu_ui: select id=%d enabled=%d price=%d\n",item.id,int(item.enabled),item.price);
	}
	void ChangePage(int col,int delta)
	{
		page[col]=(page[col]+delta+PageCount(col))%PageCount(col);
		hover=Hit(uiStatic.cursorX,uiStatic.cursorY);
	}
	void Navigate(int key)
	{
		for (int col=0;col<5;++col)
			for (int row=0;row<int(columns[col].size());++row)
				if (columns[col][row]==selection)
				{
					int nextCol=col,nextRow=row;
					if (key==K_LEFTARROW) nextCol=std::max(0,col-1);
					if (key==K_RIGHTARROW) nextCol=std::min(4,col+1);
					if (key==K_UPARROW) --nextRow;
					if (key==K_DOWNARROW) ++nextRow;
					if (!columns[nextCol].empty())
					{
						nextRow=std::max(0,std::min(nextRow,int(columns[nextCol].size())-1));
						page[nextCol]=nextRow/5; Select(columns[nextCol][nextRow]);
					}
					return;
				}
	}
};

COc2BuyMenu menu;
}

bool UI_OC2BuyMenu_Show()
{
	if (!g_pOc2BuyMenu) return false;
	oc2_buy::State state;
	if (g_pOc2BuyMenu->GetState(state) && state.canBuy && !menu.IsVisible())
		menu.Show();
	return true;
}

void UI_OC2BuyMenu_ClearState()
{
	menu.RestoreViewModel();
	if (g_pOc2BuyMenu) g_pOc2BuyMenu->SetOpen(false);
}

void UI_OC2BuyMenu_Init()
{
	// Diagnostic commands exercise the same selection/confirmation handlers as
	// mouse and keyboard. They cannot open a menu or bypass purchase eligibility.
	EngFuncs::Cmd_AddCommand("buy_menu_select",[](){ if(EngFuncs::CmdArgc()==2) menu.SelectId(std::atoi(EngFuncs::CmdArgv(1))); });
	EngFuncs::Cmd_AddCommand("buy_menu_confirm",[](){ menu.Confirm(); });
	EngFuncs::Cmd_AddCommand("buy_menu_close",[](){ if(menu.IsVisible()) menu.Hide(); });
	EngFuncs::Cmd_AddCommand("buy_menu_key",[](){ if(EngFuncs::CmdArgc()==3) menu.DebugKey(std::atoi(EngFuncs::CmdArgv(1)),std::atoi(EngFuncs::CmdArgv(2))!=0); });
}
