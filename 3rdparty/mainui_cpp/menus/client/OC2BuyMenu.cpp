#include "BaseMenu.h"
#include "ClientWindow.h"
#include "BuyModelPreview.h"
#include "BuyScroll.h"
#include "Scissor.h"
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
	bool Intersects(const Rect &other) const { return x<other.x+other.w && x+w>other.x && y<other.y+other.h && y+h>other.y; }
};

Rect Area(float x,float y,float w,float h)
{
	return {int(x*ScreenWidth),int(y*ScreenHeight),int(w*ScreenWidth),int(h*ScreenHeight)};
}

const float columnX[] = {.02f,.13f,.24f,.39f,.55f};
const float columnW[] = {.10f,.10f,.14f,.15f,.10f};
const char *columnName[] = {"装备","手枪","微冲 / 重型","步枪","投掷物"};

class COc2BuyMenu : public CClientWindow
{
public:
	COc2BuyMenu() : CClientWindow("OC2BuyMenu") { bAllowDrag=false; }
	void Precache()
	{
		if (!g_pOc2BuyMenu) return;
		for(int height:{14,18,20,22,28}) Font(height);
		for(int i=0;i<g_pOc2BuyMenu->ItemCount();++i)
		{
			oc2_buy::Item item;
			if(g_pOc2BuyMenu->GetItem(i,item)) Picture(item.image);
		}
		for(const char *path:{"gfx/weaponbuy/incendiary.tga","gfx/vgui/nightvision.tga","gfx/vgui/shield.tga","gfx/vgui/sammo.tga"})Picture(path);
	}
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
		for (auto &column:scroll) column=oc2_buy_scroll::State();
		lastScrollTime=uiStatic.realTime;
		Refresh();
		for (int col=0; col<5 && selection<0; ++col)
			if (!columns[col].empty()) selection=columns[col][0];
		CClientWindow::Show(); DisableTransition();
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
		AdvanceScroll();
		// APK touch/buy_[t|ct].cfg gives the normalized five-column layout.
		// CS:GO RefreshItems supplies the model: actual price, eligibility and
		// owned state; the server alone applies a successful purchase.
		Fill(Area(0,0,1,1),PackRGBA(12,15,18,100));
		oc2_buy::Item selected;
		bool hasSelection=selection>=0 && g_pOc2BuyMenu->GetItem(selection,selected) && selected.visible;
		if (!OC2_DrawBuyPreview(state.team,hasSelection?selected.id:0,
			// Full-height actor framing from the reference. The main world pass
			// still renders its viewmodel; this scene adds only the preview actor.
			int(ScreenWidth*.67f),int(ScreenHeight*.12f),int(ScreenWidth*.33f),int(ScreenHeight*.88f)))
			Text("反恐精英",Area(.70f,.45f,.27f,.07f),28,PackRGBA(220,220,220,255),QM_CENTER);

		Fill(Area(.01f,.133333f,.65f,.711111f),PackRGBA(0,0,0,100));
		char info[128];
		std::snprintf(info,sizeof(info),"%s   $%d",state.team==2?"反恐精英":"恐怖分子",state.money);
		Text(info,Area(.02f,.065f,.43f,.04f),28,PackRGBA(255,255,255,255));
		if (state.seconds>=0) std::snprintf(info,sizeof(info),"购买时间  %d:%02d",state.seconds/60,state.seconds%60);
		else std::snprintf(info,sizeof(info),"%s",state.seconds==-1?"购买时间不限":"购买菜单");
		Text(info,Area(.44f,.065f,.21f,.04f),20,PackRGBA(220,220,220,255),QM_CENTER);

