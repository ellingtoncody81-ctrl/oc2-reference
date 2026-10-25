/*
JoinGame.cpp - Team selection menu
Copyright (C) 2018 a1batross
Copyright (C) 2026 $_Vladislav

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "BaseMenu.h"
#include "ClientWindow.h"
#include "BuyModelPreview.h"
#include "TeamSelection.h"
#include "../frontend/PanoramaChrome.h"
#include <cstdio>
#include <cstring>

namespace
{
struct TeamRect
{
	int x,y,w,h;
	bool Contains(int px,int py) const { return px>=x&&py>=y&&px<x+w&&py<y+h; }
};
TeamRect Area(float x,float y,float w,float h)
{
	return {int(x*ScreenWidth),int(y*ScreenHeight),int(w*ScreenWidth),int(h*ScreenHeight)};
}
void Fill(TeamRect r,unsigned color) { UI_FillRect(r.x,r.y,r.w,r.h,color); }
void Text(const char *text,TeamRect r,int height,unsigned color,unsigned align=QM_CENTER)
{
	oc2_panorama::DrawText(Point(r.x,r.y),Size(r.w,r.h),text,height,color,height>=26,align);
}
const unsigned kWhite=PackRGBA(240,244,248,255);
const unsigned kMuted=PackRGBA(174,186,198,255);

class CClientJoinGame : public CClientWindow
{
public:
	CClientJoinGame() : CClientWindow("OC2TeamSelection") {}
	void _Init() override {}
	void VidInit() override
	{
		pos=Point(0,0);size=Size(int(ScreenWidth/uiStatic.scaleX),int(ScreenHeight/uiStatic.scaleY));
		CMenuItemsHolder::VidInit();
	}
	void Open(int flags,int team)
	{
		m_flags=flags;m_currentTeam=team>=0&&team<=3?team:0;
		m_selection=0;m_hover=-1;m_pressed=-1;m_enterHeld=false;
		const int elapsed=uiStatic.realTime-m_requestedAt;
		m_rejected=m_requested!=0&&elapsed>=0&&elapsed<5000
			&&!(m_requested==6&&m_currentTeam==3);
		m_requested=0;
		ReadMap();
		OC2_RestartTeamPreviews();
		if(!IsVisible())CClientWindow::Show();
		else { VidInit();EngFuncs::KEY_SetDest(KEY_MENU); }
		DisableTransition();
		Con_Printf("teamselect_ui: open flags=%d team=%d retry=%d map=%s\n",m_flags,m_currentTeam,int(m_rejected),m_map);
	}
	void Hide() override
	{
		UI_CloseClientMenu();DisableTransition();m_pressed=-1;m_enterHeld=false;
	}
	void Confirmed(int team)
	{
		m_requested=0;m_currentTeam=team;
		if(IsVisible())Hide();
		Con_Printf("teamselect_ui: accepted team=%d default_class=%d\n",team,team==2?3:1);
	}
	void Dump() const
	{
		const bool visible=uiStatic.client.Current()==this&&!uiStatic.menu.IsActive();
		Con_Printf("teamselect: visible=%d selected=%d hover=%d current_team=%d\n",
			int(visible),m_selection,m_hover,m_currentTeam);
		const TeamRect rectangles[]={Card(1),Card(2),Area(.55f,.90f,.20f,.055f)};
		const char *tags[]={"team_t","team_ct","team_join"};
		const int ids[]={1,2,7};
		for(int i=0;i<3;++i)
		{
			const TeamRect &r=rectangles[i];
			const bool enabled=visible&&(i<2||m_selection!=0);
			Con_Printf("teamselect_item: tag=%s x=%d y=%d w=%d h=%d visible=%d enabled=%d focus=%d\n",
				tags[i],r.x,r.y,r.w,r.h,int(visible),int(enabled),int(enabled&&m_hover==ids[i]));
		}
		OC2_DumpTeamPreviews();
	}
	void Draw() override
	{
		// APK touch/chooseteam.cfg splits the scene into two equal team areas.
		// The actual world remains visible under the same full-screen chrome.
		oc2_panorama::DrawBackground();
		Fill(Area(0,0,1,1),PackRGBA(9,16,24,170));
		Text("选择你的阵营",Area(.08f,.038f,.84f,.065f),34,kWhite);
		char mapLine[160];std::snprintf(mapLine,sizeof(mapLine),"%s  ·  %s",m_mode,m_map);
		Text(mapLine,Area(.08f,.108f,.84f,.032f),18,kMuted);
		DrawTeam(1);DrawTeam(2);
		Fill(Area(.06f,.835f,.88f,.0015f),PackRGBA(190,205,220,65));
		if(m_rejected)
			Text("暂时无法加入该队伍，请选择其他阵营或自动分配",Area(.06f,.845f,.88f,.035f),18,PackRGBA(238,190,117,255));
		else
			Text("选择阵营后加入对局，自动分配将按服务器规则平衡人数",Area(.06f,.845f,.88f,.035f),18,kMuted);
		Button(5,"自动分配",Area(.38f,.90f,.15f,.055f),true);
		Button(7,m_selection==1?"加入恐怖分子":m_selection==2?"加入反恐精英":"加入队伍",Area(.55f,.90f,.20f,.055f),m_selection!=0);
		if(m_flags&1)Button(6,"观战",Area(.21f,.90f,.15f,.055f),true);
		if(m_flags&4)Button(0,"返回对局",Area(.06f,.90f,.13f,.055f),true);
		if(m_flags&2)Button(3,"申请要员",Area(.77f,.90f,.17f,.055f),true);
		Text("方向键选择阵营   回车键确认   5 自动分配",Area(.20f,.968f,.60f,.023f),12,kMuted);
	}
	bool MouseMove(int x,int y) override { m_hover=Hit(x,y);return true; }
	bool KeyDown(int key) override
	{
		if(UI::Key::IsConsole(key))return CClientWindow::KeyDown(key);
		if(UI::Key::IsEscape(key)||key=='0') { Cancel();return true; }
		if(key==K_MOUSE1) { m_pressed=Hit(uiStatic.cursorX,uiStatic.cursorY);return true; }
		if(key==K_ENTER||key==K_KP_ENTER)
		{
			if(!m_enterHeld){m_enterHeld=true;Request(m_selection?m_selection:5);}
			return true;
		}
		if(key=='1'||key==K_LEFTARROW) { m_selection=1;return true; }
		if(key=='2'||key==K_RIGHTARROW) { m_selection=2;return true; }
		if(key=='5') { Request(5);return true; }
		if(key=='6'&&(m_flags&1)) { Request(6);return true; }
		if(key=='3'&&(m_flags&2)) { Request(3);return true; }
		return true;
	}
	bool KeyUp(int key) override
	{
		if(key==K_ENTER||key==K_KP_ENTER)m_enterHeld=false;
		if(key==K_MOUSE1)
		{
			const int hit=Hit(uiStatic.cursorX,uiStatic.cursorY);
			if(hit==m_pressed)
			{
				if(hit==1||hit==2)m_selection=hit;
				else if(hit==0)Cancel();
				else if(hit==7&&m_selection)Request(m_selection);
				else if(hit==3||hit==5||hit==6)Request(hit);
			}
			m_pressed=-1;
		}
		return true;
	}

private:
	int m_flags=0,m_currentTeam=0,m_selection=0,m_hover=-1,m_pressed=-1,m_requested=0,m_requestedAt=0;
	bool m_enterHeld=false,m_rejected=false;
	char m_map[96]={};
	const char *m_mode="团队对抗";
	const char *m_tasks[2]={"完成地图目标，击败敌方队伍","完成地图目标，击败敌方队伍"};
	void ReadMap()
	{
		const char *level=g_pClient?g_pClient->GetLevelName():nullptr;
		const char *base=level?level:"";
		for(const char *p=base;*p;++p)if(*p=='/'||*p=='\\')base=p+1;
		std::snprintf(m_map,sizeof(m_map),"%s",*base?base:"当前地图");
		if(char *extension=std::strrchr(m_map,'.'))*extension=0;
		m_mode="团队对抗";m_tasks[0]=m_tasks[1]="完成地图目标，击败敌方队伍";
		if(!strnicmp(m_map,"de_",3))
		{
			m_mode="爆破任务";m_tasks[0]="安放炸弹并守住爆破目标";m_tasks[1]="阻止爆破，拆除已安放的炸弹";
		}
		else if(!strnicmp(m_map,"cs_",3))
		{
			m_mode="人质救援";m_tasks[0]="阻止敌人营救人质";m_tasks[1]="营救人质并护送至安全区域";
		}
		else if(!strnicmp(m_map,"as_",3))
		{
			m_mode="保护要员";m_tasks[0]="阻止要员撤离";m_tasks[1]="保护要员抵达撤离点";
		}
	}
	TeamRect Card(int team) const { return Area(team==1?.08f:.52f,.18f,.40f,.625f); }
	void DrawTeam(int team)
	{
		const float left=team==1?.08f:.52f;
		const bool selected=m_selection==team,hot=m_hover==team;
		const unsigned accent=team==1?PackRGBA(222,176,100,255):PackRGBA(128,179,223,255);
		const TeamRect card=Card(team);
		Fill(card,team==1?PackRGBA(68,51,31,selected?150:95):PackRGBA(34,53,73,selected?170:110));
		Fill(Area(left,.18f,.40f,.004f),accent);
		const HIMAGE emblem=EngFuncs::PIC_Load("gfx/vgui/teambg.tga");
		if(emblem)
		{
			// The APK's 3264x1530 backdrop has larger insignia than t_ts/ct_ts.
			// Crop only each badge; additive drawing leaves its black canvas clear.
			const wrect_t crop=team==1?wrect_t{1002,1578,302,878}:wrect_t{1676,2252,314,890};
			const int side=Q_min(int(card.w*.70f),int(ScreenHeight*.43f));
			EngFuncs::PIC_Set(emblem,255,255,255,selected?180:145);
			EngFuncs::PIC_DrawAdditive(card.x+(card.w-side)/2,int(ScreenHeight*.278f),side,side,&crop);
		}
		const TeamRect actor=Area(left+.055f,.272f,.29f,.42f);
		// These are the same Phoenix / SAS assets used in the home scene.
		// Each team loops its real lobby idle without replaying the entrance.
		if(!OC2_DrawTeamPreview(team,team==1?28:22,actor.x,actor.y,actor.w,actor.h))
			Text(team==1?"凤凰战士":"英国特勤队",actor,28,kMuted);
		Text(team==1?"恐怖分子":"反恐精英",Area(left+.02f,.201f,.36f,.05f),28,accent);
		Text(team==1?"凤凰战士":"英国特勤队",Area(left+.02f,.702f,.36f,.033f),20,kWhite);
		Text(m_tasks[team-1],Area(left+.02f,.744f,.36f,.027f),18,kMuted);
		if(m_currentTeam==team)
			Text("当前阵营",Area(left+.29f,.251f,.09f,.027f),12,accent);
		if(selected||hot)
			UI_DrawRectangleExt(card.x,card.y,card.w,card.h,selected?accent:PackRGBA(225,234,242,160),Q_max(1,int(2*uiStatic.scaleY)));
		Text(selected?"已选择":team==1?"按 1 选择":"按 2 选择",Area(left+.02f,.777f,.36f,.023f),12,selected?accent:kMuted);
	}
	void Button(int id,const char *label,TeamRect r,bool enabled)
	{
		const bool primary=id==7;
		Fill(r,enabled?(primary?PackRGBA(192,207,218,235):PackRGBA(67,82,95,180)):PackRGBA(44,53,62,150));
		if(enabled&&m_hover==id)UI_DrawRectangleExt(r.x,r.y,r.w,r.h,kWhite,Q_max(1,int(2*uiStatic.scaleY)));
		Text(label,r,20,enabled?(primary?PackRGBA(18,30,40,255):kWhite):PackRGBA(116,129,140,255));
	}
	int Hit(int x,int y) const
	{
		if(Card(1).Contains(x,y))return 1;
		if(Card(2).Contains(x,y))return 2;
		if(Area(.38f,.90f,.15f,.055f).Contains(x,y))return 5;
		if(m_selection&&Area(.55f,.90f,.20f,.055f).Contains(x,y))return 7;
		if((m_flags&1)&&Area(.21f,.90f,.15f,.055f).Contains(x,y))return 6;
		if((m_flags&4)&&Area(.06f,.90f,.13f,.055f).Contains(x,y))return 0;
		if((m_flags&2)&&Area(.77f,.90f,.17f,.055f).Contains(x,y))return 3;
		return -1;
	}
	void Cancel()
	{
		if(!(m_flags&4))return;
		m_requested=0;Hide();
	}
	void Request(int slot)
	{
		if(!IsVisible()||(slot==6&&!(m_flags&1))||(slot==3&&!(m_flags&2)))return;
		if(slot!=1&&slot!=2&&slot!=3&&slot!=5&&slot!=6)return;
		m_requested=slot;m_requestedAt=uiStatic.realTime;Hide();
		char command[24];std::snprintf(command,sizeof(command),"jointeam %d\n",slot);
		EngFuncs::ClientCmd(false,command);
		// CSSO closes on request and the server reopens on rejection. The class
		// command is deliberately deferred until its authoritative class menu.
		Con_Printf("teamselect_ui: request slot=%d\n",slot);
	}
} uiJoinGame;
}

void UI_JoinGame_Show(int param1,int param2) { uiJoinGame.Open(param1,param2); }
void UI_JoinGame_Confirmed(int team) { uiJoinGame.Confirmed(team); }
void UI_JoinGame_Dump() { uiJoinGame.Dump(); }
