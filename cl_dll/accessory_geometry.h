#ifndef OC2_ACCESSORY_GEOMETRY_H
#define OC2_ACCESSORY_GEOMETRY_H

#include <vector>
#include <cstdint>
// GoldSrc declares an anonymous studiohdr_t, so it cannot be forward-declared.
#ifndef _STUDIO_H_
typedef float vec3_t[3];
typedef unsigned char byte;
#include "../engine/studio.h"
#endif

namespace oc2_accessory
{
constexpr float kMinimumSideFacing = .5f; // At most 60 degrees from sticker side.
struct Mount
{
    int bone = -1;
    float origin[3] = {};
    float axisX[3] = {};
    float axisY[3] = {};
    float normal[3] = {};
    float requestedUV[2] = {};
    float actualUV[2] = {};
    float snapDistance = 0; // Bind-space units, measured in the common side plane.
};

struct Frame
{
    float origin[3] = {};
    float axisX[3] = {1, 0, 0};
    float axisY[3] = {0, 1, 0};
    float normal[3] = {0, 0, 1};
};

struct Bounds { float min[3] = {}; float max[3] = {}; };
struct Triangle
{
    float p[3][3] = {};
    float normal[3] = {}; // Average authored normal in the supplied pose space.
    int bone = -1;        // -1 for a triangle whose vertices have different bones.
    int group = 0;        // Distinct real bodypart/read call for containment tests.
};
struct StudioCorner { int bone; int normalBone; float p[3]; float normal[3]; };
struct StudioTriangle
{
    StudioCorner corner[3];
    int part, model, material;
    bool decal, chrome, placeholder;
    bool skin0Real = false; // Header-static material/name/flags, decoded once.
};
struct StudioPart { int base, models; };
struct StudioTriangleCache
{
    const studiohdr_t* header = nullptr;
    std::vector<StudioPart> parts;
    std::vector<StudioTriangle> triangles;
};
struct BindMatrix { float m[3][4]; };
struct MountCache
{
    StudioTriangleCache studio;
    std::vector<BindMatrix> bind;
    std::vector<Triangle> surfaces;
    std::vector<int> originalDecalBones;
    std::vector<int> weaponBones; // All real body components, including deforming seams.
    std::vector<std::vector<int>> surfaceBins;
    int surfaceGridSize = 0;
    Frame side;
    float minUV[2] = {}, maxUV[2] = {};
    bool valid = false;
};

// Decode once per loaded header. Cached overload avoids per-frame strip parsing.
bool DecodeStudioTriangles(const studiohdr_t* header, StudioTriangleCache& out);
bool ReadStudioTriangles(const StudioTriangleCache& cache, int body, int skin,
                         const float (*poseMatrices)[3][4], std::vector<Triangle>& out,
                         bool append = false);
bool ReadStudioTriangles(const studiohdr_t* header, int body, int skin,
                         const float (*poseMatrices)[3][4], std::vector<Triangle>& out,
                         bool append = false);
bool BuildMountCache(const studiohdr_t* header, MountCache& out);
bool FindMount(const MountCache& cache, float u, float v, Mount& out);
struct MountViewConstraint
{
    const float (*poseMatrices)[3][4] = nullptr;
    float viewOrigin[3] = {};
    float viewForward[3] = {};
    float minimumOriginDepth = 1;
};
// Same whole-gun side/front surface, additionally clipped against a
// posed camera-depth half-plane. The caller still validates its real vertices
// after posing and collision, since this constraint concerns the mount origin.
bool FindMountInFront(const MountCache& cache, float u, float v,
                      const MountViewConstraint& view, Mount& out);

// Only call on a blocked placement. Candidates remain on actual frontmost
// rigid gun triangles and are sorted by normalized UV distance to the request.
// Increase maximumCandidates if the first finite search cannot install a model.
bool FindMountCandidates(const MountCache& cache, float u, float v,
                         std::vector<Mount>& out, unsigned maximumCandidates = 64,
                         const MountViewConstraint* view = nullptr);

struct CollisionNode
{
    Bounds bounds;
    int left = -1, right = -1, first = 0, count = 0;
};
struct CollisionGroup
{
    int id = 0;
    Bounds bounds;
    bool closed = false; // Exact paired edges; open meshes never claim a solid.
    bool topologyClosed = false;
};
struct CollisionEdgePair { int group, triangleA, cornerA, triangleB, cornerB; bool reverse; };
struct MeshCollider
{
    std::vector<Triangle> triangles;
    std::vector<int> order;
    std::vector<CollisionNode> nodes;
    std::vector<CollisionGroup> groups;
    std::vector<CollisionEdgePair> closedEdges;
};
struct CollisionResult
{
    bool blocked = false, surfaceContact = false, contained = false;
    int accessoryTriangle = -1, obstacleTriangle = -1, containingGroup = -1;
    unsigned trianglePairs = 0, containmentSamples = 0, closedGroups = 0;
    unsigned planeSeparatedPairs = 0, axisSeparatedPairs = 0, distancePairs = 0;
    float distance = 0;
};
// Build once, then refit once per posed weapon/arms frame for all placement and
// angle candidates. A changed triangle count/group sequence rebuilds the tree.
bool BuildMeshCollider(const std::vector<Triangle>& triangles, MeshCollider& out);
bool RefitMeshCollider(const std::vector<Triangle>& triangles, MeshCollider& cache);
// AABBs only prune work. The last stage measures actual triangle/edge distance,
// including coplanar/degenerate faces. minimumSeparation is a physical guard
// margin, not permission to penetrate or to translate an attachment root.
// Closed obstacle groups additionally reject actual accessory vertices inside
// their volumes, even when no triangle surfaces cross.
CollisionResult CheckMeshCollision(const MeshCollider& obstacles,
                                   const std::vector<Triangle>& accessory,
                                   float minimumSeparation = .0001f);

// Frame axes must be orthonormal; frame and triangles share a space, and bounds
// are in that frame. Returns the
// translation along +frame.normal. Start outside the mount plane, then resolve
// only finite Z-volume contacts in the exactly clipped XY rectangle. Disjoint
// distant foreground triangles do not push the attachment. Degenerate projected
// edges are conservative. With no contact the result is max(0,gap-bounds.minZ).
float ComputeOutwardClearance(const Frame& frame, const Bounds& bounds,
                              const std::vector<Triangle>& triangles, float gap = .02f,
                              float minimumOffset = 0);

struct SwingConfig
{
    float amplitude = 1;
    float length = 2;
    float stiffness = 45;
    float damping = 8;
    float maxAngle = 35; // Degrees, per axis.
};
struct SwingInput
{
    double time = 0;
    unsigned resetToken = 0; // Change on weapon/header/attachment replacement.
    float anchor[3] = {};   // Common world-space anchor, only for teleport detection.
    float anchorAcceleration[3] = {}; // Attachment-frame units/second^2.
    float gravity[3] = {};             // Same frame as acceleration.
    bool teleport = false;
};
struct SwingOutput
{
    float pitch = 0; // Degrees about +local X.
    float yaw = 0;   // Degrees about +local Z; neutral chain points down -local Y.
};
struct SwingState
{
    bool initialized = false;
    unsigned resetToken = 0, resets = 0, steps = 0;
    double lastTime = 0, simulationTime = 0;
    float angle[2] = {}, previousAngle[2] = {}, velocity[2] = {};
    float anchor[3] = {}, acceleration[3] = {}, gravity[3] = {};
};
void ResetSwing(SwingState& state, const SwingInput& input);
SwingOutput UpdateSwing(SwingState& state, const SwingConfig& config, const SwingInput& input);
}
#endif
