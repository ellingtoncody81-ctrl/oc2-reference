#ifndef OC2_ACCESSORY_SWING_PATH_H
#define OC2_ACCESSORY_SWING_PATH_H

#include "accessory_geometry.h"
#include "accessory_mount_contact.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace oc2_accessory
{
struct SwingPathOptions
{
    float gap = .0001f;
    float minimumMotion = .00001f;
    float interpolationGuard = .000004f;
    unsigned maximumDepth = 12;
    unsigned maximumQueries = 128;
};

struct SwingPathResult
{
    float fraction = 0;
    unsigned queries = 0;
    unsigned certifiedSegments = 0;
    // The remaining path was not certified, including budget/tolerance stops.
    // This does not imply that the requested endpoint itself intersects.
    bool limited = false;
};

// A common separating slab for the complete swept triangle (both endpoint
// triples) proves separation throughout linear interpolation. Unlike a sphere
// motion bound, this also permits movement away from an almost-touching wall.
// Endpoints must already pass the caller's complete collision/containment test.
// Bit-identical faces inherit the start certificate; moving fixed-neck faces
// are clipped by the existing tiny contact cube over their entire swept hull.
inline bool CertifySeparatedSwingPath(const MeshCollider& obstacles,
 const std::vector<Triangle>& from,const std::vector<Triangle>& to,float gap,int contactBone)
{
 if(from.size()!=to.size()||obstacles.nodes.empty()||!std::isfinite(gap)||gap<0)return false;
 unsigned pairs=0;std::vector<int> stack;stack.reserve(64);
 for(size_t index=0;index<from.size();++index)
 {
  const auto& a=from[index];const auto& b=to[index];bool stationary=true;Bounds bounds;
  for(int axis=0;axis<3;++axis)
  {
   bounds.min[axis]=std::numeric_limits<float>::infinity();bounds.max[axis]=-bounds.min[axis];
   for(int corner=0;corner<3;++corner)
   {
    const float x=a.p[corner][axis],y=b.p[corner][axis];
    if(!std::isfinite(x)||!std::isfinite(y))return false;
    stationary=stationary&&x==y;bounds.min[axis]=std::min(bounds.min[axis],std::min(x,y));bounds.max[axis]=std::max(bounds.max[axis],std::max(x,y));
   }
  }
  if(stationary)continue;
  if(a.bone!=b.bone)return false;
  std::vector<std::vector<std::array<double,3>>> clouds;
  std::vector<std::array<double,3>> sweep;
  for(int c=0;c<3;++c){sweep.push_back({a.p[c][0],a.p[c][1],a.p[c][2]});sweep.push_back({b.p[c][0],b.p[c][1],b.p[c][2]});}
  if(a.bone!=contactBone)clouds.push_back(sweep);
  else
  {
   // The swept hull clipped outside each contact-cube halfspace is convex.
   // Its vertices are endpoint vertices and intersections of hull edges with
   // the plane. Testing all 15 segments includes every possible hull edge;
   // the resulting cloud conservatively covers the full moving exterior.
   const double half=double(kMountContactHalf)-kMountContactGuard;
   for(int plane=0;plane<6;++plane)
   {
    const int coordinate=plane/2;const bool lower=plane%2==0;
    const double boundary=(coordinate==2?.01:0)+(lower?-half:half);
    const auto distance=[&](const std::array<double,3>& p){return lower?boundary-p[coordinate]:p[coordinate]-boundary;};
    std::vector<std::array<double,3>> exterior;
    for(const auto& point:sweep)if(distance(point)>=0)exterior.push_back(point);
    for(size_t i=0;i<sweep.size();++i)for(size_t j=i+1;j<sweep.size();++j)
    {
     const double x=distance(sweep[i]),y=distance(sweep[j]);
     if((x<0&&y>0)||(x>0&&y<0))
     {const double f=x/(x-y);std::array<double,3> p;for(int k=0;k<3;++k)p[k]=sweep[i][k]+f*(sweep[j][k]-sweep[i][k]);p[coordinate]=boundary;exterior.push_back(p);}
    }
    if(!exterior.empty())clouds.push_back(std::move(exterior));
   }
  }
  stack.clear();stack.push_back(0);
  while(!stack.empty())
  {
   const int nodeIndex=stack.back();stack.pop_back();const auto& node=obstacles.nodes[nodeIndex];
   bool overlap=true;
   for(int axis=0;axis<3;++axis)if(double(bounds.min[axis])>double(node.bounds.max[axis])+gap||double(bounds.max[axis])+gap<double(node.bounds.min[axis]))overlap=false;
   if(!overlap)continue;
   if(node.count==0){if(node.left>=0)stack.push_back(node.left);if(node.right>=0)stack.push_back(node.right);continue;}
   for(int item=0;item<node.count;++item)for(const auto& cloud:clouds)
   {
    const int fixedIndex=obstacles.order[node.first+item];
    if(fixedIndex<0||size_t(fixedIndex)>=obstacles.triangleBounds.size())return false;
    const auto& fixedBounds=obstacles.triangleBounds[fixedIndex];
    bool axisClear=false;
    for(int axis=0;axis<3&&!axisClear;++axis)
    {
     double lo=1e100,hi=-1e100;for(const auto& point:cloud){lo=std::min(lo,point[axis]);hi=std::max(hi,point[axis]);}
     const double scale=std::max({1.,std::abs(lo),std::abs(hi),std::abs(double(fixedBounds.min[axis])),std::abs(double(fixedBounds.max[axis]))});
     const double margin=double(gap)+64*std::numeric_limits<double>::epsilon()*scale;
     axisClear=lo-double(fixedBounds.max[axis])>margin||double(fixedBounds.min[axis])-hi>margin;
    }
    if(axisClear)continue;
    if(++pairs>32768)return false;
    const auto& fixed=obstacles.triangles[fixedIndex];
    const auto separated=[&](const double axis[3])
    {
     const double length=std::sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);if(length<1e-20)return false;
     double movingMin=1e100,movingMax=-1e100,fixedMin=1e100,fixedMax=-1e100;
     for(const auto& point:cloud)
     {double x=0;for(int k=0;k<3;++k)x+=(axis[k]/length)*point[k];movingMin=std::min(movingMin,x);movingMax=std::max(movingMax,x);}
     for(int corner=0;corner<3;++corner)
     {
      double z=0;for(int k=0;k<3;++k)z+=(axis[k]/length)*fixed.p[corner][k];fixedMin=std::min(fixedMin,z);fixedMax=std::max(fixedMax,z);
     }
     const double scale=std::max({1.,std::abs(movingMin),std::abs(movingMax),std::abs(fixedMin),std::abs(fixedMax)});
     const double margin=double(gap)+64*std::numeric_limits<double>::epsilon()*scale;
     return movingMin-fixedMax>margin||fixedMin-movingMax>margin;
    };
    const auto cross=[](const double x[3],const double y[3],double z[3]){z[0]=x[1]*y[2]-x[2]*y[1];z[1]=x[2]*y[0]-x[0]*y[2];z[2]=x[0]*y[1]-x[1]*y[0];};
    double fe[3][3],ae[3][3],be[3][3],axis[3];
    for(int e=0;e<3;++e)for(int k=0;k<3;++k){fe[e][k]=double(fixed.p[(e+1)%3][k])-fixed.p[e][k];ae[e][k]=double(a.p[(e+1)%3][k])-a.p[e][k];be[e][k]=double(b.p[(e+1)%3][k])-b.p[e][k];}
    cross(fe[0],fe[1],axis);bool clear=separated(axis);
    if(!clear){cross(ae[0],ae[1],axis);clear=separated(axis);}
    if(!clear){cross(be[0],be[1],axis);clear=separated(axis);}
    for(int e=0;e<3&&!clear;++e)for(int f=0;f<3&&!clear;++f){cross(fe[e],ae[f],axis);clear=separated(axis);if(!clear){cross(fe[e],be[f],axis);clear=separated(axis);}}
    for(int k=0;k<3&&!clear;++k){axis[0]=axis[1]=axis[2]=0;axis[k]=1;clear=separated(axis);}
    // Endpoint face normals alone omit the side faces of the swept hull.
    // Any three of its six generating points include every hull face normal;
    // after contact clipping, cube planes are already covered by XYZ above.
    for(size_t i=0;i<sweep.size()&&!clear;++i)for(size_t j=i+1;j<sweep.size()&&!clear;++j)for(size_t k=j+1;k<sweep.size()&&!clear;++k)
    {double x[3],y[3];for(int d=0;d<3;++d){x[d]=sweep[j][d]-sweep[i][d];y[d]=sweep[k][d]-sweep[i][d];}cross(x,y,axis);clear=separated(axis);}
    // All cloud chords include all true edges, including newly clipped edges.
    // Extra candidate axes can only prove separation, never allow collision.
    for(size_t i=0;i<cloud.size()&&!clear;++i)for(size_t j=i+1;j<cloud.size()&&!clear;++j)
    {double edge[3];for(int d=0;d<3;++d)edge[d]=cloud[j][d]-cloud[i][d];for(int e=0;e<3&&!clear;++e){cross(edge,fe[e],axis);clear=separated(axis);}}
    if(!clear)return false;
   }
  }
 }
 return true;
}

