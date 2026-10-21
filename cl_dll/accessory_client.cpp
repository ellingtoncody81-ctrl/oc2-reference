#include "hud.h"
#include "cl_util.h"
#include "cl_entity.h"
#include "com_model.h"
#include "studio.h"
#include "r_studioint.h"
#include "studio_util.h"
#include "StudioModelRenderer.h"
#include "demo_api.h"
#include "render_api.h"
#include "oc2_accessories.h"
#include "accessory_client.h"
#include "accessory_geometry.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

extern engine_studio_api_t IEngineStudio;
extern render_api_t gRenderAPI;

namespace
{
using namespace oc2_accessories;
Config settings=DefaultConfig();
Counts counters=DefaultCounts();
Catalog catalog=DefaultCatalog();
double nextRead=-1,lastTime=-1;
std::map<int,int> stickerTextures;
std::string level;
bool readError=false;

bool LoadText(const char *path,std::string &text,int maximum,bool *exists=nullptr)
{
 int length=0;byte *data=gEngfuncs.COM_LoadFile(const_cast<char *>(path),5,&length);
 if(exists)
 {
  *exists=data!=nullptr;
  if(!data)
  {
   int files=0;
   if(gRenderAPI.pfnGetFilesList)gRenderAPI.pfnGetFilesList(path,&files,1);
   *exists=files>0||!gRenderAPI.pfnGetFilesList;
  }
 }
 if(!data)return false;
 const bool valid=length>=0&&length<=maximum&&!memchr(data,0,length);
 if(valid)text.assign(reinterpret_cast<char *>(data),length);
 gEngfuncs.COM_FreeFile(data);
 return valid;
}
void ReadSettings(double time,bool force=false)
{
 if(!force&&time>=lastTime&&time<nextRead)return;
 lastTime=time;nextRead=time+.25;
 std::string text,error;Config candidate;
 if(LoadText(kConfigPath,text,16384))
 {
  if(ParseConfig(text.c_str(),candidate,error)){settings=candidate;readError=false;}
  else if(!readError){gEngfuncs.Con_Printf("accessories: %s\n",error.c_str());readError=true;}
 }
 if(LoadText(kCountsPath,text,4096))
 {
  Counts current;if(ParseCounts(text.c_str(),current,error))counters=current;
 }
 if(LoadText(kCatalogPath,text,131072))
 {
  Catalog current;if(ParseCatalog(text.c_str(),current,error))catalog=current;
 }
}
int WeaponId(const char *name)
{
 if(!name)return 0;
 if(!strncmp(name,"weapon_",7))name+=7;
 const char *const names[24]={"p228","scout","xm1014","mac10","aug","elite","fiveseven","ump45",
  "sg550","galil","famas","usp","glock18","awp","mp5navy","m249","m3","m4a1","tmp","g3sg1","deagle","sg552","ak47","p90"};
 for(int i=0;i<24;++i)if(!strcmp(name,names[i]))return kWeaponIds[i];
 struct Alias{const char *name;int id;};
 const Alias aliases[]={{"p250",1},{"ssg08",3},{"scar20",13},{"galilar",14},{"usp_silencer",16},
  {"glock",17},{"mp5",19},{"mp7",19},{"nova",21},{"m4a1_silencer",22},{"mp9",23},{"sg553",27}};
 for(const auto &a:aliases)if(!strcmp(name,a.name))return a.id;return 0;
}
int StickerTexture(int id)
{
 const Sticker *selected=nullptr;
 for(const auto &item:catalog)if(item.id==id){selected=&item;break;}
 if(!selected&&!catalog.empty())selected=&catalog.front();
 if(!selected||!SafeStickerFile(selected->file)||!gRenderAPI.GL_LoadTexture)return 0;
 auto found=stickerTextures.find(selected->id);if(found!=stickerTextures.end())return found->second;
 const std::string path=std::string(kStickerDirectory)+selected->file;
 const int texture=gRenderAPI.GL_LoadTexture(path.c_str(),nullptr,0,TF_CLAMP);
 if(texture)stickerTextures[selected->id]=texture;
 return texture;
}

struct Diagnostics
{
 int weapon=0,drawn=0,statBone=-1,charmBone=-1,sticker=0;
 float statOffset=0,charmOffset=0,statRequired=0,charmRequired=0,pitch=0,yaw=0,minDepth=1e20f,boxMinDepth=1e20f;
 float statUV[2]{},charmUV[2]{};
 unsigned steps=0,resets=0;int triangles=0;
 int statNearIterations=0,charmNearIterations=0;
 bool statNearClamped=false,charmNearClamped=false;
 float contactGap[2]{},contactLateral[2]{};
 bool blocked[2]{},surfaceAdjusted[2]{},swingLimited=false;
 unsigned testedCandidates[2]{};
 double drawMs[2]{},refitMs[2]{},collisionMs[2]{};
 unsigned collisionPairs[2]{},containmentSamples[2]{};
 bool statEnabled=false,charmEnabled=false;
 unsigned count=0;
} diag;
void Dump()
{
 ReadSettings(gEngfuncs.GetClientTime(),true);
 const int index=Index(diag.weapon);const unsigned count=index<0?0:counters.values[index];
 gEngfuncs.Con_Printf("oc2accessory: weapon=%d stat=%d charm=%d drawn=%d count=%u stat_bone=%d charm_bone=%d stat_offset=%.6f charm_offset=%.6f pitch=%.6f yaw=%.6f sticker=%d revision=%u read_error=%d\n",
  diag.weapon,int(diag.statEnabled),int(diag.charmEnabled),diag.drawn,count,diag.statBone,diag.charmBone,
  diag.statOffset,diag.charmOffset,diag.pitch,diag.yaw,diag.sticker,settings.revision,int(readError));
 gEngfuncs.Con_Printf("oc2accessory_geometry: stat_uv=%.6f/%.6f charm_uv=%.6f/%.6f required=%.6f/%.6f min_depth=%.6f box_min_depth=%.6f triangles=%d steps=%u resets=%u near_clamp=%d/%d near_iterations=%d/%d\n",
  diag.statUV[0],diag.statUV[1],diag.charmUV[0],diag.charmUV[1],diag.statRequired,diag.charmRequired,diag.minDepth,diag.boxMinDepth,diag.triangles,diag.steps,diag.resets,
  int(diag.statNearClamped),int(diag.charmNearClamped),diag.statNearIterations,diag.charmNearIterations);
 gEngfuncs.Con_Printf("oc2accessory_contact: gap=%.6f/%.6f lateral=%.6f/%.6f blocked=%d/%d adjusted=%d/%d swing_limited=%d candidates=%u/%u\n",
  diag.contactGap[0],diag.contactGap[1],diag.contactLateral[0],diag.contactLateral[1],
  int(diag.blocked[0]),int(diag.blocked[1]),int(diag.surfaceAdjusted[0]),int(diag.surfaceAdjusted[1]),
  int(diag.swingLimited),diag.testedCandidates[0],diag.testedCandidates[1]);
 gEngfuncs.Con_Printf("oc2accessory_cpu: draw_ms=%.4f/%.4f refit_ms=%.4f/%.4f collision_ms=%.4f/%.4f pairs=%u/%u containment=%u/%u\n",
  diag.drawMs[0],diag.drawMs[1],diag.refitMs[0],diag.refitMs[1],diag.collisionMs[0],diag.collisionMs[1],
  diag.collisionPairs[0],diag.collisionPairs[1],diag.containmentSamples[0],diag.containmentSamples[1]);
}
void Reload() { nextRead=-1;stickerTextures.clear();ReadSettings(gEngfuncs.GetClientTime(),true); }
}