		for (int col=0;col<5;++col)
		{
			Text(columnName[col],Area(columnX[col],.115f,columnW[col],.035f),18,PackRGBA(230,230,230,255));
			const Rect viewport=Viewport(col);
			UI::Scissor::PushScissor(viewport.x,viewport.y,viewport.w,viewport.h);
			for (int row=0;row<int(columns[col].size());++row)
			{
				const Rect card=Card(col,row);
				if (!card.Intersects(viewport)) continue;
				int index=columns[col][row];
				oc2_buy::Item item;
				if (!g_pOc2BuyMenu->GetItem(index,item)) continue;
				DrawCard(item,card,viewport);
			}
			// Draw outlines after every card, inside their own rectangles. The old
			// external outline was overwritten by the next card's background.
			for (int row=0;row<int(columns[col].size());++row)
			{
				const int index=columns[col][row];
				const Rect card=Card(col,row);
				if ((selection==index || hover==index) && card.Intersects(viewport))
					Outline(card,selection==index?PackRGBA(237,199,97,255):PackRGBA(210,210,210,210),viewport);
			}
			UI::Scissor::PopScissor();
			DrawScrollHint(col);
		}

		Fill(Area(.43f,.866667f,.23f,.111111f),PackRGBA(0,0,0,110));
		DrawButton(-2,Area(.44f,.888889f,.10f,.066667f),true);
		DrawButton(-3,Area(.55f,.888889f,.10f,.066667f),hasSelection&&selected.enabled);
		if (hasSelection)
		{
			std::snprintf(info,sizeof(info),"%s   $%d",selected.name,selected.price);
			Text(info,Area(.02f,.89f,.39f,.035f),22,PackRGBA(255,255,255,255));
			Text(selected.enabled?"点击购买或按回车键":selected.reason,Area(.02f,.93f,.39f,.028f),18,
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
					ScrollColumn(col,key==K_MWHEELUP?-1.0f:1.0f);
			return true;
		}
		if (key>= '1' && key<='5')
		{
			int col=key-'1';
			if (!columns[col].empty())
			{
				const int row=std::min(int(columns[col].size())-1,int(std::ceil(scroll[col].position)));
				Select(columns[col][row]); scroll[col].Reveal(row,int(columns[col].size()));
			}
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
				{ scroll[col].Reveal(row,int(columns[col].size())); Select(columns[col][row]); return; }
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
	void DebugScroll(int col,float notches) { if(IsVisible() && Refresh()) ScrollColumn(col,notches); }
	void DebugDump() const
	{
		if (!IsVisible()) return;
		Con_Printf("buymenu_scroll: time=%d selection=%d hover=%d cursor=%d,%d hit=%d\n",uiStatic.realTime,selection,hover,uiStatic.cursorX,uiStatic.cursorY,Hit(uiStatic.cursorX,uiStatic.cursorY));
		for (int col=0;col<5;++col)
		{
			const Rect viewport=Viewport(col);
			Con_Printf("buymenu_scroll: col=%d count=%d offset=%.5f target=%.5f max=%.5f viewport=%d,%d,%d,%d\n",
				col,int(columns[col].size()),scroll[col].position,scroll[col].target,
				oc2_buy_scroll::State::Maximum(int(columns[col].size())),viewport.x,viewport.y,viewport.w,viewport.h);
			for(int row=0;row<int(columns[col].size());++row)
			{
				const Rect card=Card(col,row);
				if (!card.Intersects(viewport)) continue;
				const int top=std::max(card.y,viewport.y),bottom=std::min(card.y+card.h,viewport.y+viewport.h);
				Con_Printf("buymenu_scroll_card: col=%d row=%d index=%d rect=%d,%d,%d,%d visible=%d,%d centerhit=%d selected=%d\n",
					col,row,columns[col][row],card.x,card.y,card.w,card.h,top,bottom,
					Hit(card.x+card.w/2,(top+bottom)/2),int(selection==columns[col][row]));
			}
		}
	}
private:
	oc2_buy::State state;
	std::vector<int> columns[5];
	oc2_buy_scroll::State scroll[5];
	int lastScrollTime=0;
	int selection=-1,hover=-1,pressed=-1;
	bool enterHeld=false;
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
		for (int col=0;col<5;++col) scroll[col].Clamp(int(columns[col].size()));
		oc2_buy::Item selected;
		if (selection>=0 && (!g_pOc2BuyMenu->GetItem(selection,selected) || !selected.visible))
		{
			selection=-1;
			for (int col=0;col<5 && selection<0;++col)
				if (!columns[col].empty()) selection=columns[col][0];
		}
		return true;
	}
	Rect Viewport(int col) const { return Area(columnX[col],.155556f,columnW[col],.666667f); }
	Rect Card(int col,int row) const
	{
		const Rect viewport=Viewport(col);
		const float step=viewport.h/oc2_buy_scroll::visibleRows;
		const int top=int(std::floor((row-scroll[col].position)*step));
		const int bottom=int(std::floor((row+1-scroll[col].position)*step));
		const int gap=std::max(1,int(ScreenHeight/540.0f));
		return {viewport.x,viewport.y+top,viewport.w,std::max(1,bottom-top-gap)};
	}
	void AdvanceScroll()
	{
		const float seconds=uiStatic.realTime>lastScrollTime?(uiStatic.realTime-lastScrollTime)*.001f:0.0f;
		lastScrollTime=uiStatic.realTime;
		for (int col=0;col<5;++col) scroll[col].Advance(seconds,int(columns[col].size()));
		// Recompute even with a stationary mouse: cards can move underneath it.
		hover=Hit(uiStatic.cursorX,uiStatic.cursorY);
	}
	void Fill(Rect rect,unsigned int color,const Rect *clip=nullptr)
	{
		// The menu PIC scissor clips pictures/font glyphs, but engine FillRGBA
		// bypasses it. Clip solid card backgrounds and outlines explicitly.
		if (clip)
		{
			const int right=std::min(rect.x+rect.w,clip->x+clip->w);
			const int bottom=std::min(rect.y+rect.h,clip->y+clip->h);
			rect.x=std::max(rect.x,clip->x); rect.y=std::max(rect.y,clip->y);
			rect.w=right-rect.x; rect.h=bottom-rect.y;
		}
		if (rect.w>0 && rect.h>0) UI_FillRect(rect.x,rect.y,rect.w,rect.h,color);
	}
	void Outline(Rect r,unsigned int color,const Rect &clip)
	{
		const int width=std::max(1,int(ScreenHeight/540.0f));
		Fill({r.x,r.y,r.w,width},color,&clip);
		Fill({r.x,r.y+r.h-width,r.w,width},color,&clip);
		Fill({r.x,r.y,width,r.h},color,&clip);
		Fill({r.x+r.w-width,r.y,width,r.h},color,&clip);
	}
	void DrawScrollHint(int col)
	{
		const int count=int(columns[col].size());
		if (count<=int(oc2_buy_scroll::visibleRows)) return;
		const Rect track=Area(columnX[col],.837f,columnW[col],.002f);
		Fill(track,PackRGBA(170,170,170,80));
		const int width=std::max(2,int(track.w*oc2_buy_scroll::visibleRows/count));
		const int x=track.x+int((track.w-width)*scroll[col].position/oc2_buy_scroll::State::Maximum(count));
		Fill({x,track.y,width,std::max(2,track.h)},PackRGBA(224,198,130,220));
		Text("滚轮滚动",Area(columnX[col],.845f,columnW[col],.023f),14,PackRGBA(215,215,215,220),QM_CENTER);
	}
	HFont Font(int height)
	{
		const int pixels=std::max(10,int(height*ScreenHeight/1080.0f));
		if (!fonts.count(pixels) || g_FontMgr->GetFontTall(fonts[pixels])<=0)
			fonts[pixels]=CFontBuilder("Microsoft YaHei UI",pixels,500).Create();
		return fonts[pixels];
	}
	void Text(const char *text,Rect rect,int height,unsigned int color,uint align=QM_CENTER)
	{
		const HFont font=Font(height);
		const int actualHeight=std::max(1,g_FontMgr->GetFontTall(font));
		UI_DrawString(font,rect.x,rect.y,rect.w,rect.h,text,color,actualHeight,align,ETF_NO_WRAP|ETF_FORCECOL);
	}
	HIMAGE Picture(const char *name)
	{
		if (!name || !name[0]) return 0;
		auto it=pictures.find(name);
		if (it!=pictures.end()) return it->second;
		return pictures[name]=EngFuncs::PIC_Load(name);
	}
	void DrawCard(const oc2_buy::Item &item,Rect r,const Rect &clip)
	{
		Fill(r,PackRGBA(5,5,5,245),&clip);
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
		Text(item.name,{r.x+pad,r.y+2,r.w-pad*2,int(r.h*.19f)},18,textColor,QM_CENTER);
		char cost[32]; std::snprintf(cost,sizeof(cost),"$%d",item.price);
		Text(cost,{r.x+pad,r.y+int(r.h*.81f),r.w-pad*2,int(r.h*.18f)},18,textColor,QM_RIGHT);
		if (item.owned || item.count>0)
		{
			char owned[32];
			if (item.limit>1) std::snprintf(owned,sizeof(owned),"%d / %d",item.count,item.limit);
			else std::snprintf(owned,sizeof(owned),"已拥有");
			Text(owned,{r.x+pad,r.y+int(r.h*.81f),int(r.w*.65f),int(r.h*.18f)},14,PackRGBA(225,198,126,255));
		}
	}
	void DrawButton(int id,Rect r,bool enabled)
	{
		const int value=enabled?255:100;
		Fill(r,PackRGBA(0,0,0,230));
		Text(id==-2?"关闭":"购买",r,28,PackRGBA(value,value,value,255),QM_CENTER);
		if (hover==id && enabled) UI_DrawRectangleExt(r.x,r.y,r.w,r.h,PackRGBA(237,199,97,255),2);
	}
	int Hit(int x,int y) const
	{
		if (Area(.44f,.888889f,.10f,.066667f).Contains(x,y)) return -2;
		if (Area(.55f,.888889f,.10f,.066667f).Contains(x,y)) return -3;
		for (int col=0;col<5;++col)
		{
			if (!Viewport(col).Contains(x,y)) continue;
			for (int row=0;row<int(columns[col].size());++row)
				if (Card(col,row).Contains(x,y)) return columns[col][row];
		}
		return -1;
	}
	void Select(int index)
	{
		selection=index;
		oc2_buy::Item item;
		if (g_pOc2BuyMenu->GetItem(index,item)) Con_Printf("buymenu_ui: select id=%d enabled=%d price=%d\n",item.id,int(item.enabled),item.price);
	}
	void ScrollColumn(int col,float notches)
	{
		if (col<0 || col>=5) return;
		scroll[col].Scroll(notches,int(columns[col].size()));
		pressed=-1; // scrolling during a press must not activate a displaced card
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
						scroll[nextCol].Reveal(nextRow,int(columns[nextCol].size())); Select(columns[nextCol][nextRow]);
					}
					return;
				}
	}
};

COc2BuyMenu menu;
}

void UI_OC2BuyMenu_Precache()
{
	menu.VidInit();menu.Precache();
	extern void OC2_PrecacheBuyModels();
	if(g_pOc2BuyMenu)OC2_PrecacheBuyModels();
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
	EngFuncs::Cmd_AddCommand("buy_menu_scroll",[](){ if(EngFuncs::CmdArgc()==3) menu.DebugScroll(std::atoi(EngFuncs::CmdArgv(1)),float(std::atof(EngFuncs::CmdArgv(2)))); });
	EngFuncs::Cmd_AddCommand("buy_menu_scroll_dump",[](){ menu.DebugDump(); });
}
