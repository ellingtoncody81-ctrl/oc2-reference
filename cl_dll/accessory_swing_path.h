#ifndef OC2_ACCESSORY_SWING_PATH_H
#define OC2_ACCESSORY_SWING_PATH_H

#include "accessory_geometry.h"
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
