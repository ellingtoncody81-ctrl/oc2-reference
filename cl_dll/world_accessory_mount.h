#pragma once
#include "accessory_geometry.h"
#include <algorithm>
#include <cmath>

// Correspondence is evaluated only when a model or saved mount changes.
namespace oc2_world_mount
{
struct V
{
 double x=0,y=0,z=0;
 V()=default;V(double a,double b,double c):x(a),y(b),z(c){}explicit V(const float*p):x(p[0]),y(p[1]),z(p[2]){}
 V operator+(V b)const{return {x+b.x,y+b.y,z+b.z};}V operator-(V b)const{return {x-b.x,y-b.y,z-b.z};}
 V operator*(double s)const{return {x*s,y*s,z*s};}double dot(V b)const{return x*b.x+y*b.y+z*b.z;}
 V cross(V b)const{return {y*b.z-z*b.y,z*b.x-x*b.z,x*b.y-y*b.x};}double length2()const{return dot(*this);}
 V unit()const{double n=std::sqrt(length2());return n>1e-12?*this*(1/n):V();}
 void store(float*p)const{p[0]=float(x);p[1]=float(y);p[2]=float(z);}
};
inline V transform(const float m[3][4],V p,bool position=true)
{return {m[0][0]*p.x+m[0][1]*p.y+m[0][2]*p.z+(position?m[0][3]:0),m[1][0]*p.x+m[1][1]*p.y+m[1][2]*p.z+(position?m[1][3]:0),m[2][0]*p.x+m[2][1]*p.y+m[2][2]*p.z+(position?m[2][3]:0)};}
inline V inverse(const float m[3][4],V p,bool position=true)
{if(position)p=p-V(m[0][3],m[1][3],m[2][3]);return {m[0][0]*p.x+m[1][0]*p.y+m[2][0]*p.z,m[0][1]*p.x+m[1][1]*p.y+m[2][1]*p.z,m[0][2]*p.x+m[1][2]*p.y+m[2][2]*p.z};}
inline V segment(V p,V a,V b)
{V d=b-a;double n=d.length2(),t=n>1e-20?std::max(0.,std::min(1.,(p-a).dot(d)/n)):0.;return a+d*t;}
inline V closest(V p,V a,V b,V c)
{
 V ab=b-a,ac=c-a,n=ab.cross(ac);double nn=n.length2();
 if(nn>1e-20){V q=p-n*((p-a).dot(n)/nn),d=q-a;double aa=ab.dot(ab),bb=ab.dot(ac),cc=ac.dot(ac),den=aa*cc-bb*bb;
  if(den>1e-20){double u=(cc*d.dot(ab)-bb*d.dot(ac))/den,v=(aa*d.dot(ac)-bb*d.dot(ab))/den;if(u>=0&&v>=0&&u+v<=1)return q;}}
 V q=segment(p,a,b),r=segment(p,b,c),s=segment(p,c,a);if((p-r).length2()<(p-q).length2())q=r;if((p-s).length2()<(p-q).length2())q=s;return q;
}
inline bool Map(const oc2_accessory::MountCache& view,const oc2_accessory::Mount& source,
 const oc2_accessory::MountCache& target,const std::vector<oc2_accessory::Triangle>& triangles,
 const float fit[3][4],oc2_accessory::Mount& out,float& distance,int targetBone=-1,
 const unsigned char* regions=nullptr,size_t regionCount=0,int region=0)
{
 if(source.bone<0||size_t(source.bone)>=view.bind.size())return false;
 const auto& vb=view.bind[source.bone];V desired=transform(fit,transform(vb.m,V(source.origin)));
 V expected=transform(fit,transform(vb.m,V(source.normal),false),false).unit();
 V along=transform(fit,transform(vb.m,V(source.axisX),false),false).unit();
 if(!std::isfinite(desired.length2())||expected.length2()<.5||along.length2()<.5)return false;
 double best=1e30;V bestPoint,bestNormal;int bone=-1;
 for(size_t index=0;index<triangles.size();++index)
 {
  const auto&t=triangles[index];
  if(t.bone<0||size_t(t.bone)>=target.bind.size())continue;
  if(targetBone>=0&&t.bone!=targetBone)continue;
  if(regions&&(index>=regionCount||regions[index]!=region))continue;
  V n=(V(t.p[1])-V(t.p[0])).cross(V(t.p[2])-V(t.p[0])).unit();if(n.length2()<.5)continue;
  if(n.dot(V(t.normal))<0)n=n*(-1);
  double align=n.dot(expected);if(align<.3)continue;
  V q=closest(desired,V(t.p[0]),V(t.p[1]),V(t.p[2]));double d=(q-desired).length2();
  double score=d+.0025*(1-align);if(score<best){best=score;bestPoint=q;bestNormal=n;bone=t.bone;}
 }
 if(bone<0)return false;distance=float(std::sqrt((bestPoint-desired).length2()));if(distance>3)return false;
 V x=(along-bestNormal*along.dot(bestNormal)).unit(),y=bestNormal.cross(x).unit();if(x.length2()<.5||y.length2()<.5)return false;
 out=source;out.bone=bone;const auto& tb=target.bind[bone];inverse(tb.m,bestPoint).store(out.origin);
 inverse(tb.m,x,false).unit().store(out.axisX);inverse(tb.m,y,false).unit().store(out.axisY);inverse(tb.m,bestNormal,false).unit().store(out.normal);
 out.snapDistance=distance;return true;
}
}
