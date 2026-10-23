#ifndef OC2_ACCESSORY_GEOMETRY_H
#define OC2_ACCESSORY_GEOMETRY_H

#include <vector>
#include <array>
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
    int bone = -1; // Actual rigid surface owner, including moving gun parts.
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
struct StudioPosition { int bone;float p[3]; };
struct PosedPosition { float p[3];unsigned generation=0; };
struct StudioTriangleCache
{
    const studiohdr_t* header = nullptr;
    std::vector<StudioPart> parts;
    std::vector<StudioTriangle> triangles;
    std::vector<StudioPosition> positions;
    std::vector<std::array<int,3>> positionIndices;
    mutable std::vector<PosedPosition> posedPositions;
    mutable unsigned poseGeneration=0;
    mutable int materialSkin = -1;
    mutable std::vector<unsigned char> realMaterials;
};
struct BindMatrix { float m[3][4]; };
struct MountCache
{
    StudioTriangleCache studio;
    std::vector<BindMatrix> bind;
    std::vector<Triangle> surfaces;
    std::vector<MountCache> regions; // Dual guns: independent sticker sides, horizontal UV atlas.
    int rigRoot = -1;
    std::vector<int> originalDecalBones;
    std::vector<int> weaponBones; // All real body components, including deforming seams.
    std::vector<std::vector<int>> surfaceBins;
    int surfaceGridSize = 0;
    Frame side;
    float minUV[2] = {}, maxUV[2] = {};
    bool valid = false;
};

// Decode once per loaded header. Cached overload avoids per-frame strip parsing.
// A nonempty allowedBones keeps whole triangles with any matching corner,
// including mixed seams. Null or empty preserves the complete real mesh.
bool DecodeStudioTriangles(const studiohdr_t* header, StudioTriangleCache& out);
bool ReadStudioTriangles(const StudioTriangleCache& cache, int body, int skin,
                         const float (*poseMatrices)[3][4], std::vector<Triangle>& out,
                          bool append = false, bool positionsOnly = false,
                          const std::vector<int>* allowedBones = nullptr);
bool ReadStudioTriangles(const studiohdr_t* header, int body, int skin,
                         const float (*poseMatrices)[3][4], std::vector<Triangle>& out,
                          bool append = false, const std::vector<int>* allowedBones = nullptr);
bool BuildMountCache(const studiohdr_t* header, MountCache& out);
// Maximum actual part-vertex departure from its bind socket in receiver space.
// Measures geometry only; no sequence, timing or collision-gap classification.
bool BoneSocketDisplacement(const MountCache& cache,int receiver,int part,
                            const float (*poseMatrices)[3][4],float& maxDistance);