void OC2_AccessoryKill(const char *weapon,int killer,int victim,int killerTeam,int victimTeam)
{
 const cl_entity_t *local=gEngfuncs.GetLocalPlayer();
 if(!local||killer!=local->index||killer==victim||victim<1||victim>MAX_PLAYERS||
  (killerTeam!=1&&killerTeam!=2)||(victimTeam!=1&&victimTeam!=2)||killerTeam==victimTeam)return;
 if(gEngfuncs.pDemoAPI&&gEngfuncs.pDemoAPI->IsPlayingback())return;
 const int id=WeaponId(weapon),index=Index(id);if(index<0)return;
 ReadSettings(gEngfuncs.GetClientTime(),true);
 const WeaponConfig *cfg=Find(settings,id);if(!cfg||!cfg->stattrak||!gRenderAPI.pfnSaveFile)return;
 // Read-modify-write the separate counter store for this authoritative event.
 // This observes a menu reset even when a kill arrives before the next draw.
 Counts current=DefaultCounts();std::string text,error;bool exists=false;
 const bool loaded=LoadText(kCountsPath,text,4096,&exists);
 if((exists&&!loaded)||(loaded&&!ParseCounts(text.c_str(),current,error)))return;
 if(current.values[index]<kMaximumCount)++current.values[index];
 text=SerializeCounts(current);
 if(gRenderAPI.pfnSaveFile(kCountsPath,text.data(),static_cast<int>(text.size())))counters=current;
 else gEngfuncs.Con_Printf("accessories: could not save kill counter\n");
}

void OC2_AccessoriesInit()
{
 gEngfuncs.pfnAddCommand("oc2_accessory_dump",Dump);
 gEngfuncs.pfnAddCommand("oc2_accessory_reload",Reload);
}

