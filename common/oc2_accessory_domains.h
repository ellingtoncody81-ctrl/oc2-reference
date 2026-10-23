#pragma once
#include "oc2_accessories.h"

// Generated from the same real gun-side surfaces used by the client. Rows are
// top to bottom in the preview. Both rigid (1) and legacy moving-part (2)
// cells are editable gun surfaces. Empty cells (0) fit to the nearest surface.
namespace oc2_accessory_domains
{
static const char *const kPath="gfx/inventory/previews/mount_domains.dat";
static const int kWidth=128,kHeight=64;
struct Domains
{
 std::array<std::string,24> cells;
 bool valid=false;
};
inline bool Parse(const char *text,Domains &out,std::string &error)
{
 if(!text){error="Missing mount regions";return false;}
 std::istringstream input(text);std::string magic;int version=0,width=0,height=0;
 if(!(input>>magic>>version>>width>>height)||magic!="OC2_MOUNT_DOMAINS"||version!=1||width!=kWidth||height!=kHeight)
  {error="Unsupported mount regions";return false;}
 Domains next;std::array<bool,24> seen{};
 for(int n=0;n<24;++n)
 {
  std::string tag;int weapon=0;
  if(!(input>>tag>>weapon)||tag!="weapon") {error="Incomplete mount regions";return false;}
  const int index=oc2_accessories::Index(weapon);
  if(index<0||seen[index]){error="Invalid mount region weapon";return false;}
  seen[index]=true;std::string &grid=next.cells[index];grid.reserve(kWidth*kHeight);
  for(int y=0;y<kHeight;++y)
  {
   std::string row;
   if(!(input>>row)||row.size()!=kWidth||row.find_first_not_of("012")!=std::string::npos)
    {error="Invalid mount region row";return false;}
   grid+=row;
  }
 }
 std::string extra;if(input>>extra){error="Unexpected mount region data";return false;}
 next.valid=true;out=next;error.clear();return true;
}
inline bool Allowed(const Domains &domains,int weapon,float u,float v)
{
 const int index=oc2_accessories::Index(weapon);
 if(!domains.valid||index<0||!oc2_accessories::Range(u,0,1)||!oc2_accessories::Range(v,0,1))return false;
 const int x=std::min(kWidth-1,int(u*kWidth)),y=std::min(kHeight-1,int((1-v)*kHeight));
 return domains.cells[index][y*kWidth+x]!='0';
}
inline bool NearestAllowed(const Domains &domains,int weapon,float &u,float &v)
{
 if(Allowed(domains,weapon,u,v))return true;
 const int index=oc2_accessories::Index(weapon);
 if(!domains.valid||index<0||!oc2_accessories::Range(u,0,1)||!oc2_accessories::Range(v,0,1))return false;
 float distance=std::numeric_limits<float>::max(),bestU=u,bestV=v;bool found=false;
 for(int y=0;y<kHeight;++y)for(int x=0;x<kWidth;++x)
 {
  if(domains.cells[index][y*kWidth+x]=='0')continue;
  const float candidateU=(x+.5f)/kWidth,candidateV=1-(y+.5f)/kHeight;
  const float dx=candidateU-u,dy=candidateV-v,squared=dx*dx+dy*dy;
  if(squared<distance){distance=squared;bestU=candidateU;bestV=candidateV;found=true;}
 }
 if(found){u=bestU;v=bestV;}return found;
}
}