bool FindMount(const MountCache& cache, float u, float v, Mount& out);
// 0 = no rigid side face, 1 = installable, including articulated components.
// Uses the same frontmost surface as placement and the menu's exported mask.
int MountDomainAt(const MountCache& cache, float u, float v);
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
struct CollisionPlane
{
    double normal[3]{},errorN[3]{},marginLength=0;
    float origin[3]{};unsigned epoch=0;bool valid=false;
};
struct MeshCollider
{
    std::vector<Triangle> triangles;
    std::vector<int> order;
    std::vector<CollisionNode> nodes;
    std::vector<CollisionGroup> groups;
    std::vector<CollisionEdgePair> closedEdges;
    std::vector<Bounds> triangleBounds;
    std::vector<int> triangleGroups;
    mutable std::vector<CollisionPlane> planes;
    unsigned planeEpoch=1;
    bool surfaceOnly=false;
    std::uint64_t geometrySerial=0;
    // Only a search hint: both current triangles are checked again at the
    // requested gap before it can reject a pose, including after every refit.
    mutable int contactObstacle=-1,contactAccessory=-1;
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
bool BuildMeshCollider(const std::vector<Triangle>& triangles, MeshCollider& out,bool closedGroups=true);
bool RefitMeshCollider(const std::vector<Triangle>& triangles, MeshCollider& cache,bool closedGroups=true);
// AABBs only prune work. The last stage measures actual triangle/edge distance,
// including coplanar/degenerate faces. minimumSeparation is a physical guard
// margin, not permission to penetrate or to translate an attachment root.
// Closed obstacle groups additionally reject actual accessory vertices inside
// their volumes, even when no triangle surfaces cross.
CollisionResult CheckMeshCollision(const MeshCollider& obstacles,
                                   const std::vector<Triangle>& accessory,
                                   float minimumSeparation = .0001f);
CollisionResult CheckMeshCollision(const MeshCollider& obstacles,const MeshCollider& accessory,
                                   float minimumSeparation = .0001f);

// A free pose certified at a larger gap can be reused only while actual mesh
// displacement proves the original .0001 clearance still holds. Moving bones,
// changing topology and nearby hands invalidate it; no frame is skipped by time.
constexpr float kCertifiedCollisionGap = .001f;
struct CollisionMemo
{
    MeshCollider obstacles;
    std::vector<Triangle> accessory;
    Bounds bounds;
    bool valid = false;
    float separation=kCertifiedCollisionGap;
    const MeshCollider* owner=nullptr;
    std::uint64_t geometrySerial=0;
};
bool ReuseMeshCollision(const MeshCollider& obstacles,const std::vector<Triangle>& accessory,
                        const CollisionMemo& memo,CollisionResult& result,float minimumSeparation=.0001f,
                        const std::vector<int>* uniqueCorners=nullptr);
void RememberMeshCollision(const MeshCollider& obstacles,const std::vector<Triangle>& accessory,
                           CollisionMemo& memo,float separation=kCertifiedCollisionGap);

struct BoneEnvelope { int bone;Bounds local; };
struct PoseGeometry
{
    std::vector<BoneEnvelope> bones;
    std::vector<int> triangles,uniqueCorners;
    std::vector<std::pair<int,int>> links;
};
struct PoseSource
{
    const StudioTriangleCache* mesh=nullptr;
    const PoseGeometry* geometry=nullptr;
    int body=0,skin=0;
    std::vector<BindMatrix> matrices;
};
struct BoneMetric
{
    Bounds local,bounds;
    double matrix[3][4]{},rounding[3]{};
};
struct ClosurePoint { int source,bone;float p[3]; };
struct PoseClosure { int group;ClosurePoint a,b; };
bool BuildPoseGeometry(const StudioTriangleCache& mesh,int body,int skin,PoseGeometry& out,bool collectCorners=false,
                       const std::vector<int>* allowedBones=nullptr);
bool BuildPoseMetrics(const std::vector<PoseSource>& sources,const Frame& frame,std::vector<BoneMetric>& out);
// Bounds include both float stages (world skinning, then gun-aligned projection).
// Far bone sweeps are excluded only by disjoint conservative envelopes. A free
// query must use minimumSeparation + returned error against the retained mesh.
bool PoseDisplacementBound(const std::vector<BoneMetric>& old,const std::vector<BoneMetric>& current,
                           const std::vector<Triangle>& query,float& error,bool allBones=false);
bool BuildPoseClosures(const MeshCollider& collider,const std::vector<PoseSource>& sources,std::vector<PoseClosure>& out);
bool PoseClosuresUnchanged(const MeshCollider& collider,const std::vector<PoseClosure>& closures,
                           const std::vector<PoseSource>& sources,const Frame& frame);

struct BoneSceneSource
{
    const StudioTriangleCache* mesh=nullptr;
    const PoseGeometry* geometry=nullptr;
    int body=0,skin=0,firstMetric=0,firstTriangle=0;
};
struct BoneSceneRigid
{
    int source=0,bone=0,metric=0;
    MeshCollider collider;
    std::vector<int> originalTriangles;
};
struct BoneSceneGroup
{
    int id=0,rigid=-1;
    std::vector<int> metrics;
    bool topologyClosed=false,unsupported=false;
};
struct BoneScene
{
    std::vector<BoneSceneSource> sources;
    std::vector<BoneSceneRigid> rigid;
    std::vector<BoneSceneGroup> groups;
    // source/raw-triangle, preserving the original scene's triangle indices.
    std::vector<std::array<int,3>> mixed;
    mutable std::vector<std::pair<const MeshCollider*,MeshCollider>> localQueries;
    mutable std::vector<std::vector<CollisionMemo>> rigidMemos;
    mutable std::vector<std::vector<unsigned>> certifyCooldown;
    mutable MeshCollider mixedCollider;
    mutable std::vector<Triangle> transformed,mixedTriangles;
    bool valid=false;
};
// Build only when the actual source/body/skin list changes. Reference supplies
// its original closed-group topology; obstacle triangles stay in bone space.
bool BuildBoneScene(const std::vector<PoseSource>& sources,const MeshCollider& reference,BoneScene& out);
// false means unsupported and requires the original posed scene check. A true
// blocked result may be conservative; true/free certifies the requested gap.
// Metrics must be from these sources in BuildPoseMetrics order, and query must
// already be in frame coordinates. Reflections are supported; scaling is not.
bool CheckBoneScene(const BoneScene& scene,const std::vector<PoseSource>& sources,
                    const std::vector<BoneMetric>& metrics,const Frame& frame,
                    const MeshCollider& query,CollisionResult& result,float gap=.0001f);

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