namespace
{
namespace geo=oc2_accessory;
struct Matrix { float m[3][4]{}; };
Matrix Identity() { Matrix a;for(int i=0;i<3;++i)a.m[i][i]=1;return a; }
Matrix Product(const Matrix &a,const Matrix &b)
{
 Matrix c;
 for(int i=0;i<3;++i)for(int j=0;j<4;++j)
 {
  c.m[i][j]=a.m[i][0]*b.m[0][j]+a.m[i][1]*b.m[1][j]+a.m[i][2]*b.m[2][j];
  if(j==3)c.m[i][j]+=a.m[i][3];
 }
 return c;
}
void Point(const float m[3][4],const float *p,float *out)
{ for(int i=0;i<3;++i)out[i]=m[i][0]*p[0]+m[i][1]*p[1]+m[i][2]*p[2]+m[i][3]; }
void Direction(const float m[3][4],const float *p,float *out)
{ for(int i=0;i<3;++i)out[i]=m[i][0]*p[0]+m[i][1]*p[1]+m[i][2]*p[2]; }
float Dot(const float *a,const float *b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
void Unit(float *p) { float n=std::sqrt(Dot(p,p));if(n>1e-8f)for(int i=0;i<3;++i)p[i]/=n; }
Matrix FrameMatrix(const geo::Frame &f)
{
 Matrix m;for(int i=0;i<3;++i){m.m[i][0]=f.axisX[i];m.m[i][1]=f.axisY[i];m.m[i][2]=f.normal[i];m.m[i][3]=f.origin[i];}return m;
}
geo::Frame MountFrame(const geo::Mount &mount,const float pose[3][4])
{
 geo::Frame f;Point(pose,mount.origin,f.origin);Direction(pose,mount.axisX,f.axisX);
 Direction(pose,mount.axisY,f.axisY);Direction(pose,mount.normal,f.normal);
 Unit(f.axisX);Unit(f.axisY);Unit(f.normal);return f;
}
Matrix Rotation(float pitch,float yaw)
{
 const float p=pitch*.0174532925199433f,y=yaw*.0174532925199433f;
 Matrix x=Identity(),z=Identity();x.m[1][1]=x.m[2][2]=std::cos(p);x.m[1][2]=-std::sin(p);x.m[2][1]=std::sin(p);
 z.m[0][0]=z.m[1][1]=std::cos(y);z.m[0][1]=-std::sin(y);z.m[1][0]=std::sin(y);return Product(z,x);
}
bool BindBones(studiohdr_t *h,std::array<Matrix,MAXSTUDIOBONES> &out)
{
 if(!h||h->numbones<1||h->numbones>MAXSTUDIOBONES)return false;
 auto bones=reinterpret_cast<mstudiobone_t *>(reinterpret_cast<byte *>(h)+h->boneindex);
 for(int i=0;i<h->numbones;++i)
 {
  vec4_t q;AngleQuaternion(&bones[i].value[3],q);Matrix local;QuaternionMatrix(q,local.m);
  for(int j=0;j<3;++j)local.m[j][3]=bones[i].value[j];
  if(bones[i].parent>=i||bones[i].parent< -1)return false;
  out[i]=bones[i].parent<0?local:Product(out[bones[i].parent],local);
 }
 return true;
}
struct Joint { int parent=-1;Matrix local,bind,inverse; };
struct Carrier { std::string bone;std::vector<std::pair<int,float>> links; };
std::vector<Joint> joints;
std::vector<Carrier> carriers;
bool skinAttempted=false,skinValid=false;
bool ReadMatrix(std::istringstream &s,Matrix &m)
{ for(auto &row:m.m)for(float &v:row)if(!(s>>v)||!std::isfinite(v))return false;return true; }
bool LoadSkin()
{
 if(skinAttempted)return skinValid;skinAttempted=true;
 std::string text,magic;int version=0,count=0;
 if(!LoadText("models/attachments/charm.skin",text,32768))return false;
 std::istringstream input(text);
 if(!(input>>magic>>version)||magic!="OC2_ATTACHMENT_SKIN"||version!=1||!(input>>magic>>count)||magic!="joints"||count<1||count>16)return false;
 std::vector<Joint> parsed(count);
 for(int i=0;i<count;++i)
 {
  int id;std::string name;Joint &j=parsed[i];
  if(!(input>>magic>>id>>j.parent>>name)||magic!="joint"||id!=i||j.parent>=i||j.parent< -1||
   !ReadMatrix(input,j.local)||!ReadMatrix(input,j.bind)||!ReadMatrix(input,j.inverse))return false;
 }
 if(!(input>>magic>>count)||magic!="carriers"||count<1||count>MAXSTUDIOBONES)return false;
 std::vector<Carrier> weights(count);
 for(int i=0;i<count;++i)
 {
  int id,n;if(!(input>>magic>>id>>weights[i].bone>>n)||magic!="carrier"||id!=i||n<1||n>4)return false;
  float sum=0;
  for(int k=0;k<n;++k)
  {
   int joint;float weight;if(!(input>>joint>>weight)||joint<0||joint>=int(parsed.size())||!Range(weight,0,1))return false;
   weights[i].links.push_back(std::make_pair(joint,weight));sum+=weight;
  }
  if(std::fabs(sum-1)>.00001f)return false;
 }
 if(!End(input))return false;
 joints=parsed;carriers=weights;skinValid=true;return true;
}

std::map<const studiohdr_t *,geo::MountCache> mounts;
std::map<const studiohdr_t *,geo::StudioTriangleCache> meshes;
std::vector<geo::Triangle> occluders;
geo::SwingState swing;
struct Motion
{
 double time=-1;float anchor[3]{},velocity[3]{},acceleration[3]{};
 int weapon=0;float statOffset=0,charmOffset=0;unsigned token=0,revision=0;
 const studiohdr_t *header=nullptr;bool left=false;
 double statTime=-1,charmTime=-1;
 float statUV[2]{},charmUV[2]{};
 bool statPlaced=false,charmPlaced=false;
} motion;
std::array<geo::MeshCollider,2> attachmentObstacles;
bool counterColliderReady=false;
struct ObstacleSource
{
 const studiohdr_t *header;int body,skin;
 bool operator==(const ObstacleSource &other)const{return header==other.header&&body==other.body&&skin==other.skin;}
};
std::vector<ObstacleSource> obstacleSources,previousObstacleSources;
geo::MountCache *weaponSurface=nullptr;
std::map<const studiohdr_t *,int> colliderBones;
geo::Frame collisionFrame;
void SetCollisionFrame(const geo::MountCache &cache,const float (*pose)[3][4],const WeaponConfig &cfg)
{
 auto found=colliderBones.find(cache.studio.header);
 if(found==colliderBones.end())
 {
  std::map<int,unsigned> counts;
  for(const auto &triangle:cache.surfaces)if(triangle.bone>=0)++counts[triangle.bone];
  int bone=0;unsigned most=0;
  for(const auto &entry:counts)if(entry.second>most){bone=entry.first;most=entry.second;}
  found=colliderBones.insert(std::make_pair(cache.studio.header,bone)).first;
 }
 int bone=found->second;
 geo::Mount preferred;
 // The larger articulated mesh benefits most from aligned bounds. This also
 // handles dual pistols whose two gun bones rotate independently on inspect.
 if(geo::FindMount(cache,cfg.charm?cfg.charmU:cfg.statU,cfg.charm?cfg.charmV:cfg.statV,preferred))
  bone=preferred.bone;
 collisionFrame=geo::Frame();
 if(bone<0||bone>=int(cache.bind.size()))return;
 const auto &bind=cache.bind[bone].m;
 const float *source[2]={cache.side.axisX,cache.side.axisY};
 float *axes[2]={collisionFrame.axisX,collisionFrame.axisY};
 for(int axis=0;axis<2;++axis)
 {
  float local[3];
  for(int i=0;i<3;++i)local[i]=bind[0][i]*source[axis][0]+bind[1][i]*source[axis][1]+bind[2][i]*source[axis][2];
  Direction(pose[bone],local,axes[axis]);
 }
 Unit(collisionFrame.axisX);
 const float parallel=Dot(collisionFrame.axisX,collisionFrame.axisY);
 for(int i=0;i<3;++i)collisionFrame.axisY[i]-=parallel*collisionFrame.axisX[i];
 Unit(collisionFrame.axisY);
 for(int i=0;i<3;++i)
 {
  collisionFrame.normal[i]=collisionFrame.axisX[(i+1)%3]*collisionFrame.axisY[(i+2)%3]-
   collisionFrame.axisX[(i+2)%3]*collisionFrame.axisY[(i+1)%3];
  collisionFrame.origin[i]=pose[bone][i][3];
 }
 // A common orthonormal coordinate change preserves actual distances, even
 // for mirrored viewmodels. It keeps the BVH aligned while inspecting/turning.
 if(!std::isfinite(Dot(collisionFrame.normal,collisionFrame.normal))||Dot(collisionFrame.normal,collisionFrame.normal)<.99f)
  collisionFrame=geo::Frame();
}
void CollisionCoordinates(std::vector<geo::Triangle> &triangles)
{
 const float *axes[3]={collisionFrame.axisX,collisionFrame.axisY,collisionFrame.normal};
 for(auto &triangle:triangles)
 {
  for(auto &point:triangle.p)
  {
   float relative[3];for(int i=0;i<3;++i)relative[i]=point[i]-collisionFrame.origin[i];
   for(int i=0;i<3;++i)point[i]=Dot(relative,axes[i]);
  }
  float normal[3];for(int i=0;i<3;++i)normal[i]=triangle.normal[i];
  for(int i=0;i<3;++i)triangle.normal[i]=Dot(normal,axes[i]);
 }
}
WeaponConfig frameConfig;
int frameWeapon=0;

geo::StudioTriangleCache &Mesh(studiohdr_t *header)
{
 auto it=meshes.find(header);
 if(it==meshes.end())
 {
  geo::StudioTriangleCache parsed;geo::DecodeStudioTriangles(header,parsed);
  it=meshes.insert(std::make_pair(header,parsed)).first;
 }
 return it->second;
}
unsigned ReverseDigits(unsigned n)
{ unsigned r=0;for(int i=0;i<6;++i){r=r*10+n%10;n/=10;}return r; }

void CharmPose(studiohdr_t *h,const geo::SwingOutput &angles,float length,std::array<Matrix,MAXSTUDIOBONES> &pose)
{
 std::vector<Matrix> dynamic(joints.size()),delta(joints.size());
 for(std::size_t i=0;i<joints.size();++i)
 {
  const Joint &j=joints[i];Matrix around=Identity();
  if(i)
  {
   float fraction=i==1?.2f:.4f;
   around=Rotation(std::max(-15.f,std::min(15.f,angles.pitch*fraction)),i==1?0:std::max(-20.f,std::min(0.f,angles.yaw*.5f)));
   for(int k=0;k<3;++k)
   {
    const float pivot[3]={j.bind.m[0][3],j.bind.m[1][3],j.bind.m[2][3]};
    around.m[k][3]=pivot[k]-(around.m[k][0]*pivot[0]+around.m[k][1]*pivot[1]+around.m[k][2]*pivot[2]);
   }
  }
  dynamic[i]=Product(around,j.bind);
  if(j.parent>=0)dynamic[i]=Product(delta[j.parent],dynamic[i]);
  delta[i]=Product(dynamic[i],j.inverse);
 }
 auto bones=reinterpret_cast<mstudiobone_t *>(reinterpret_cast<byte *>(h)+h->boneindex);
 for(int i=0;i<h->numbones;++i)
 {
  pose[i]=Identity();
  for(const Carrier &c:carriers)if(c.bone==bones[i].name)
  {
   pose[i]=Matrix();
   for(const auto &link:c.links)for(int a=0;a<3;++a)for(int b=0;b<4;++b)pose[i].m[a][b]+=delta[link.first].m[a][b]*link.second;
   break;
  }
  // Uniform length changes the pendant size but never alters carrier weights.
  for(auto &row:pose[i].m)for(float &v:row)v*=length;
 }
}

struct Placement
{
 geo::Mount mount;
 geo::Frame frame,anchorFrame;
 geo::Bounds bounds;
 std::array<Matrix,MAXSTUDIOBONES> local,posed;
 std::vector<geo::Triangle> triangles;
 geo::SwingState swingState;
 geo::SwingOutput angles;
 geo::CollisionResult collision;
 float velocity[3]{};
 float required=0,offset=0,minDepth=1e20f,boxMinDepth=1e20f;
 float contactGap=0,contactLateral=0;
 bool valid=false,fitted=false,limited=false;
};
// These samples are points on the real compiled shell/clip backing mesh, not
// arbitrary origins or bounding boxes. Their coordinates are evaluated through
// the same bones as the pixels drawn below.
struct ContactSample
{
 bool found=false;
 geo::StudioCorner corner[3]{};
 float weight[3]{};
};
std::map<const studiohdr_t *,ContactSample> contactSamples;
const ContactSample &ContactOnMesh(studiohdr_t *header,const std::array<Matrix,MAXSTUDIOBONES> &bind,bool charm)
{
 auto entry=contactSamples.find(header);if(entry!=contactSamples.end())return entry->second;
 ContactSample sample;
 const auto skins=reinterpret_cast<const short *>(reinterpret_cast<const byte *>(header)+header->skinindex);
 const auto textures=reinterpret_cast<const mstudiotexture_t *>(reinterpret_cast<const byte *>(header)+header->textureindex);
 for(const auto &triangle:Mesh(header).triangles)
 {
  if(triangle.material<0||triangle.material>=header->numskinref)continue;
  int material=skins[triangle.material];if(material<0||material>=header->numtextures)continue;
  if(strcmp(textures[material].name,charm?"charm_clip.bmp":"counter_shell.bmp"))continue;
  float p[3][3];bool backing=true;
  for(int i=0;i<3;++i)
  {
   Point(bind[triangle.corner[i].bone].m,triangle.corner[i].p,p[i]);
   if(std::fabs(p[i][2])>.0001f)backing=false;
  }
  if(!backing)continue;
  const double x1=p[1][0]-p[0][0],y1=p[1][1]-p[0][1],x2=p[2][0]-p[0][0],y2=p[2][1]-p[0][1];
  const double determinant=x1*y2-y1*x2;if(std::fabs(determinant)<1e-12)continue;
  const double b=(-p[0][0]*y2+p[0][1]*x2)/determinant,c=(-x1*p[0][1]+y1*p[0][0])/determinant;
  if(b< -1e-6||c< -1e-6||b+c>1+1e-6)continue;
  sample.found=true;sample.weight[0]=float(1-b-c);sample.weight[1]=float(b);sample.weight[2]=float(c);
  for(int i=0;i<3;++i)sample.corner[i]=triangle.corner[i];
  break;
 }
 return contactSamples.insert(std::make_pair(header,sample)).first->second;
}
void PosePlacement(CStudioModelRenderer &r,studiohdr_t *header,const std::array<Matrix,MAXSTUDIOBONES> &base,
                   int body,bool charm,bool left,const geo::MeshCollider &obstacles,Placement &p)
{
 p.local=base;p.frame=p.anchorFrame;
 if(charm)CharmPose(header,p.angles,frameConfig.length,p.local);
 else if(left)
 {
  auto bones=reinterpret_cast<mstudiobone_t *>(reinterpret_cast<byte *>(header)+header->boneindex);
  for(int i=0;i<header->numbones;++i)if(!strncmp(bones[i].name,"digit",5))
   for(int row=0;row<3;++row)p.local[i].m[row][0]=-p.local[i].m[row][0];
 }
 // A mounting pad sized in model units. Collision never increases this gap.
 p.required=p.offset=.01f;
 for(int i=0;i<3;++i)p.frame.origin[i]+=p.frame.normal[i]*p.offset;
 p.minDepth=1e20f;p.boxMinDepth=1e20f;p.valid=p.fitted=false;
 for(int i=0;i<3;++i){p.bounds.min[i]=1e20f;p.bounds.max[i]=-1e20f;}
 Matrix world=FrameMatrix(p.frame);
 for(int i=0;i<header->numbones;++i)p.posed[i]=Product(world,p.local[i]);
 geo::ReadStudioTriangles(Mesh(header),body,0,reinterpret_cast<const float (*)[3][4]>(p.posed.data()),p.triangles);
 for(const auto &triangle:p.triangles)for(const auto &point:triangle.p)
 {
  float d[3];for(int i=0;i<3;++i)d[i]=point[i]-r.m_vRenderOrigin[i];
  p.minDepth=std::min(p.minDepth,Dot(d,r.m_vNormal));
  for(int i=0;i<3;++i)d[i]=point[i]-p.frame.origin[i];
  const float local[3]={Dot(d,p.frame.axisX),Dot(d,p.frame.axisY),Dot(d,p.frame.normal)};
  for(int i=0;i<3;++i){p.bounds.min[i]=std::min(p.bounds.min[i],local[i]);p.bounds.max[i]=std::max(p.bounds.max[i],local[i]);}
 }
 float relative[3];for(int i=0;i<3;++i)relative[i]=p.frame.origin[i]-r.m_vRenderOrigin[i];
 p.boxMinDepth=Dot(relative,r.m_vNormal);
 const float basis[3]={Dot(p.frame.axisX,r.m_vNormal),Dot(p.frame.axisY,r.m_vNormal),Dot(p.frame.normal,r.m_vNormal)};
 for(int i=0;i<3;++i)p.boxMinDepth+=basis[i]*(basis[i]<0?p.bounds.max[i]:p.bounds.min[i]);
 const ContactSample &contact=ContactOnMesh(header,base,charm);
 if(!contact.found)return;
 float actual[3]{},d[3],lateral[3];
 for(int i=0;i<3;++i)
 {
  float vertex[3];Point(p.posed[contact.corner[i].bone].m,contact.corner[i].p,vertex);
  for(int axis=0;axis<3;++axis)actual[axis]+=vertex[axis]*contact.weight[i];
 }
 for(int i=0;i<3;++i)d[i]=actual[i]-p.anchorFrame.origin[i];
 p.contactGap=Dot(d,p.anchorFrame.normal);
 for(int i=0;i<3;++i)lateral[i]=d[i]-p.anchorFrame.normal[i]*p.contactGap;
 p.contactLateral=std::sqrt(Dot(lateral,lateral));
 // Bounds, near clipping and the physical contact sample above use world
 // coordinates; only the collision query uses the shared gun-aligned frame.
 CollisionCoordinates(p.triangles);
 const auto collisionStart=std::chrono::steady_clock::now();
 p.collision=geo::CheckMeshCollision(obstacles,p.triangles);
 if(charm&&counterColliderReady&&!p.collision.blocked)
 {
  auto second=geo::CheckMeshCollision(attachmentObstacles[1],p.triangles);
  second.trianglePairs+=p.collision.trianglePairs;second.containmentSamples+=p.collision.containmentSamples;
  second.closedGroups+=p.collision.closedGroups;p.collision=second;
 }
 const int slot=charm?1:0;
 diag.collisionMs[slot]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-collisionStart).count();
 diag.collisionPairs[slot]+=p.collision.trianglePairs;diag.containmentSamples[slot]+=p.collision.containmentSamples;
 p.valid=std::isfinite(p.minDepth)&&p.minDepth<1e19f;
 p.fitted=p.valid&&!p.collision.blocked&&p.minDepth>=1.10f&&
  std::fabs(p.contactGap-.01f)<.001f&&p.contactLateral<.001f;
}
Placement EvaluatePlacement(CStudioModelRenderer &r,studiohdr_t *header,const std::array<Matrix,MAXSTUDIOBONES> &base,
                           int body,bool charm,bool left,const geo::Mount &mount,const geo::MeshCollider &obstacles)
{
 Placement p;p.mount=mount;p.local=base;p.anchorFrame=MountFrame(mount,r.m_rgCachedLightTransform[mount.bone]);p.frame=p.anchorFrame;
 if(charm)
 {
  geo::SwingInput input;input.time=r.m_clTime;input.resetToken=motion.token;
  for(int i=0;i<3;++i)input.anchor[i]=p.frame.origin[i];
  float acceleration[3]{};const double dt=r.m_clTime-motion.time;
  if(motion.time>=0&&dt>0&&dt<=.1)
   for(int i=0;i<3;++i){p.velocity[i]=float((p.frame.origin[i]-motion.anchor[i])/dt);acceleration[i]=std::max(-400.f,std::min(400.f,float((p.velocity[i]-motion.velocity[i])/dt)));}
  input.anchorAcceleration[0]=Dot(acceleration,p.frame.axisX);input.anchorAcceleration[1]=Dot(acceleration,p.frame.axisY);input.anchorAcceleration[2]=Dot(acceleration,p.frame.normal);
  const float gravity[3]={0,0,-80};input.gravity[0]=Dot(gravity,p.frame.axisX);input.gravity[1]=Dot(gravity,p.frame.axisY);input.gravity[2]=Dot(gravity,p.frame.normal);
  input.teleport=motion.time>=0&&(dt<0||dt>.25);
  geo::SwingConfig cfg;cfg.amplitude=1;cfg.maxAngle=frameConfig.amplitude;cfg.length=frameConfig.length*2.5f;cfg.stiffness=frameConfig.stiffness;cfg.damping=frameConfig.damping;
  // Every candidate starts from the same old state. Only the final accepted
  // position is committed, so a safety search never integrates time twice.
  p.swingState=swing;p.angles=geo::UpdateSwing(p.swingState,cfg,input);
 }
 PosePlacement(r,header,base,body,charm,left,obstacles,p);
 if(!charm||p.fitted)return p;
 const geo::SwingOutput desired=p.angles;
 Placement safe=p;safe.angles=geo::SwingOutput();
 PosePlacement(r,header,base,body,true,left,obstacles,safe);
 if(!safe.fitted)return p;
 // Stay in the connected collision-free angle interval from neutral. The
 // same already-integrated state is used for every pose trial.
 float low=0,high=1;
 for(int step=1;step<=8;++step)
 {
  const float amount=step/8.f;
  Placement trial=p;trial.angles.pitch=desired.pitch*amount;trial.angles.yaw=desired.yaw*amount;
  PosePlacement(r,header,base,body,true,left,obstacles,trial);
  if(!trial.fitted){high=amount;break;}
  low=amount;safe=std::move(trial);
 }
 for(int i=0;i<5&&high-low>.001f;++i)
 {
  const float amount=(low+high)*.5f;
  Placement trial=p;trial.angles.pitch=desired.pitch*amount;trial.angles.yaw=desired.yaw*amount;
  PosePlacement(r,header,base,body,true,left,obstacles,trial);
  if(trial.fitted){low=amount;safe=std::move(trial);}else high=amount;
 }
 safe.limited=true;
 const float applied[2]={safe.angles.pitch*.0174532925199433f,safe.angles.yaw*.0174532925199433f};
 for(int i=0;i<2;++i)
 {
  const float blockedDirection=safe.swingState.angle[i]-applied[i];
  if(safe.swingState.velocity[i]*blockedDirection>0)safe.swingState.velocity[i]=0;
  safe.swingState.angle[i]=safe.swingState.previousAngle[i]=applied[i];
 }
 return safe;
}

void DrawOne(CStudioModelRenderer &r,alight_t &lighting,float *lightdir,bool charm)
{
 const auto drawStart=std::chrono::steady_clock::now();
 const float u=charm?frameConfig.charmU:frameConfig.statU,v=charm?frameConfig.charmV:frameConfig.statV;
 geo::Mount mount;if(!weaponSurface||!geo::FindMount(*weaponSurface,u,v,mount)||mount.bone<0||mount.bone>=r.m_nCachedBones)return;
 model_t *model=IEngineStudio.Mod_ForName(charm?"models/attachments/oc2_sticker_charm.mdl":"models/attachments/oc2_stattrak.mdl",false);
 studiohdr_t *header=model?reinterpret_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(model)):nullptr;
 std::array<Matrix,MAXSTUDIOBONES> base;
 if(!BindBones(header,base)||(charm&&!LoadSkin()))return;
 const bool left=gHUD.cl_righthand&&gHUD.cl_righthand->value<=0;
 const unsigned count=counters.values[Index(frameWeapon)];
 const int body=charm?0:static_cast<int>(left?ReverseDigits(count):count);
 const int slot=charm?1:0;
 const geo::MeshCollider &obstacles=attachmentObstacles[0];
 unsigned testedCandidates=0;
 auto evaluate=[&](const geo::Mount &candidate)
 {
  ++testedCandidates;
  return EvaluatePlacement(r,header,base,body,charm,left,candidate,obstacles);
 };
 const double previousTime=charm?motion.charmTime:motion.statTime;
 const bool previousPlaced=charm?motion.charmPlaced:motion.statPlaced;
 float *previousUV=charm?motion.charmUV:motion.statUV;
 Placement selected=evaluate(mount);
 const bool requestedFit=selected.fitted;
 const bool nearClamped=selected.minDepth<1.10f;
 if(previousPlaced)
 {
  const bool moving=std::fabs(previousUV[0]-mount.actualUV[0])>1e-5f||std::fabs(previousUV[1]-mount.actualUV[1])>1e-5f;
  if(moving||!selected.fitted)
  {
   const float dt=previousTime<0?0:std::max(0.f,std::min(.1f,float(r.m_clTime-previousTime)));
   const float release=selected.fitted?1-std::exp(-8.f*dt):0;
   geo::Mount preferred;
   if(geo::FindMount(*weaponSurface,previousUV[0]+(u-previousUV[0])*release,previousUV[1]+(v-previousUV[1])*release,preferred))
   {
    Placement tested=evaluate(preferred);
    if(tested.fitted)selected=std::move(tested);
   }
  }
 }
 if(!selected.valid)return;
 if(!selected.fitted)
 {
  geo::MountViewConstraint constraint;constraint.poseMatrices=r.m_rgCachedLightTransform;
  for(int i=0;i<3;++i){constraint.viewOrigin[i]=r.m_vRenderOrigin[i];constraint.viewForward[i]=r.m_vNormal[i];}
  constraint.minimumOriginDepth=1.10f;
  std::vector<geo::Mount> candidates;
  geo::FindMountCandidates(*weaponSurface,u,v,candidates,64,&constraint);
  for(const auto &candidate:candidates)
  {
   Placement tested=evaluate(candidate);
   if(tested.fitted){selected=std::move(tested);break;}
  }
  if(!selected.fitted)
  {
   // Dense features near a blocked point must not exclude the rest of the
   // whole-gun domain. This bounded broad search still lands on real surfaces.
   candidates.clear();
   for(int y=0;y<=8;++y)for(int x=0;x<=12;++x)
   {
    geo::Mount candidate;
    if(geo::FindMountInFront(*weaponSurface,x/12.f,y/8.f,constraint,candidate))candidates.push_back(candidate);
   }
   std::sort(candidates.begin(),candidates.end(),[&](const geo::Mount &a,const geo::Mount &b)
   {
    const float ax=a.actualUV[0]-u,ay=a.actualUV[1]-v,bx=b.actualUV[0]-u,by=b.actualUV[1]-v;
    return ax*ax+ay*ay<bx*bx+by*by;
   });
   for(const auto &candidate:candidates)
   {
    Placement tested=evaluate(candidate);
    if(tested.fitted){selected=std::move(tested);break;}
   }
  }
 }
 // Never float a root, scale the model or silently hide an impossible pose.
 // Such a pose remains anchored and explicitly fails the contact diagnostics.
 mount=selected.mount;
 if(charm)
 {
  swing=selected.swingState;diag.pitch=selected.angles.pitch;diag.yaw=selected.angles.yaw;
  const double dt=r.m_clTime-motion.time;
  if(dt!=0||motion.time<0){motion.time=r.m_clTime;for(int i=0;i<3;++i){motion.anchor[i]=selected.anchorFrame.origin[i];motion.velocity[i]=selected.velocity[i];}}
  diag.steps=swing.steps;diag.resets=swing.resets;
  motion.charmPlaced=selected.fitted;diag.charmNearClamped=nearClamped;diag.charmNearIterations=int(testedCandidates);
  diag.swingLimited=selected.limited;
 }
 else{motion.statPlaced=selected.fitted;diag.statNearClamped=nearClamped;diag.statNearIterations=int(testedCandidates);}
 diag.contactGap[slot]=selected.contactGap;diag.contactLateral[slot]=selected.contactLateral;
 diag.blocked[slot]=!selected.fitted;diag.testedCandidates[slot]=testedCandidates;
 diag.surfaceAdjusted[slot]=!requestedFit||std::fabs(mount.actualUV[0]-u)>.001f||std::fabs(mount.actualUV[1]-v)>.001f;
 for(int i=0;i<2;++i)previousUV[i]=mount.actualUV[i];
 const float required=selected.required;
 float &offset=charm?motion.charmOffset:motion.statOffset;
 double &appliedTime=charm?motion.charmTime:motion.statTime;
 appliedTime=r.m_clTime;offset=selected.offset;
 diag.boxMinDepth=std::min(diag.boxMinDepth,selected.boxMinDepth);diag.minDepth=std::min(diag.minDepth,selected.minDepth);
 const auto &posed=selected.posed;const auto &addonTriangles=selected.triangles;