// The start must already be free at options.gap against the current gun pose.
// Face/corner order, coordinate frame and obstacles must stay fixed throughout
// this call. groups supplies one ID (0, 1 or 2) per face: fixed clip, chain,
// pendant. Assign connecting faces to the chain; no face is discarded.
//
// free(query, expandedGap) must certify the supplied real triangles against the
// gun at that separation. Each query contains one complete group. The callback
// must consume it synchronously and must not retain its temporary storage.
//
// The guarantee concerns LINEAR vertex interpolation, not an angular arc. For
// a midpoint query, every triangle point along [a,b] moves by at most the largest
// endpoint vertex displacement in its group times (b-a)/2. A free query with
// that extra gap therefore certifies the entire interval, including thin walls
// between free endpoints. Unproved intervals are visited strictly left first.
template<class Group, class Free>
SwingPathResult CertifySwingPath(const std::vector<Triangle>& from,
                                const std::vector<Triangle>& to,
                                const std::vector<Group>& groups,
                                Free&& free,
                                const SwingPathOptions& options = SwingPathOptions())
{
    SwingPathResult result;
    if(from.size()!=to.size()||groups.size()!=from.size()||
       !std::isfinite(options.gap)||options.gap<0||
       !std::isfinite(options.minimumMotion)||options.minimumMotion<0||
       !std::isfinite(options.interpolationGuard)||options.interpolationGuard<0)
    {result.limited=true;return result;}

    std::array<std::vector<size_t>,3> indices;
    std::array<std::vector<Triangle>,3> query;
    std::array<double,3> displacement{},coordinateScale{};
    unsigned active=0;
    for(size_t i=0;i<from.size();++i)
    {
        const unsigned group=static_cast<unsigned>(groups[i]);
        if(group>=3){result.limited=true;return result;}
        indices[group].push_back(i);query[group].push_back(from[i]);active|=1u<<group;
        for(int corner=0;corner<3;++corner)
        {
            double squared=0;
            for(int axis=0;axis<3;++axis)
            {
                const double start=from[i].p[corner][axis],end=to[i].p[corner][axis];
                if(!std::isfinite(start)||!std::isfinite(end))
                {result.limited=true;return result;}
                const double delta=end-start;squared+=delta*delta;
                coordinateScale[group]=std::max(coordinateScale[group],std::max(std::abs(start),std::abs(end)));
            }
            displacement[group]=std::max(displacement[group],std::sqrt(squared));
        }
    }
    // The caller has certified the start against this same gun pose. A group
    // with bit-identical endpoint vertices has no interpolation or motion;
    // inherit that certificate instead of demanding an additional guard gap.
    for(unsigned group=0;group<3;++group)
        if(displacement[group]==0)active&=~(1u<<group);
    // Dyadic endpoints through depth 20 are exactly representable in float, so
    // returning a rounded fraction cannot advance beyond a certified endpoint.
    const unsigned depthLimit=std::min(20u,options.maximumDepth);
    auto visit=[&](auto&& self,double a,double b,unsigned pending,unsigned depth)->bool
    {
        const double midpoint=(a+b)*.5,halfWidth=(b-a)*.5;
        double unresolvedMotion=0;
        for(unsigned group=0;group<3;++group)if(pending&(1u<<group))
        {
            if(result.queries>=options.maximumQueries)return false;
            auto& triangles=query[group];const auto& ids=indices[group];
            for(size_t i=0;i<ids.size();++i)
                for(int corner=0;corner<3;++corner)for(int axis=0;axis<3;++axis)
                {
                    const double start=from[ids[i]].p[corner][axis];
                    const double end=to[ids[i]].p[corner][axis];
                    triangles[i].p[corner][axis]=float(start+(end-start)*midpoint);
                }
            const double motion=displacement[group]*halfWidth;
            // Cover float interpolation in all three coordinates. Callers can
            // add their matrix/rendering rounding allowance to the guard.
            const double rounding=options.interpolationGuard+
                4*std::numeric_limits<float>::epsilon()*std::max(1.0,coordinateScale[group]);
            const float expanded=std::nextafter(float(double(options.gap)+motion+rounding),
                                                std::numeric_limits<float>::infinity());
            if(!std::isfinite(expanded))return false;
            ++result.queries;
            if(free(triangles,expanded))pending&=~(1u<<group);
            else unresolvedMotion=std::max(unresolvedMotion,motion);
        }
        if(!pending)
        {
            result.fraction=float(b);++result.certifiedSegments;return true;
        }
        if(depth>=depthLimit||unresolvedMotion<=options.minimumMotion||
           result.queries>=options.maximumQueries)return false;
        // Parent certificates remain valid for both children. This avoids
        // re-querying a stationary clip because the pendant has a long sweep.
        if(!self(self,a,midpoint,pending,depth+1))return false;
        return self(self,midpoint,b,pending,depth+1);
    };
    result.limited=!visit(visit,0,1,active,0);
    return result;
}
}
#endif
