#ifndef OC2_ACCESSORY_MOUNT_CONTACT_H
#define OC2_ACCESSORY_MOUNT_CONTACT_H

#include "accessory_geometry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace oc2_accessory
{
constexpr float kMountContactHalf = .05f;
constexpr float kMountContactGuard = .000002f;

struct MountContactClipStats
{
    std::size_t originalTriangles = 0;
    std::size_t outputTriangles = 0;
    std::size_t contactClippedTriangles = 0;
};

namespace mount_contact_detail
{
struct Point { double p[3]; };
struct Polygon { std::array<Point,12> vertices; unsigned count=0; };
inline bool Same(const Point& a,const Point& b)
{return a.p[0]==b.p[0]&&a.p[1]==b.p[1]&&a.p[2]==b.p[2];}
inline void Append(Polygon& polygon,const Point& point)
{
    if(!polygon.count||!Same(polygon.vertices[polygon.count-1],point))
        polygon.vertices[polygon.count++]=point;
}
inline void Close(Polygon& polygon)
{
    if(polygon.count>1&&Same(polygon.vertices[0],polygon.vertices[polygon.count-1]))--polygon.count;
}
inline void Split(const Polygon& polygon,int axis,double plane,bool lower,
                  Polygon& inside,Polygon& outside)
{
    inside.count=outside.count=0;
    if(!polygon.count)return;
    auto distance=[&](const Point& p){return lower?p.p[axis]-plane:plane-p.p[axis];};
    Point previous=polygon.vertices[polygon.count-1];double before=distance(previous);
    for(unsigned i=0;i<polygon.count;++i)
    {
        const Point current=polygon.vertices[i];const double after=distance(current);
        if((before>=0)!=(after>=0))
        {
            const double fraction=before/(before-after);Point intersection;
            for(int a=0;a<3;++a)intersection.p[a]=previous.p[a]+(current.p[a]-previous.p[a])*fraction;
            intersection.p[axis]=plane;
            Append(inside,intersection);Append(outside,intersection);
        }
        if(after>=0)Append(inside,current);else Append(outside,current);
        previous=current;before=after;
    }
    Close(inside);Close(outside);
}
inline bool HasArea(const Polygon& polygon)
{
    for(unsigned i=2;i<polygon.count;++i)
    {
        double a[3],b[3];for(int j=0;j<3;++j)
        {a[j]=polygon.vertices[i-1].p[j]-polygon.vertices[0].p[j];b[j]=polygon.vertices[i].p[j]-polygon.vertices[0].p[j];}
        if(a[1]*b[2]-a[2]*b[1]!=0||a[2]*b[0]-a[0]*b[2]!=0||a[0]*b[1]-a[1]*b[0]!=0)return true;
    }
    return false;
}
inline void Fan(const Polygon& polygon,const Triangle& source,std::vector<Triangle>& out)
{
    if(!polygon.count)return;
    // Degenerate input faces still represent real collision segments/points.
    const unsigned count=std::max(3u,polygon.count);
    for(unsigned i=2;i<count;++i)
    {
        Triangle triangle=source;
        const unsigned corners[3]={0,std::min(i-1,polygon.count-1),std::min(i,polygon.count-1)};
        for(int c=0;c<3;++c)for(int a=0;a<3;++a)triangle.p[c][a]=float(polygon.vertices[corners[c]].p[a]);
        out.push_back(triangle);
    }
}
}

// Collision-only installation contact policy: remove the tiny cube centered
// at (0,0,.01) in gun-mount coordinates. This does not hide rendered geometry,
// move a root, or exempt a clip/chain/pendant component from collision checks.
// Every part of a crossing face outside the cube is retained and tested.
//
// For a CCD midpoint query with separation baseGap+B+interpolationGuard, pass
// B+interpolationGuard as extraMotion. Only a portion guaranteed to remain in
// the allowed contact region may be ignored. The additional inward guard
// covers float output rounding; exhausted motion allowance retains every face.
inline MountContactClipStats TrianglesOutsideMountContact(const std::vector<Triangle>& input,
                                                         std::vector<Triangle>& output,
                                                         float extraMotion=0,int contactBone=-1)
{
    if(&input==&output)
    {
        const auto saved=input;
        return TrianglesOutsideMountContact(saved,output,extraMotion,contactBone);
    }
    MountContactClipStats stats;stats.originalTriangles=input.size();output.clear();output.reserve(input.size());
    const double half=std::isfinite(extraMotion)?
        std::max(0.0,double(kMountContactHalf)-std::max(0.0,double(extraMotion))-kMountContactGuard):0;
    if(half<=0)
    {output=input;stats.outputTriangles=output.size();return stats;}
    const double low[3]={-half,-half,.01-half},high[3]={half,half,.01+half};
    std::vector<Triangle> exterior;exterior.reserve(16);
    for(const Triangle& triangle:input)
    {
        // Only the rigid installation neck may contact the gun at its root.
        // Moving chain and pendant faces remain complete even inside the port.
        if(contactBone>=0&&triangle.bone!=contactBone){output.push_back(triangle);continue;}
        using namespace mount_contact_detail;
        Polygon remainder;remainder.count=3;bool finite=true,disjoint=false,contained=true;
        for(int axis=0;axis<3;++axis)
        {
            double lo=triangle.p[0][axis],hi=lo;
            for(int c=0;c<3;++c)
            {
                const double value=triangle.p[c][axis];remainder.vertices[c].p[axis]=value;
                finite=finite&&std::isfinite(value);lo=std::min(lo,value);hi=std::max(hi,value);
            }
            disjoint=disjoint||hi<low[axis]||lo>high[axis];contained=contained&&lo>=low[axis]&&hi<=high[axis];
        }
        if(!finite||disjoint){output.push_back(triangle);continue;}
        if(contained){++stats.contactClippedTriangles;continue;}
        const bool originalArea=HasArea(remainder);exterior.clear();
        for(int plane=0;plane<6&&remainder.count;++plane)
        {
            const int axis=plane/2;const bool lower=(plane%2)==0;
            Polygon inside,outside;Split(remainder,axis,lower?low[axis]:high[axis],lower,inside,outside);
            Fan(outside,triangle,exterior);remainder=inside;
        }
        // A disjoint or merely tangent face needs no new triangulation.
        if(!remainder.count||(originalArea&&!HasArea(remainder)))output.push_back(triangle);
        else
        {
            ++stats.contactClippedTriangles;
            output.insert(output.end(),exterior.begin(),exterior.end());
        }
    }
    stats.outputTriangles=output.size();return stats;
}
}
#endif