 studiohdr_t *savedHeader=r.m_pStudioHeader;model_t *savedModel=r.m_pRenderModel;
 const cl_entity_t savedEntity=*r.m_pCurrentEntity;
 mstudiobodyparts_t *savedPart=r.m_pBodyPart;mstudiomodel_t *savedSub=r.m_pSubModel;
 const int savedFlags=IEngineStudio.GetForceFaceFlags();
 r.m_pRenderModel=model;r.m_pStudioHeader=header;
 // Keep entity.model as the parent weapon: the engine determines handedness
 // from its Source hand rig. The independent addon has no such hand bones.
 r.m_pCurrentEntity->curstate.body=body;r.m_pCurrentEntity->curstate.skin=0;
 r.m_pCurrentEntity->curstate.sequence=0;r.m_pCurrentEntity->curstate.frame=0;
 r.m_pCurrentEntity->curstate.framerate=0;r.m_pCurrentEntity->curstate.animtime=r.m_clTime;
 IEngineStudio.StudioSetHeader(header);IEngineStudio.SetRenderModel(model);
 for(int i=0;i<header->numbones;++i)
 {
  MatrixCopy(posed[i].m,(*r.m_pbonetransform)[i]);MatrixCopy(posed[i].m,(*r.m_plighttransform)[i]);
 }
 int savedTexture=0;mstudiotexture_t *swapped=nullptr;
 if(charm)
 {
  const int texture=StickerTexture(frameConfig.sticker);
  auto textures=reinterpret_cast<mstudiotexture_t *>(reinterpret_cast<byte *>(header)+header->textureindex);
  for(int i=0;texture&&i<header->numtextures;++i)if(!strcmp(textures[i].name,"charm_sticker.bmp"))
   {swapped=&textures[i];savedTexture=swapped->index;swapped->index=texture;break;}
 }
 IEngineStudio.StudioSetupLighting(&lighting);IEngineStudio.StudioSetRemapColors(r.m_nTopColor,r.m_nBottomColor);
 r.StudioRenderModel(lightdir);
 if(swapped)swapped->index=savedTexture;
 // Refit the gun/arms only once per frame. The charm separately queries this
 // much smaller counter collider, so entity groups cannot share a solid.
 if(!charm&&frameConfig.charm)
 {
  const auto start=std::chrono::steady_clock::now();
  counterColliderReady=geo::RefitMeshCollider(addonTriangles,attachmentObstacles[1]);
  diag.refitMs[1]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 }
 *r.m_pCurrentEntity=savedEntity;r.m_pStudioHeader=savedHeader;r.m_pRenderModel=savedModel;
 r.m_pBodyPart=savedPart;r.m_pSubModel=savedSub;
 for(int i=0;i<r.m_nCachedBones;++i)
  {MatrixCopy(r.m_rgCachedBoneTransform[i],(*r.m_pbonetransform)[i]);MatrixCopy(r.m_rgCachedLightTransform[i],(*r.m_plighttransform)[i]);}
 IEngineStudio.StudioSetHeader(savedHeader);IEngineStudio.SetRenderModel(savedModel);
 IEngineStudio.StudioSetupLighting(&lighting);IEngineStudio.StudioSetRemapColors(r.m_nTopColor,r.m_nBottomColor);IEngineStudio.SetForceFaceFlags(savedFlags);
 ++diag.drawn;
 diag.drawMs[slot]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-drawStart).count();
 diag.triangles=int(occluders.size());
 if(charm){diag.charmBone=mount.bone;diag.charmOffset=offset;diag.charmRequired=required;for(int i=0;i<2;++i)diag.charmUV[i]=mount.actualUV[i];}
 else{diag.statBone=mount.bone;diag.statOffset=offset;diag.statRequired=required;for(int i=0;i<2;++i)diag.statUV[i]=mount.actualUV[i];}
}
}

