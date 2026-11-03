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
#include "skin_inventory_client.h"
#include "accessory_geometry.h"
#include "oc2_cold_perf.h"
#include "accessory_swing_path.h"
#include "accessory_mount_contact.h"
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
cvar_t *accessoriesEnabled=nullptr;
cvar_t *charmVerify=nullptr;
struct CharmCollisionStats
{
 unsigned checks=0,blocked=0,solved=0,limited=0,failures=0,queries=0,segments=0,rawBlocked=0,contactOnly=0,retained=0,refits=0;
} charmStats;
unsigned runCharmFailures=0,runCharmBlocked=0;
void CharmFailure()
{
 ++charmStats.failures;
 if(++runCharmFailures==1)gEngfuncs.Con_Printf("oc2_charm_unsolved: run_failures=1\n");
}
void ResetCharmCollisionStats(){charmStats=CharmCollisionStats();}
using PerfClock=std::chrono::steady_clock;
struct NativePerf
{
 bool active=false;double clientTime=-1,cpu=0;unsigned calls=0,count=0;
 PerfClock::time_point previous;
 std::array<double,512> intervals{};
} nativePerf;
struct CpuScope
{
 bool active=nativePerf.active;PerfClock::time_point start;
 CpuScope(){if(active)start=PerfClock::now();}
 ~CpuScope(){if(active){nativePerf.cpu+=std::chrono::duration<double,std::milli>(PerfClock::now()-start).count();++nativePerf.calls;}}
};
void PerfReset(){nativePerf=NativePerf();nativePerf.active=true;}
void PerfDump()
{
 nativePerf.active=false;
 const unsigned count=std::min<unsigned>(nativePerf.count,unsigned(nativePerf.intervals.size()));
 std::vector<double> times(nativePerf.intervals.begin(),nativePerf.intervals.begin()+count);std::sort(times.begin(),times.end());
 double total=0;for(double value:times)total+=value;
 const double mean=count?total/count:0;
 gEngfuncs.Con_Printf("oc2accessory_perf: enabled=%d frames=%u mean_frame_ms=%.6f median_frame_ms=%.6f p95_frame_ms=%.6f fps=%.6f accessory_cpu_ms=%.6f\n",
  int(!accessoriesEnabled||accessoriesEnabled->value!=0),count,mean,count?times[count/2]:0,count?times[std::min(count-1,unsigned(count*.95))]:0,
  mean>0?1000/mean:0,nativePerf.count?nativePerf.cpu/nativePerf.count:0);
}

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
 static const cvar_t *configPoll=nullptr;
 if(!configPoll)configPoll=gEngfuncs.pfnGetCvarPointer("cl_oc2_config_poll");
 if(!force&&configPoll&&configPoll->value==0)return;
 if(!force&&time>=lastTime&&time<nextRead)return;
 lastTime=time;nextRead=time+.25;
 static std::string parsedSettings,parsedCounts,parsedCatalog;
 std::string text,error;Config candidate;
 if(LoadText(kConfigPath,text,16384))
 {
  if(!force&&text==parsedSettings){}
  else if(ParseConfig(text.c_str(),candidate,error)){settings=candidate;readError=false;parsedSettings=text;}
  else if(!readError){gEngfuncs.Con_Printf("accessories: %s\n",error.c_str());readError=true;}
 }
 if(LoadText(kCountsPath,text,4096)&&(force||text!=parsedCounts))
 {
  Counts current;if(ParseCounts(text.c_str(),current,error)){counters=current;parsedCounts=text;}
 }
 if(LoadText(kCatalogPath,text,131072)&&(force||text!=parsedCatalog))
 {
  Catalog current;if(ParseCatalog(text.c_str(),current,error)){catalog=current;parsedCatalog=text;}
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

void DumpMotion();
struct Diagnostics
{
 int weapon=0,drawn=0,statBone=-1,charmBone=-1,sticker=0;
 float statOffset=0,charmOffset=0,statRequired=0,charmRequired=0,pitch=0,yaw=0,minDepth=1e20f,boxMinDepth=1e20f;
 float statUV[2]{},charmUV[2]{};
 float pendantDrop=0;
 float mountLocal[2][3]{};
 unsigned steps=0,resets=0;int triangles=0;
 int statNearIterations=0,charmNearIterations=0;
 bool statNearClamped=false,charmNearClamped=false;
 float contactGap[2]{},contactLateral[2]{};
 float charmNeck=0;
 int charmHostRoot=-1,charmHostBones=0;bool charmHostOnly=false;
 int charmMagBone=-1;float charmSocketError=0;bool charmMagDetached=false;
 bool blocked[2]{},surfaceAdjusted[2]{},swingLimited=false;
 unsigned testedCandidates[2]{};
 double drawMs[2]{},refitMs[2]{},collisionMs[2]{};
 unsigned collisionPairs[2]{},containmentSamples[2]{};
 unsigned collisionReuse[2]{},collisionFull[2]{};
 unsigned collisionVerified[2]{},collisionMismatch[2]{};
 unsigned sceneReused=0,sceneVerified=0,sceneMismatch=0,sceneRefreshes=0;
 unsigned boneQueries=0,boneUnsupported=0;
 float sceneError=0;
 double closureMs=0,sceneBoundMs=0,sceneCoreMs=0;
 bool statEnabled=false,charmEnabled=false;
 unsigned count=0,revision=0;
 WeaponConfig config;
} diag;
void Dump()
{
 ReadSettings(gEngfuncs.GetClientTime(),true);
 const int index=Index(diag.weapon);const unsigned count=diag.count;
 gEngfuncs.Con_Printf("oc2accessory: weapon=%d stat=%d charm=%d drawn=%d count=%u stat_bone=%d charm_bone=%d stat_offset=%.6f charm_offset=%.6f pitch=%.6f yaw=%.6f sticker=%d revision=%u read_error=%d\n",
  diag.weapon,int(diag.statEnabled),int(diag.charmEnabled),diag.drawn,count,diag.statBone,diag.charmBone,
  diag.statOffset,diag.charmOffset,diag.pitch,diag.yaw,diag.sticker,diag.revision,int(readError));
 gEngfuncs.Con_Printf("oc2accessory_config: weapon=%d revision=%u loaded_revision=%u stattrak=%d stat_u=%.6f stat_v=%.6f charm=%d charm_u=%.6f charm_v=%.6f sticker=%d amplitude=%.6f length=%.6f stiffness=%.6f damping=%.6f\n",
  diag.config.weapon,diag.revision,settings.revision,int(diag.config.stattrak),diag.config.statU,diag.config.statV,
  int(diag.config.charm),diag.config.charmU,diag.config.charmV,diag.config.sticker,
  diag.config.amplitude,diag.config.length,diag.config.stiffness,diag.config.damping);
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
 gEngfuncs.Con_Printf("oc2accessory_cache: reused=%u/%u full=%u/%u verified=%u/%u mismatch=%u/%u\n",diag.collisionReuse[0],diag.collisionReuse[1],diag.collisionFull[0],diag.collisionFull[1],
  diag.collisionVerified[0],diag.collisionVerified[1],diag.collisionMismatch[0],diag.collisionMismatch[1]);
 gEngfuncs.Con_Printf("oc2accessory_scene: reused=%u verified=%u mismatch=%u refreshes=%u error=%.7f\n",diag.sceneReused,diag.sceneVerified,diag.sceneMismatch,diag.sceneRefreshes,diag.sceneError);
 gEngfuncs.Con_Printf("oc2accessory_bones: queries=%u unsupported=%u\n",diag.boneQueries,diag.boneUnsupported);
 gEngfuncs.Con_Printf("oc2accessory_scene_cpu: closure_ms=%.5f bound_ms=%.5f core_ms=%.5f\n",diag.closureMs,diag.sceneBoundMs,diag.sceneCoreMs);
 gEngfuncs.Con_Printf("oc2accessory_mount: fixed=1 collision=1 relocation=0 stat_local=%.6f/%.6f/%.6f charm_local=%.6f/%.6f/%.6f\n",
  diag.mountLocal[0][0],diag.mountLocal[0][1],diag.mountLocal[0][2],
  diag.mountLocal[1][0],diag.mountLocal[1][1],diag.mountLocal[1][2]);
 gEngfuncs.Con_Printf("oc2accessory_charm_collision: enabled=1 oracle=%d checks=%u blocked=%u solved=%u limited=%u triangles=%d stat_collision=0 failures=%u queries=%u segments=%u raw_blocked=%u contact_only=%u contact_half=%.6f contact_neck_only=1 neck=%.3f retained=%u refits=%u host_only=%d host_root=%d host_bones=%d mag_bone=%d mag_detached=%d socket_error=%.6f run_failures=%u run_blocked=%u\n",
  int(charmVerify&&charmVerify->value!=0),charmStats.checks,charmStats.blocked,charmStats.solved,charmStats.limited,
  diag.triangles,charmStats.failures,charmStats.queries,charmStats.segments,charmStats.rawBlocked,charmStats.contactOnly,oc2_accessory::kMountContactHalf,diag.charmNeck,charmStats.retained,charmStats.refits,int(diag.charmHostOnly),diag.charmHostRoot,diag.charmHostBones,diag.charmMagBone,int(diag.charmMagDetached),diag.charmSocketError,runCharmFailures,runCharmBlocked);
 DumpMotion();
}
void Reload() { nextRead=-1;stickerTextures.clear();ReadSettings(gEngfuncs.GetClientTime(),true); }
}

