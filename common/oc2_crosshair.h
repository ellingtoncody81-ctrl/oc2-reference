#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// CS:GO weapon_csbase.cpp DrawCrosshair + Panorama default reticle, adapted to
// GoldSrc's production spread snapshot. Shared by the HUD and settings preview.
namespace oc2_crosshair
{
struct Variable { const char *name,*value; };
static const Variable variables[]={
 {"cl_crosshairstyle","2"},{"cl_crosshairsize","5"},{"cl_crosshairthickness","0.5"},
 {"cl_crosshairgap","1"},{"cl_crosshairgap_useweaponvalue","0"},{"cl_crosshaircolor","1"},
 {"cl_crosshaircolor_r","50"},{"cl_crosshaircolor_g","250"},{"cl_crosshaircolor_b","50"},
 {"cl_crosshairalpha","200"},{"cl_crosshairusealpha","1"},{"cl_crosshairdot","1"},
 {"cl_crosshair_drawoutline","1"},{"cl_crosshair_outlinethickness","1"},{"cl_crosshair_t","0"},
 {"cl_crosshair_dynamic_splitdist","7"},{"cl_crosshair_dynamic_splitalpha_innermod","1"},
 {"cl_crosshair_dynamic_splitalpha_outermod","0.5"},{"cl_crosshair_dynamic_maxdist_splitratio","0.35"},
 {"cl_fixedcrosshairgap","3"}
};
inline float Clamp(float n,float lo,float hi,float fallback)
{return std::isfinite(n)?std::max(lo,std::min(hi,n)):fallback;}
struct Config
{
 int style=2,color=1,r=50,g=250,b=50,alpha=200;
 float size=5,thickness=.5f,gap=1,outline=1,split=7,innerAlpha=1,outerAlpha=.5f,ratio=.35f,fixedGap=3;
 bool weaponGap=false,useAlpha=true,dot=true,drawOutline=true,t=false;
};
template<class Read> Config ReadConfig(Read read)
{
 Config c;
 c.style=int(Clamp(read("cl_crosshairstyle"),0,5,2));c.color=int(Clamp(read("cl_crosshaircolor"),0,5,1));
 c.r=int(Clamp(read("cl_crosshaircolor_r"),0,255,50));c.g=int(Clamp(read("cl_crosshaircolor_g"),0,255,250));c.b=int(Clamp(read("cl_crosshaircolor_b"),0,255,50));
 c.alpha=int(Clamp(read("cl_crosshairalpha"),0,255,200));
 c.size=Clamp(read("cl_crosshairsize"),0,30,5);c.thickness=Clamp(read("cl_crosshairthickness"),0,10,.5f);
 c.gap=Clamp(read("cl_crosshairgap"),-20,30,1);c.outline=Clamp(read("cl_crosshair_outlinethickness"),.1f,3,1);
 c.split=Clamp(read("cl_crosshair_dynamic_splitdist"),0,100,7);c.innerAlpha=Clamp(read("cl_crosshair_dynamic_splitalpha_innermod"),0,1,1);
 c.outerAlpha=Clamp(read("cl_crosshair_dynamic_splitalpha_outermod"),.3f,1,.5f);c.ratio=Clamp(read("cl_crosshair_dynamic_maxdist_splitratio"),0,1,.35f);
 c.fixedGap=Clamp(read("cl_fixedcrosshairgap"),-10,30,3);
 c.weaponGap=read("cl_crosshairgap_useweaponvalue")!=0;c.useAlpha=read("cl_crosshairusealpha")!=0;
 c.dot=read("cl_crosshairdot")!=0;c.drawOutline=read("cl_crosshair_drawoutline")!=0;c.t=read("cl_crosshair_t")!=0;
 return c;
}
struct Input
{
 float width=1920,height=1080,centerX=960,centerY=540,fov=90,spread=.01f,speed=0,weaponGap=4,oldGap=4;
 bool grounded=true,ducked=false;
};
struct Rect {float x,y,w,h;int r,g,b,a;bool additive;};
inline std::vector<Rect> Build(const Config &c,const Input &in)
{
 std::vector<Rect> out;out.reserve(64);
 const float scale=Clamp(in.height,1,16384,1080)/480.f;
 int r=c.r,g=c.g,b=c.b;
 static const int colors[5][3]={{250,50,50},{50,250,50},{250,250,50},{50,50,250},{50,250,250}};
 if(c.color<5){r=colors[c.color][0];g=colors[c.color][1];b=colors[c.color][2];}
 const int alpha=c.useAlpha?c.alpha:200;
 const auto rect=[&](float x,float y,float w,float h,int a,bool outline=true)
 {
  if(w<=0||h<=0||a<=0)return;
  if(c.drawOutline&&outline)out.push_back({x-c.outline,y-c.outline,w+2*c.outline,h+2*c.outline,0,0,0,a,false});
  out.push_back({x,y,w,h,r,g,b,a,!c.useAlpha});
 };
 const float cx=std::floor(in.centerX),cy=std::floor(in.centerY);
 const float thick=std::max(1.f,std::round(c.thickness*scale));
 const float length=std::round(c.size*scale),half=std::floor(thick*.5f);
 const auto cross=[&](float gap,float len,int a,float width,bool t)
 {
  const float h=std::floor(width*.5f);
  rect(cx-gap-h-len,cy-h,len,width,a);rect(cx+gap+width-h,cy-h,len,width,a);
  if(!t)rect(cx-h,cy-gap-h-len,width,len,a);
  rect(cx-h,cy+gap+width-h,width,len,a);
 };
 const float raw=Clamp(in.spread,0,1,0)*320.f/std::tan(Clamp(in.fov,1,179,90)*.00872664626f);
 if(c.style<2)
 {
  const float s=in.height/768.f;
  const float gap=(c.style==1?c.fixedGap+7:!in.grounded?30:in.ducked?3:in.speed>100?20:10)*s;
  cross(std::round(gap),std::max(2.f,std::round(6*s)),alpha,std::max(1.f,std::round(2*s)),false);
  if(c.style==0)
  {
   const float radius=std::max(gap+8*s,raw*scale+8*s);
   for(int arm=0;arm<4;++arm)for(int segment=0;segment<9;++segment)
   {
    const float angle=(arm*90-24+segment*6)*.01745329252f;
    const float w=std::max(1.f,std::round(s*1.5f));
    rect(std::round(cx+std::cos(angle)*radius)-w*.5f,std::round(cy+std::sin(angle)*radius)-w*.5f,w,w,alpha,false);
   }
  }
  return out;
 }
 float gap=std::round(raw*scale)+c.gap;
 if(c.style==4)gap=(c.weaponGap?in.weaponGap:4)+c.gap;
 else if(c.style==5)gap=std::round(in.oldGap*in.height/1200.f+c.gap);
 else if(raw<=0)gap=(c.weaponGap?in.weaponGap:4)+c.gap;
 gap=float(int(gap));
 if(c.style==2&&raw>c.split)
 {
  const float inner=std::ceil(length*(1-c.ratio)),outer=std::floor(length*c.ratio);
  cross(float(int(std::round(c.split*scale)+c.gap)),inner,int(alpha*c.innerAlpha),thick,c.t);
  cross(gap+inner,outer,int(alpha*c.outerAlpha),thick,c.t);
 }
 else cross(gap,length,alpha,thick,c.t);
 if(c.dot)rect(cx-half,cy-half,thick,thick,alpha);
 return out;
}
struct OldState {float gap=4,time=-1;int shots=0,weapon=0,player=0;};
inline float OldGap(OldState &state,float time,int shots,int weapon,int player,float base,float recoil=3.5f)
{
 if(state.time<0||time<state.time||time-state.time>1||state.weapon!=weapon||state.player!=player)
 {state=OldState();state.gap=base;state.shots=shots;}
 const float dt=state.time<0?0:std::max(0.f,time-state.time);
 if(shots>state.shots)state.gap+=recoil/3.5f;
 if(state.gap>base)state.gap-=42*dt;
 state.gap=std::max(base,std::min(25.f,state.gap));state.shots=shots;state.time=time;state.weapon=weapon;state.player=player;
 return state.gap;
}
}