void OC2_AccessoriesVidInit()
{
 mounts.clear();meshes.clear();stickerTextures.clear();occluders.clear();weaponSurface=nullptr;
 contactSamples.clear();colliderBones.clear();attachmentObstacles=std::array<geo::MeshCollider,2>();
 obstacleSources.clear();previousObstacleSources.clear();counterColliderReady=false;
 skinAttempted=skinValid=false;joints.clear();carriers.clear();swing=geo::SwingState();motion=Motion();
 frameWeapon=0;diag=Diagnostics();nextRead=-1;lastTime=-1;
}
void OC2_BeginAccessories(CStudioModelRenderer &r,int weapon)
{
 const char *map=gEngfuncs.pfnGetLevelName();
 if(level!=(map?map:"")){OC2_AccessoriesVidInit();level=map?map:"";}
 ReadSettings(r.m_clTime);diag=Diagnostics();diag.weapon=weapon;frameWeapon=0;weaponSurface=nullptr;occluders.clear();obstacleSources.clear();
 const bool left=gHUD.cl_righthand&&gHUD.cl_righthand->value<=0;
 if(motion.weapon!=weapon||motion.header!=r.m_pStudioHeader||motion.left!=left||motion.revision!=settings.revision)
 {
  const unsigned token=motion.token+1;motion=Motion();motion.weapon=weapon;motion.header=r.m_pStudioHeader;
  motion.left=left;motion.revision=settings.revision;motion.token=token;swing=geo::SwingState();
 }
 const WeaponConfig *cfg=Find(settings,weapon);
 if(!cfg||(!cfg->stattrak&&!cfg->charm)||!IEngineStudio.IsHardware())return;
 auto it=mounts.find(r.m_pStudioHeader);
 if(it==mounts.end())
 {
  geo::MountCache cache;geo::BuildMountCache(r.m_pStudioHeader,cache);
  it=mounts.insert(std::make_pair(r.m_pStudioHeader,cache)).first;
 }
 if(!it->second.valid)return;
 frameWeapon=weapon;frameConfig=*cfg;weaponSurface=&it->second;
 SetCollisionFrame(*weaponSurface,r.m_rgCachedLightTransform,frameConfig);
 diag.statEnabled=cfg->stattrak;diag.charmEnabled=cfg->charm;diag.sticker=cfg->sticker;diag.count=counters.values[Index(weapon)];
 geo::ReadStudioTriangles(weaponSurface->studio,r.m_pCurrentEntity->curstate.body,r.m_pCurrentEntity->curstate.skin,r.m_rgCachedLightTransform,occluders);
 obstacleSources.push_back({r.m_pStudioHeader,r.m_pCurrentEntity->curstate.body,r.m_pCurrentEntity->curstate.skin});
}
void OC2_ObserveAccessoryOccluder(CStudioModelRenderer &r)
{
 if(!frameWeapon)return;
 geo::ReadStudioTriangles(Mesh(r.m_pStudioHeader),r.m_pCurrentEntity->curstate.body,r.m_pCurrentEntity->curstate.skin,*r.m_plighttransform,occluders,true);
 obstacleSources.push_back({r.m_pStudioHeader,r.m_pCurrentEntity->curstate.body,r.m_pCurrentEntity->curstate.skin});
}
void OC2_DrawAccessories(CStudioModelRenderer &r,alight_s &lighting,float *lightdir)
{
 if(!frameWeapon)return;
 if(obstacleSources!=previousObstacleSources)
 {
  attachmentObstacles=std::array<geo::MeshCollider,2>();
  previousObstacleSources=obstacleSources;
 }
 const auto start=std::chrono::steady_clock::now();
 CollisionCoordinates(occluders);
 if(!geo::RefitMeshCollider(occluders,attachmentObstacles[0]))return;
 diag.refitMs[0]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 counterColliderReady=false;
 if(frameConfig.stattrak)DrawOne(r,lighting,lightdir,false);
 if(frameConfig.charm)DrawOne(r,lighting,lightdir,true);
}