int OC2_LegacyCharmTexture(int sticker){ReadSettings(gEngfuncs.GetClientTime());return StickerTexture(sticker);}
void OC2_AccessoryKill(const char *weapon,int killer,int victim,int killerTeam,int victimTeam)
{
 const cl_entity_t *local=gEngfuncs.GetLocalPlayer();
 if(!local||killer!=local->index||killer==victim||victim<1||victim>MAX_PLAYERS||
  (killerTeam!=1&&killerTeam!=2)||(victimTeam!=1&&victimTeam!=2)||killerTeam==victimTeam)return;
 if(gEngfuncs.pDemoAPI&&gEngfuncs.pDemoAPI->IsPlayingback())return;
 const int id=WeaponId(weapon),index=Index(id);if(index<0)return;
 if(!OC2_InventoryOwnAccessories(id))return;
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
 accessoriesEnabled=gEngfuncs.pfnRegisterVariable("oc2_accessories_enabled","1",0);
 charmVerify=gEngfuncs.pfnRegisterVariable("oc2_charm_verify","0",0);
 gEngfuncs.pfnAddCommand("oc2_charm_collision_reset",ResetCharmCollisionStats);
 gEngfuncs.pfnAddCommand("oc2_accessory_perf_reset",PerfReset);
 gEngfuncs.pfnAddCommand("oc2_accessory_perf_dump",PerfDump);
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
int skinVariant=-1;
bool ReadMatrix(std::istringstream &s,Matrix &m)
{ for(auto &row:m.m)for(float &v:row)if(!(s>>v)||!std::isfinite(v))return false;return true; }
bool LoadSkin(bool longNeck)
{
 const int variant=longNeck?1:0;
 if(skinVariant!=variant){skinVariant=variant;skinAttempted=skinValid=false;}
 if(skinAttempted)return skinValid;skinAttempted=true;
 std::string text,magic;int version=0,count=0;
 if(!LoadText(longNeck?"models/attachments/charm_m249.skin":"models/attachments/charm.skin",text,32768))return false;
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
// Immutable topology prepared while the map is loading. Every use is refit
// against the actual current pose before any collision query.
std::map<const studiohdr_t *,geo::MeshCollider> warmCharmColliders;
geo::SwingState swing;
struct Motion
{
 unsigned searchCursor=0;
 double searchRetry=0;
 double time=-1;float anchor[3]{},velocity[3]{};
 int weapon=0;unsigned token=0,revision=0;
 const studiohdr_t *header=nullptr;bool left=false;
 WeaponConfig config;
 geo::Mount requested[2];
 bool requestedResolved[2]{};
 bool charmSafe=false,biasReady=false,charmRepose=false;
 geo::SwingOutput safeAngles,bias,foot,restFoot;
 bool swingBasisReady=false;
 geo::SwingOutput basisBias,basisFoot;
 float basisLength=0,swingBasis[3][3]{};
 const studiohdr_t *swingHeader=nullptr;
 int swingBone=-1;float swingCenter[3]{};
 std::array<Matrix,MAXSTUDIOBONES> safeLocal;
} motion;
bool SameCustomization(const WeaponConfig &a,const WeaponConfig &b)
{
 return a.weapon==b.weapon&&a.stattrak==b.stattrak&&a.statU==b.statU&&a.statV==b.statV&&
  a.charm==b.charm&&a.charmU==b.charmU&&a.charmV==b.charmV&&a.sticker==b.sticker&&
  a.amplitude==b.amplitude&&a.length==b.length&&a.stiffness==b.stiffness&&a.damping==b.damping;
}
void SynchronizeMotion(const WeaponConfig &cfg,int weapon,const studiohdr_t *header,bool left,unsigned revision)
{
 // A file revision or tuning change does not move the physical mounting joint.
 // Keep the certified rest orientation instead of searching a new one mid-pose.
 const bool reset=motion.weapon!=weapon||motion.header!=header||motion.left!=left||
  motion.config.charm!=cfg.charm||motion.config.charmU!=cfg.charmU||motion.config.charmV!=cfg.charmV;
 if(reset)
 {
  const unsigned token=motion.token+1;motion=Motion();motion.weapon=weapon;motion.header=header;
  motion.left=left;motion.token=token;swing=geo::SwingState();
 }
 else if(!SameCustomization(motion.config,cfg))
 {
  if(motion.config.length!=cfg.length)motion.charmRepose=true;
  if(motion.config.stattrak!=cfg.stattrak||motion.config.statU!=cfg.statU||motion.config.statV!=cfg.statV)
   motion.requestedResolved[0]=false;
 }
 motion.config=cfg;motion.revision=revision;
}
void DumpMotion()
{
 gEngfuncs.Con_Printf("oc2accessory_pose_state: token=%u bias_ready=%d safe=%d repose=%d bias=%.6f/%.6f foot=%.6f/%.6f safe_angles=%.6f/%.6f length=%.6f pendant_drop=%.6f rest_foot=%.6f/%.6f\n",
  motion.token,int(motion.biasReady),int(motion.charmSafe),int(motion.charmRepose),motion.bias.pitch,motion.bias.yaw,
  motion.foot.pitch,motion.foot.yaw,motion.safeAngles.pitch,motion.safeAngles.yaw,motion.config.length,diag.pendantDrop,
  motion.restFoot.pitch,motion.restFoot.yaw);
}
std::map<const studiohdr_t *,std::array<Matrix,MAXSTUDIOBONES>> addonBind;
geo::MountCache *weaponSurface=nullptr;
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
// Refit the real posed gun once per render frame, then reuse its BVH for
// angle trials. Hands and the fixed StatTrak are not collision obstacles.
struct CharmGunScene
{
 const studiohdr_t *header=nullptr;int body=-1,skin=-1,hostRoot=-1;
 std::vector<int> allowedBones;
 geo::PoseGeometry geometry;
 std::vector<geo::PoseSource> sources;
 std::vector<geo::BoneMetric> metrics,retainedMetrics;
 std::vector<geo::PoseClosure> closures;
 geo::Frame frame;
 geo::MeshCollider full,oracle,oracleLocal;
 std::array<geo::MeshCollider,4> queries;
 std::vector<geo::Triangle> localScratch,outsideScratch,gunScratch;
 int footBone=-1,pendantBone=-1;
 bool fullReady=false,oracleReady=false,canRetain=false;
 float retainedError=0;
} charmGun;
void LocalTriangles(std::vector<geo::Triangle>& triangles,const geo::Frame& frame)
{
 const float *axes[3]={frame.axisX,frame.axisY,frame.normal};
 for(auto& t:triangles)for(auto& p:t.p)
 {
  float v[3];for(int i=0;i<3;++i)v[i]=p[i]-frame.origin[i];
  for(int i=0;i<3;++i)p[i]=Dot(v,axes[i]);
 }
}
bool FullCharmGun()
{
 if(charmGun.fullReady)return true;
 OC2ColdTimer cold("charm_collider",charmGun.header?charmGun.header->name:nullptr);
 auto& gun=charmGun.gunScratch;
 const auto& source=charmGun.sources.front();
 if(!geo::ReadStudioTriangles(*source.mesh,source.body,source.skin,
  reinterpret_cast<const float (*)[3][4]>(source.matrices.data()),gun,false,true,
  charmGun.allowedBones.empty()?nullptr:&charmGun.allowedBones))return false;
 LocalTriangles(gun,charmGun.frame);
 charmGun.fullReady=geo::RefitMeshCollider(gun,charmGun.full);
 if(charmGun.fullReady)
 {
  charmGun.retainedMetrics=charmGun.metrics;
  charmGun.canRetain=geo::BuildPoseClosures(charmGun.full,charmGun.sources,charmGun.closures);
  charmGun.retainedError=0;++charmStats.refits;
 }
 return charmGun.fullReady;
}
bool CurrentCharmOracle()
{
 if(charmGun.oracleReady)return true;
 std::vector<geo::Triangle> gun;const auto& source=charmGun.sources.front();
 if(!geo::ReadStudioTriangles(*source.mesh,source.body,source.skin,
  reinterpret_cast<const float (*)[3][4]>(source.matrices.data()),gun,false,true,
  charmGun.allowedBones.empty()?nullptr:&charmGun.allowedBones))return false;
 if(!geo::RefitMeshCollider(gun,charmGun.oracle))return false;
 LocalTriangles(gun,charmGun.frame);
 charmGun.oracleReady=geo::RefitMeshCollider(gun,charmGun.oracleLocal);
 return charmGun.oracleReady;
}
bool SameCharmBody(const geo::StudioTriangleCache& mesh,int previous,int current)
{
 // Animation events can change the raw body even when every selected submodel
 // stays the same. Match ReadStudioTriangles' active-body rule so those aliases
 // keep the existing BVH; current bone matrices still refit it when required.
 previous=std::max(0,previous);current=std::max(0,current);
 for(const auto& part:mesh.parts)
  if(part.base>0&&part.models>0&&(previous/part.base)%part.models!=(current/part.base)%part.models)return false;
 return true;
}
bool BeginCharmGun(CStudioModelRenderer& r,const geo::Frame& frame)
{
 const int body=r.m_pCurrentEntity->curstate.body,skin=r.m_pCurrentEntity->curstate.skin;
 int hostRoot=-1;const std::vector<int>* allowed=nullptr;std::vector<int> assembly;
 diag.charmMagBone=-1;diag.charmSocketError=0;diag.charmMagDetached=false;
 if(frameWeapon==10)
 {
  for(const auto& region:weaponSurface->regions)
   if(std::find(region.weaponBones.begin(),region.weaponBones.end(),motion.requested[1].bone)!=region.weaponBones.end())
   {hostRoot=region.rigRoot;allowed=&region.weaponBones;break;}
  if(!allowed||allowed->empty()||hostRoot<0)return false;
  assembly=*allowed;const int magazine=hostRoot==2?5:11;
  float displacement=0;
  if(geo::BoneSocketDisplacement(*weaponSurface,hostRoot,magazine,r.m_rgCachedLightTransform,displacement))
  {
   diag.charmMagBone=magazine;diag.charmSocketError=displacement;
   diag.charmMagDetached=displacement>.03f;
   if(diag.charmMagDetached)
   {
    // A removed magazine is a separately carried component. If it carries
    // the charm, it owns the collider; otherwise it is not in the receiver.
    if(motion.requested[1].bone==magazine)assembly={magazine};
    else assembly.erase(std::remove(assembly.begin(),assembly.end(),magazine),assembly.end());
   }
  }
  allowed=&assembly;
 }
 diag.charmHostOnly=allowed!=nullptr;diag.charmHostRoot=hostRoot;diag.charmHostBones=allowed?int(allowed->size()):0;
 if(charmGun.header!=r.m_pStudioHeader||!SameCharmBody(weaponSurface->studio,charmGun.body,body)||charmGun.skin!=skin||charmGun.hostRoot!=hostRoot||charmGun.allowedBones!=assembly)
 {
  OC2ColdTimer cold("charm_geometry",r.m_pRenderModel?r.m_pRenderModel->name:nullptr);
  charmGun=CharmGunScene();charmGun.header=r.m_pStudioHeader;charmGun.body=body;charmGun.skin=skin;
  const auto warm=warmCharmColliders.find(r.m_pStudioHeader);
  if(!allowed&&skin==0&&SameCharmBody(weaponSurface->studio,0,body)&&warm!=warmCharmColliders.end())
   charmGun.full=warm->second;
  charmGun.hostRoot=hostRoot;if(allowed)charmGun.allowedBones=*allowed;
  if(!geo::BuildPoseGeometry(weaponSurface->studio,body,skin,charmGun.geometry,false,
   charmGun.allowedBones.empty()?nullptr:&charmGun.allowedBones))return false;
  charmGun.sources.resize(1);
  auto& source=charmGun.sources.front();source.mesh=&weaponSurface->studio;
  source.geometry=&charmGun.geometry;source.body=body;source.skin=skin;
 }
 auto& source=charmGun.sources.front();source.matrices.resize(r.m_nCachedBones);
 for(int i=0;i<r.m_nCachedBones;++i)MatrixCopy(r.m_rgCachedLightTransform[i],source.matrices[i].m);
 charmGun.frame=frame;charmGun.fullReady=charmGun.oracleReady=false;
 if(!geo::BuildPoseMetrics(charmGun.sources,frame,charmGun.metrics))return false;
 charmGun.canRetain=charmGun.canRetain&&
  geo::PoseClosuresUnchanged(charmGun.full,charmGun.closures,charmGun.sources,frame)&&
  geo::PoseDisplacementBound(charmGun.retainedMetrics,charmGun.metrics,charmGun.full.triangles,charmGun.retainedError,true);
 return charmGun.canRetain||FullCharmGun();
}
constexpr float kCharmCollisionGap=.0001f;
bool CharmFree(const std::vector<geo::Triangle>& world,float gap=kCharmCollisionGap,bool local=false)
{
 ++charmStats.queries;
  if(!local)
  {
   charmGun.localScratch.assign(world.begin(),world.end());
   LocalTriangles(charmGun.localScratch,charmGun.frame);
  }
  const auto& triangles=local?world:charmGun.localScratch;
 // The fixed gun connection has an explicit, tiny contact port. It is not a
 // material/bone exemption: every triangle crossing its boundary is cut, and
 // every outside fragment (including the pin/arm) is still collision tested.
 // Enlarged CCD gaps also shrink the ignorable port by the full motion bound.
 int slot=3;
 if(!triangles.empty())
 {
  if(std::all_of(triangles.begin(),triangles.end(),[](const geo::Triangle& t){return t.bone==charmGun.footBone;}))slot=0;
  else if(std::all_of(triangles.begin(),triangles.end(),[](const geo::Triangle& t){return t.bone==charmGun.pendantBone;}))slot=2;
  else if(local)slot=1;
 }
 auto& query=charmGun.queries[slot];
 const auto check=[&](float error)
 {
   auto& outside=charmGun.outsideScratch;
  geo::TrianglesOutsideMountContact(triangles,outside,std::max(0.f,gap-kCharmCollisionGap)+error,charmGun.footBone);
  if(outside.empty())return true;
  if(!geo::RefitMeshCollider(outside,query,false))return false;
  return !geo::CheckMeshCollision(charmGun.full,query,gap+error).blocked;
 };
 // A retained real-gun BVH is usable only with a bound for every bone and
 // unchanged closure. Its extra separation covers the entire current gun.
 if(!charmGun.fullReady&&charmGun.canRetain&&check(charmGun.retainedError))
 {++charmStats.retained;return true;}
 return FullCharmGun()&&check(0);
}
std::vector<unsigned> CharmGroups(const std::vector<geo::Triangle>& triangles,studiohdr_t *header)
{
 auto bones=reinterpret_cast<mstudiobone_t *>(reinterpret_cast<byte *>(header)+header->boneindex);
 int foot=-1,pendant=-1;
 for(int i=0;i<header->numbones;++i)
 {
  if(!strcmp(bones[i].name,"skin_08"))foot=i;
  if(!strcmp(bones[i].name,"skin_10"))pendant=i;
 }
 std::vector<unsigned> groups;groups.reserve(triangles.size());
 for(const auto& t:triangles)groups.push_back(t.bone==foot?0u:t.bone==pendant?2u:1u);
 return groups;
}
unsigned ReverseDigits(unsigned n)
{ unsigned r=0;for(int i=0;i<6;++i){r=r*10+n%10;n/=10;}return r; }

void CharmPose(studiohdr_t *h,const geo::SwingOutput &angles,float length,std::array<Matrix,MAXSTUDIOBONES> &pose,const geo::SwingOutput& foot)
{
  // LoadSkin validates at most sixteen joints; candidate poses need no heap.
  std::array<Matrix,16> dynamic,delta;
 for(std::size_t i=0;i<joints.size();++i)
 {
  const Joint &j=joints[i];Matrix around=Identity();
  if(i)
  {
   if(i==1)around=Rotation(foot.pitch,foot.yaw);
   else around=Rotation(std::max(-75.f,std::min(75.f,angles.pitch*.5f)),
                   std::max(-90.f,std::min(90.f,angles.yaw*.5f)));
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

bool BuildCharmSwingBasis(studiohdr_t *header,const geo::SwingOutput &rest,const geo::SwingOutput &foot,
 float length,int bone,const float center[3],float basis[3][3])
{
 if(bone<0||bone>=header->numbones)return false;
 std::array<Matrix,MAXSTUDIOBONES> neutral,perturbed;
 for(int axis=0;axis<2;++axis)
 {
  auto angle=rest;if(axis)angle.yaw-=.25f;else angle.pitch-=.25f;
  CharmPose(header,angle,length,neutral,foot);
  float origin[3];Point(neutral[bone].m,center,origin);
  if(axis)angle.yaw+=.5f;else angle.pitch+=.5f;
  CharmPose(header,angle,length,perturbed,foot);
  float point[3];Point(perturbed[bone].m,center,point);
  for(int i=0;i<3;++i)basis[axis?0:2][i]=(point[i]-origin[i])*(axis?1.f:-1.f);
  if(Dot(basis[axis?0:2],basis[axis?0:2])<1e-12f)return false;
  Unit(basis[axis?0:2]);
 }
 const float *z=basis[2],*x=basis[0];
 basis[1][0]=z[1]*x[2]-z[2]*x[1];basis[1][1]=z[2]*x[0]-z[0]*x[2];basis[1][2]=z[0]*x[1]-z[1]*x[0];
 if(Dot(basis[1],basis[1])<1e-8f)return false;
 Unit(basis[1]);return true;
}
bool CurrentCharmSwingBasis(studiohdr_t *header)
{
 if(!motion.biasReady)return false;
 if(motion.swingHeader!=header)
 {
  motion.swingHeader=header;motion.swingBasisReady=false;motion.swingBone=-1;
  std::fill(motion.swingCenter,motion.swingCenter+3,0.f);
  const auto bones=reinterpret_cast<const mstudiobone_t *>(reinterpret_cast<const byte *>(header)+header->boneindex);
  for(int i=0;i<header->numbones;++i)if(!strcmp(bones[i].name,"skin_10")){motion.swingBone=i;break;}
  unsigned count=0;
  for(const auto &triangle:Mesh(header).triangles)for(const auto &corner:triangle.corner)if(corner.bone==motion.swingBone)
  {for(int i=0;i<3;++i)motion.swingCenter[i]+=corner.p[i];++count;}
  if(count)for(float &value:motion.swingCenter)value/=count;else motion.swingBone=-1;
 }
 if(motion.swingBone<0)return false;
 if(motion.swingBasisReady&&motion.basisLength==frameConfig.length&&
    motion.basisBias.pitch==motion.bias.pitch&&motion.basisBias.yaw==motion.bias.yaw&&
    motion.basisFoot.pitch==motion.foot.pitch&&motion.basisFoot.yaw==motion.foot.yaw)return true;
 motion.swingBasisReady=false;
 if(!BuildCharmSwingBasis(header,motion.bias,motion.foot,frameConfig.length,motion.swingBone,motion.swingCenter,motion.swingBasis))return false;
 motion.basisBias=motion.bias;motion.basisFoot=motion.foot;motion.basisLength=frameConfig.length;
 motion.swingBasisReady=true;return true;
}

struct Placement
{
 geo::Frame frame,anchorFrame;
 geo::Bounds bounds;
 std::array<Matrix,MAXSTUDIOBONES> local,posed;
 std::vector<geo::Triangle> triangles;
 geo::SwingState swingState;
 geo::SwingOutput angles,foot;
 float velocity[3]{};
 float offset=.01f,minDepth=1e20f,boxMinDepth=1e20f;
 float contactGap=0,contactLateral=0;
};
float PendantDrop(const Placement &p)
{
 double height=0;unsigned count=0;
 for(const auto &triangle:p.triangles)if(triangle.bone==charmGun.pendantBone)
  for(const auto &point:triangle.p){height+=point[2];++count;}
 return count?float(p.anchorFrame.origin[2]-height/count):0.f;
}
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
 void PlacementDiagnostics(CStudioModelRenderer &r,studiohdr_t *header,const std::array<Matrix,MAXSTUDIOBONES> &base,
                           bool charm,Placement &p);
 void PosePlacement(CStudioModelRenderer &r,studiohdr_t *header,const std::array<Matrix,MAXSTUDIOBONES> &base,
                   int body,bool charm,bool left,Placement &p,bool localReady=false,bool diagnostics=true)
{
 p.frame=p.anchorFrame;
 if(!localReady)p.local=base;
 if(charm&&!localReady)CharmPose(header,p.angles,frameConfig.length,p.local,p.foot);
 else if(!charm&&left)
 {
  auto bones=reinterpret_cast<mstudiobone_t *>(reinterpret_cast<byte *>(header)+header->boneindex);
  for(int i=0;i<header->numbones;++i)if(!strncmp(bones[i].name,"digit",5))
   for(int row=0;row<3;++row)p.local[i].m[row][0]=-p.local[i].m[row][0];
 }
 // Fixed contact offset in model units; animation never changes the mount.
 p.offset=.01f;
 for(int i=0;i<3;++i)p.frame.origin[i]+=p.frame.normal[i]*p.offset;
 Matrix world=FrameMatrix(p.frame);
 for(int i=0;i<header->numbones;++i)p.posed[i]=Product(world,p.local[i]);
 geo::ReadStudioTriangles(Mesh(header),body,0,reinterpret_cast<const float (*)[3][4]>(p.posed.data()),p.triangles,false,true);
  if(diagnostics)PlacementDiagnostics(r,header,base,charm,p);
 }
 void PlacementDiagnostics(CStudioModelRenderer &r,studiohdr_t *header,const std::array<Matrix,MAXSTUDIOBONES> &base,
                           bool charm,Placement &p)
 {
  p.minDepth=1e20f;p.boxMinDepth=1e20f;
  for(int i=0;i<3;++i){p.bounds.min[i]=1e20f;p.bounds.max[i]=-1e20f;}
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
 // These values are diagnostics only. They never move or suppress an addon.
}
Placement EvaluatePlacement(CStudioModelRenderer &r,studiohdr_t *header,const std::array<Matrix,MAXSTUDIOBONES> &base,
                           int body,bool charm,bool left,const geo::Mount &mount)
{
 Placement p;p.local=base;p.foot=motion.foot;p.anchorFrame=MountFrame(mount,r.m_rgCachedLightTransform[mount.bone]);p.frame=p.anchorFrame;
 if(charm)
 {
  geo::SwingInput input;input.time=r.m_clTime;input.resetToken=motion.token;
  for(int i=0;i<3;++i)input.anchor[i]=p.frame.origin[i];
  float acceleration[3]{};const double dt=r.m_clTime-motion.time;
  if(motion.time>=0&&dt>0&&dt<=.1)
   for(int i=0;i<3;++i){p.velocity[i]=float((p.frame.origin[i]-motion.anchor[i])/dt);acceleration[i]=std::max(-400.f,std::min(400.f,float((p.velocity[i]-motion.velocity[i])/dt)));}
  input.anchorAcceleration[0]=Dot(acceleration,p.frame.axisX);input.anchorAcceleration[1]=Dot(acceleration,p.frame.axisY);input.anchorAcceleration[2]=Dot(acceleration,p.frame.normal);
  const float gravity[3]={0,0,-80};input.gravity[0]=Dot(gravity,p.frame.axisX);input.gravity[1]=Dot(gravity,p.frame.axisY);input.gravity[2]=Dot(gravity,p.frame.normal);
   // Rest/avoidance rotations can reverse the model's hinge axes. Project
   // inertia through the actual skinned pendant response, not the gun axes.
   if(CurrentCharmSwingBasis(header))
   {
    float localAcceleration[3],localGravity[3];
    std::copy(input.anchorAcceleration,input.anchorAcceleration+3,localAcceleration);
    std::copy(input.gravity,input.gravity+3,localGravity);
    for(int i=0;i<3;++i)
    {
     input.anchorAcceleration[i]=Dot(localAcceleration,motion.swingBasis[i]);
     input.gravity[i]=Dot(localGravity,motion.swingBasis[i]);
    }
   }
  input.teleport=motion.time>=0&&(dt<0||dt>.25);
  geo::SwingConfig cfg;cfg.amplitude=1;cfg.maxAngle=frameConfig.amplitude;cfg.length=frameConfig.length*2.5f;cfg.stiffness=frameConfig.stiffness;cfg.damping=frameConfig.damping;
  p.swingState=swing;p.angles=geo::UpdateSwing(p.swingState,cfg,input);
 }
  if(!charm)PosePlacement(r,header,base,body,false,left,p,false,true);
 return p;
}

geo::SwingOutput RecoverCharmFoot(const geo::SwingOutput &current,const geo::SwingOutput &rest,double dt)
{
 geo::SwingOutput result=current;
 if(!std::isfinite(dt)||dt<=0)return result;
 const float blend=float(1-std::exp(-12*std::max(0.,std::min(.05,dt))));
 const auto recover=[&](float value,float target)
 {
  const float delta=std::remainder(target-value,360.f);
  return std::fabs(delta)<.01f?target:value+delta*blend;
 };
 result.pitch=recover(current.pitch,rest.pitch);result.yaw=recover(current.yaw,rest.yaw);
 return result;
}

Placement ConstrainCharm(CStudioModelRenderer& r,studiohdr_t *header,
 const std::array<Matrix,MAXSTUDIOBONES>& base,int body,bool left,Placement desired)
{
  if(!BeginCharmGun(r,desired.anchorFrame)){CharmFailure();PosePlacement(r,header,base,body,true,left,desired,false,false);return desired;}
 auto charmBones=reinterpret_cast<mstudiobone_t *>(reinterpret_cast<byte *>(header)+header->boneindex);
 for(int i=0;i<header->numbones;++i)
 {
  if(!strcmp(charmBones[i].name,"skin_08"))charmGun.footBone=i;
  if(!strcmp(charmBones[i].name,"skin_10"))charmGun.pendantBone=i;
 }
 const auto pose=[&](geo::SwingOutput angles)
 {
  Placement p;p.anchorFrame=desired.anchorFrame;p.swingState=desired.swingState;p.angles=angles;p.foot=motion.foot;
  std::copy(desired.velocity,desired.velocity+3,p.velocity);
  PosePlacement(r,header,base,body,true,left,p,false,false);return p;
 };
 const auto free=[&](const Placement& p){return CharmFree(p.triangles);};
  const auto hangingFree=[&](const Placement& p){return PendantDrop(p)>=-.05f&&free(p);};
 const auto seekRest=[&](Placement& result)
 {
  // Continue a bounded search across frames. The old nested search could do
  // 14,600 complete poses, then repeat all of them on the very next frame.
  if(r.m_clTime<motion.searchRetry)return false;
  static const float pitch[]={0,-15,-30,-45,-60,-90,-120,-150,30,60,90,120};
  static const float yaw[]={0,25,-25,60,-60,100,-100,180,-180};
  static const float footPitch[]={0,-5,5,-10,10,-20,20,-35,35,-60,60,-75,75,-90,90,-120,120,-150,150,180};
  static const float footYaw[]={0,-15,15,-30,30,-60,60,-120,120,180};
  static const float bodyPitch[]={0,-30,-60,-90,-120,-150,60,120};
  static const float bodyYaw[]={0,-25,25,-60,60,-120,120,180,-180};
  const auto old=motion.foot;
  const auto started=std::chrono::steady_clock::now();
  for(unsigned attempted=0;attempted<32;++attempted)
  {
   unsigned cursor=motion.searchCursor++;geo::SwingOutput angles;
   if(cursor<108){motion.foot=old;angles.pitch=pitch[cursor/9];angles.yaw=yaw[cursor%9];}
   else
   {
    cursor-=108;
    if(cursor>=14400){motion.searchCursor=0;motion.searchRetry=r.m_clTime+.25;break;}
    const unsigned foot=cursor/72;motion.foot.pitch=footPitch[foot/10];motion.foot.yaw=footYaw[foot%10];
    angles.pitch=bodyPitch[(cursor%72)/9];angles.yaw=bodyYaw[cursor%9];
   }
   Placement test=pose(angles);
    if(hangingFree(test)){result=std::move(test);motion.searchCursor=0;motion.searchRetry=0;return true;}
   if(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()>=3)break;
  }
  motion.foot=old;return false;
 };
 // Find a fixed-root rest orientation when the hanging body already intersects
 // at zero. The mounting joint itself remains rigid at the saved gun point.
 if(!motion.biasReady)
 {
  Placement neutral=pose(geo::SwingOutput());
   bool found=hangingFree(neutral);
  if(!found)found=seekRest(neutral);
   if(!found){CharmFailure();PosePlacement(r,header,base,body,true,left,desired,false,false);return desired;}
   motion.bias=neutral.angles;motion.restFoot=motion.foot;motion.biasReady=true;
  motion.safeLocal=neutral.local;motion.safeAngles=neutral.angles;motion.charmSafe=true;
 }
 desired.angles.pitch+=motion.bias.pitch;desired.angles.yaw+=motion.bias.yaw;
 // Animation avoidance may articulate the support temporarily. Always aim
 // back at the original rest support, through the same complete mesh CCD.
 desired.foot=RecoverCharmFoot(motion.foot,motion.restFoot,r.m_clTime-motion.time);
 PosePlacement(r,header,base,body,true,left,desired,false,false);
 const bool desiredFree=free(desired);
 bool desiredUnchanged=true;
 Placement previous=desired;previous.local=motion.safeLocal;previous.angles=motion.safeAngles;previous.foot=motion.foot;
 PosePlacement(r,header,base,body,true,left,previous,!motion.charmRepose,false);
 bool previousFree=motion.charmSafe&&free(previous);
 if(!previousFree)
 {
  // A separate gun part can move into last frame's pose. Re-establish a free
  // orientation about this very same mount, never search another gun point.
  bool found=false;
  const auto seek=[&](float pitch,float yaw)
  {
   geo::SwingOutput angles;angles.pitch=pitch;angles.yaw=yaw;
   Placement test=pose(angles);if(!hangingFree(test))return false;
   previous=std::move(test);return true;
  };
  if(desiredFree){previous=desired;found=true;}
  else if(seek(motion.bias.pitch,motion.bias.yaw))found=true;
   if(!found)found=seekRest(previous);
  if(!found){CharmFailure();return desired;}
  PosePlacement(r,header,base,body,true,left,desired,false,false);
  desiredUnchanged=false;
  previousFree=true;
 }
 std::vector<geo::Triangle> from=previous.triangles,to=desired.triangles;
 LocalTriangles(from,charmGun.frame);LocalTriangles(to,charmGun.frame);
 geo::SwingPathOptions options;options.gap=kCharmCollisionGap;options.interpolationGuard=.00005f;
 // An unresolved interval keeps its last certified pose. Bound work during
 // firing without skipping triangles or accepting an unchecked swing segment.
 options.maximumQueries=16;
 const auto path=geo::CertifySwingPath(from,to,CharmGroups(from,header),
   [&](const std::vector<geo::Triangle>& triangles,float gap){return CharmFree(triangles,gap,true);},options);
 charmStats.segments+=path.certifiedSegments;
 if(path.fraction<1)
 {
  ++charmStats.limited;
  Placement accepted=desired;const float f=path.fraction;
  for(int i=0;i<header->numbones;++i)for(int a=0;a<3;++a)for(int b=0;b<4;++b)
   accepted.local[i].m[a][b]=previous.local[i].m[a][b]+(desired.local[i].m[a][b]-previous.local[i].m[a][b])*f;
  accepted.angles.pitch=previous.angles.pitch+(desired.angles.pitch-previous.angles.pitch)*f;
  accepted.angles.yaw=previous.angles.yaw+(desired.angles.yaw-previous.angles.yaw)*f;
   accepted.foot.pitch=previous.foot.pitch+(desired.foot.pitch-previous.foot.pitch)*f;
   accepted.foot.yaw=previous.foot.yaw+(desired.foot.yaw-previous.foot.yaw)*f;
  PosePlacement(r,header,base,body,true,left,accepted,true,false);
  desired=std::move(accepted);
  for(int i=0;i<2;++i)
  {
   const float angle=(i?desired.angles.yaw-motion.bias.yaw:desired.angles.pitch-motion.bias.pitch)*.0174532925199433f;
   desired.swingState.angle[i]=desired.swingState.previousAngle[i]=angle;
   desired.swingState.velocity[i]=0;
  }
 }
 // Rendering reposes the accepted matrices in float. Certify those resulting
 // triangles as well; retain the already checked previous pose on rounding
 // contact instead of publishing a pose that only passed interpolation math.
 // A full, unchanged desired pose already passed this identical query above.
 if((path.fraction<1||!desiredFree||!desiredUnchanged)&&!free(desired))
 {
  desired=std::move(previous);++charmStats.limited;
  for(int i=0;i<2;++i)
  {
   const float angle=(i?desired.angles.yaw-motion.bias.yaw:desired.angles.pitch-motion.bias.pitch)*.0174532925199433f;
   desired.swingState.angle[i]=desired.swingState.previousAngle[i]=angle;
   desired.swingState.velocity[i]=0;
  }
 }
 if(!desiredFree)++charmStats.solved;
 motion.safeLocal=desired.local;motion.safeAngles=desired.angles;motion.foot=desired.foot;
 motion.charmSafe=true;motion.charmRepose=false;
 return desired;
}

void DrawOne(CStudioModelRenderer &r,alight_t &lighting,float *lightdir,bool charm)
{
 OC2ColdTimer cold(charm?"view_charm":"view_counter",r.m_pRenderModel?r.m_pRenderModel->name:nullptr);
 const auto drawStart=std::chrono::steady_clock::now();
 const float u=charm?frameConfig.charmU:frameConfig.statU,v=charm?frameConfig.charmV:frameConfig.statV;
 geo::Mount mount=motion.requested[charm?1:0];
 if(!weaponSurface||mount.bone<0||mount.bone>=r.m_nCachedBones)return;
 // M249's moving ammunition box needs the longer physical mounting neck.
 // The chosen neck is fixed for the weapon; animation never moves the root.
 const bool longNeck=frameWeapon==20;
 const char *charmModel=longNeck?"models/attachments/oc2_sticker_charm_m249.mdl":"models/attachments/oc2_sticker_charm.mdl";
 model_t *model=IEngineStudio.Mod_ForName(charm?charmModel:"models/attachments/oc2_stattrak.mdl",false);
 studiohdr_t *header=model?reinterpret_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(model)):nullptr;
 if(!header)return;
 auto bind=addonBind.find(header);
 if(bind==addonBind.end())
 {
  std::array<Matrix,MAXSTUDIOBONES> parsed;
  if(!BindBones(header,parsed))return;
  bind=addonBind.insert(std::make_pair(header,parsed)).first;
 }
 const auto &base=bind->second;
 if(charm&&!LoadSkin(longNeck))return;
 if(charm)diag.charmNeck=longNeck?.6f:.2f;
 const bool left=gHUD.cl_righthand&&gHUD.cl_righthand->value<=0;
 const unsigned count=diag.count;
 const int body=charm?0:static_cast<int>(left?ReverseDigits(count):count);
 const int slot=charm?1:0;
 // Resolve the configured point once in BeginAccessories, then use exactly
 // that bone-local mount through every pose. No avoidance search, return-UV
 // interpolation, or near-plane fallback may change the player's placement.
 Placement selected=EvaluatePlacement(r,header,base,body,charm,left,mount);
 if(charm)
 {
  OC2ColdTimer solve("view_charm_solve",r.m_pRenderModel?r.m_pRenderModel->name:nullptr);
  selected=ConstrainCharm(r,header,base,body,left,std::move(selected));
    PlacementDiagnostics(r,header,base,true,selected);diag.pendantDrop=PendantDrop(selected);
  if(charmVerify&&charmVerify->value!=0)
  {
   ++charmStats.checks;
   const bool ready=CurrentCharmOracle();
   geo::CollisionResult rawHit,hit;
   if(ready)
   {
    rawHit=geo::CheckMeshCollision(charmGun.oracle,selected.triangles,.0001f);
    std::vector<geo::Triangle> actual=selected.triangles,outside;
    LocalTriangles(actual,charmGun.frame);
    geo::TrianglesOutsideMountContact(actual,outside,0,charmGun.footBone);
    hit=geo::CheckMeshCollision(charmGun.oracleLocal,outside,.0001f);
    if(rawHit.blocked){++charmStats.rawBlocked;if(!hit.blocked)++charmStats.contactOnly;}
   }
   if(!ready||hit.blocked)
   {
    ++charmStats.blocked;++runCharmBlocked;CharmFailure();
    static bool captured=false;
    if(!captured&&ready&&gRenderAPI.pfnSaveFile)
    {
     std::string packet;
     const auto append=[&](const std::vector<geo::Triangle>& triangles)
     {const int count=int(triangles.size());packet.append(reinterpret_cast<const char*>(&count),sizeof(count));packet.append(reinterpret_cast<const char*>(triangles.data()),triangles.size()*sizeof(geo::Triangle));};
     append(charmGun.oracle.triangles);append(selected.triangles);
     const std::string filename="oc2_charm_failure_"+std::to_string(PerfClock::now().time_since_epoch().count())+".bin";
     if(gRenderAPI.pfnSaveFile(filename.c_str(),packet.data(),int(packet.size())))
     {
      captured=true;
      const int queryBone=rawHit.accessoryTriangle>=0?selected.triangles[rawHit.accessoryTriangle].bone:-2;
      const int gunBone=rawHit.obstacleTriangle>=0?charmGun.oracle.triangles[rawHit.obstacleTriangle].bone:-2;
      gEngfuncs.Con_Printf("charm collision capture: file=%s weapon=%d seq=%d query_triangle=%d query_bone=%d gun_triangle=%d gun_bone=%d pitch=%.6f yaw=%.6f\n",
       filename.c_str(),frameWeapon,r.m_pCurrentEntity->curstate.sequence,rawHit.accessoryTriangle,queryBone,rawHit.obstacleTriangle,gunBone,selected.angles.pitch,selected.angles.yaw);
     }
    }
   }
  }
 }
 if(charm)
 {
  swing=selected.swingState;diag.pitch=selected.angles.pitch;diag.yaw=selected.angles.yaw;
  const double dt=r.m_clTime-motion.time;
  if(dt!=0||motion.time<0){motion.time=r.m_clTime;for(int i=0;i<3;++i){motion.anchor[i]=selected.anchorFrame.origin[i];motion.velocity[i]=selected.velocity[i];}}
  diag.steps=swing.steps;diag.resets=swing.resets;
 }
 diag.contactGap[slot]=selected.contactGap;diag.contactLateral[slot]=selected.contactLateral;
 diag.surfaceAdjusted[slot]=std::fabs(mount.actualUV[0]-u)>.001f||std::fabs(mount.actualUV[1]-v)>.001f;
 diag.testedCandidates[slot]=1;
 for(int i=0;i<3;++i)diag.mountLocal[slot][i]=mount.origin[i];
 diag.boxMinDepth=std::min(diag.boxMinDepth,selected.boxMinDepth);diag.minDepth=std::min(diag.minDepth,selected.minDepth);
 const auto &posed=selected.posed;
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
  int texture=0;bool hide=false;
  if(!OC2_InventoryCharmOverride(frameWeapon,texture,hide))texture=StickerTexture(frameConfig.sticker);
  auto textures=reinterpret_cast<mstudiotexture_t *>(reinterpret_cast<byte *>(header)+header->textureindex);
  for(int i=0;texture&&i<header->numtextures;++i)if(!strcmp(textures[i].name,"charm_sticker.bmp"))
   {swapped=&textures[i];savedTexture=swapped->index;swapped->index=texture;break;}
 }
 IEngineStudio.StudioSetupLighting(&lighting);IEngineStudio.StudioSetRemapColors(r.m_nTopColor,r.m_nBottomColor);
 r.StudioRenderModel(lightdir);
 if(swapped)swapped->index=savedTexture;
 *r.m_pCurrentEntity=savedEntity;r.m_pStudioHeader=savedHeader;r.m_pRenderModel=savedModel;
 r.m_pBodyPart=savedPart;r.m_pSubModel=savedSub;
 for(int i=0;i<r.m_nCachedBones;++i)
  {MatrixCopy(r.m_rgCachedBoneTransform[i],(*r.m_pbonetransform)[i]);MatrixCopy(r.m_rgCachedLightTransform[i],(*r.m_plighttransform)[i]);}
 IEngineStudio.StudioSetHeader(savedHeader);IEngineStudio.SetRenderModel(savedModel);
 IEngineStudio.StudioSetupLighting(&lighting);IEngineStudio.StudioSetRemapColors(r.m_nTopColor,r.m_nBottomColor);IEngineStudio.SetForceFaceFlags(savedFlags);
 ++diag.drawn;
 diag.drawMs[slot]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-drawStart).count();
 diag.triangles+=int(selected.triangles.size());
 if(charm){diag.charmBone=mount.bone;diag.charmOffset=selected.offset;diag.charmRequired=selected.offset;for(int i=0;i<2;++i)diag.charmUV[i]=mount.actualUV[i];}
 else{diag.statBone=mount.bone;diag.statOffset=selected.offset;diag.statRequired=selected.offset;for(int i=0;i<2;++i)diag.statUV[i]=mount.actualUV[i];}
}
}

void OC2_AccessoriesVidInit()
{
 const char *map=gEngfuncs.pfnGetLevelName();level=map?map:"";
 ResetCharmCollisionStats();runCharmFailures=runCharmBlocked=0;
 mounts.clear();meshes.clear();warmCharmColliders.clear();stickerTextures.clear();weaponSurface=nullptr;
 contactSamples.clear();addonBind.clear();charmGun=CharmGunScene();
 skinAttempted=skinValid=false;skinVariant=-1;joints.clear();carriers.clear();swing=geo::SwingState();motion=Motion();
 frameWeapon=0;diag=Diagnostics();nextRead=-1;lastTime=-1;
}
void OC2_PrecacheAccessoryMounts(model_s *model)
{
 if(!model||model->type!=mod_studio)return;
 auto *header=static_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(model));
 if(!header||mounts.count(header))return;
 geo::MountCache cache;geo::BuildMountCache(header,cache);
 if(cache.valid&&!cache.bind.empty())
 {
  std::vector<geo::Triangle> triangles;geo::MeshCollider collider;
  if(geo::ReadStudioTriangles(cache.studio,0,0,reinterpret_cast<const float (*)[3][4]>(cache.bind.data()),triangles,false,true)&&
     geo::BuildMeshCollider(triangles,collider,false))warmCharmColliders.emplace(header,std::move(collider));
 }
 mounts.emplace(header,std::move(cache));
}
void OC2_BeginAccessories(CStudioModelRenderer &r,int weapon)
{
 OC2ColdTimer cold("view_accessory_begin",r.m_pRenderModel?r.m_pRenderModel->name:nullptr);
 CpuScope timer;
 if(nativePerf.active)
 {
  const auto now=PerfClock::now();
  if(r.m_clTime>nativePerf.clientTime&&nativePerf.clientTime>=0)
  {
   nativePerf.intervals[nativePerf.count%nativePerf.intervals.size()]=std::chrono::duration<double,std::milli>(now-nativePerf.previous).count();++nativePerf.count;
  }
  if(r.m_clTime!=nativePerf.clientTime){nativePerf.previous=now;nativePerf.clientTime=r.m_clTime;}
 }
 const char *map=gEngfuncs.pfnGetLevelName();
 if(level!=(map?map:"")){OC2_AccessoriesVidInit();level=map?map:"";}
 ReadSettings(r.m_clTime);diag=Diagnostics();diag.weapon=weapon;frameWeapon=0;weaponSurface=nullptr;
 WeaponConfig instanceConfig;unsigned instanceCount=0;
 const bool instance=OC2_InventoryInstanceAccessories(weapon,instanceConfig,instanceCount);
 if(!instance&&!OC2_InventoryOwnAccessories(weapon))return;
 const bool left=gHUD.cl_righthand&&gHUD.cl_righthand->value<=0;
 WeaponConfig effective;
 const WeaponConfig *cfg=instance?&instanceConfig:Find(settings,weapon);
 if(!instance&&cfg)OC2_InventoryPublishAccessories(weapon,*cfg,counters.values[Index(weapon)]);
 const int charmSelection=OC2_InventoryCharmSelection(weapon);
 if(cfg&&charmSelection)
 {
  effective=*cfg;effective.charm=charmSelection>0;cfg=&effective;
 }
 diag.revision=settings.revision;if(cfg)diag.config=*cfg;
 if(cfg)SynchronizeMotion(*cfg,weapon,r.m_pStudioHeader,left,settings.revision);
 if((accessoriesEnabled&&accessoriesEnabled->value==0)||!cfg||(!cfg->stattrak&&!cfg->charm)||!IEngineStudio.IsHardware())return;
 auto it=mounts.find(r.m_pStudioHeader);
 if(it==mounts.end())
 {
  OC2ColdTimer cold("view_mount_build",r.m_pRenderModel?r.m_pRenderModel->name:nullptr);
  geo::MountCache cache;geo::BuildMountCache(r.m_pStudioHeader,cache);
  it=mounts.insert(std::make_pair(r.m_pStudioHeader,cache)).first;
 }
 if(!it->second.valid)return;
 frameWeapon=weapon;frameConfig=*cfg;weaponSurface=&it->second;
 // The requested mounts depend only on the current customization and static
 // model geometry. Their world frames still follow the current bone pose.
 for(int slot=0;slot<2;++slot)if((slot?cfg->charm:cfg->stattrak)&&!motion.requestedResolved[slot])
 {
  motion.requestedResolved[slot]=true;
  const float u=slot?cfg->charmU:cfg->statU,v=slot?cfg->charmV:cfg->statV;
  geo::FindMount(*weaponSurface,u,v,motion.requested[slot]);
 }
 diag.statEnabled=cfg->stattrak;diag.charmEnabled=cfg->charm;diag.sticker=cfg->sticker;diag.count=instance?instanceCount:counters.values[Index(weapon)];
}
void OC2_DrawAccessories(CStudioModelRenderer &r,alight_s &lighting,float *lightdir)
{
 CpuScope timer;
 if(!frameWeapon)return;
 if(frameConfig.stattrak)DrawOne(r,lighting,lightdir,false);
 if(frameConfig.charm)DrawOne(r,lighting,lightdir,true);
}
