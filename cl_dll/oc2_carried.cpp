#include "hud.h"
#include "cl_util.h"
#include "cl_entity.h"
#include "com_model.h"
#include "studio.h"
#include "r_studioint.h"
#include "StudioModelRenderer.h"
#include "skin_inventory_client.h"
#include "oc2_carried.h"
#include "oc2_world_stickers.h"
#include "oc2_item_appearance.h"
#include "oc2_accessories.h"
#include "oc2_carry_state.h"
#include "parsemsg.h"
#include "camera.h"
#include "pm_shared.h"
#include <cmath>
#include <chrono>
#include <cstring>
#include <string>
extern engine_studio_api_t IEngineStudio;

namespace
{
cvar_t *carriedEnabled=nullptr;
using CarryPerfClock=std::chrono::steady_clock;
struct CarryPerfStats
{
 bool active=false;
 double totalMs=0,prepareMs=0,bodyMs=0,utilityMs=0;
 unsigned totalCalls=0,prepareCalls=0,bodyCalls=0,utilityCalls=0;
} carryPerf;
struct CarryPerfScope
{
 bool active;double &elapsed;unsigned &calls;CarryPerfClock::time_point start;
 CarryPerfScope(double &ms,unsigned &count):active(carryPerf.active),elapsed(ms),calls(count)
 {if(active)start=CarryPerfClock::now();}
 void Stop()
 {
  if(!active)return;
  elapsed+=std::chrono::duration<double,std::milli>(CarryPerfClock::now()-start).count();
  ++calls;active=false;
 }
 ~CarryPerfScope(){Stop();}
};
void CarryPerfReset(){carryPerf=CarryPerfStats();carryPerf.active=true;}
void CarryPerfDump()
{
 carryPerf.active=false;
 gEngfuncs.Con_Printf("oc2_carry_perf: enabled=%d total_ms=%.6f total_calls=%u prepare_ms=%.6f prepare_calls=%u body_ms=%.6f body_calls=%u utility_ms=%.6f utility_calls=%u\n",
  int(!carriedEnabled||carriedEnabled->value!=0),carryPerf.totalMs,carryPerf.totalCalls,carryPerf.prepareMs,carryPerf.prepareCalls,
  carryPerf.bodyMs,carryPerf.bodyCalls,carryPerf.utilityMs,carryPerf.utilityCalls);
}
oc2_carry::State utilityState[33];
bool utilityValid[33]={};
bool utilitySnapshotStarted=false;
std::string utilityLevel;
struct CarryDrawRecord
{
 int weapon=0,serial=0;
 char anchor[16]={},model[MAX_MODEL_NAME]={},directionBasis[16]={};
 float direction[3]={};
};
struct CarryDrawSnapshot
{
 float time=0;
 int count=0;
 CarryDrawRecord records[12];
};
CarryDrawSnapshot carryDraw[33];
void CheckUtilityLevel();
void RememberDraw(int player,int weapon,int serial,const char *anchor,const model_t *model,
 const char *directionBasis,const float direction[3])
{
 if(player<1||player>32||!model)return;
 auto &snapshot=carryDraw[player];
 if(snapshot.count>=12)return;
 auto &record=snapshot.records[snapshot.count++];
 record.weapon=weapon;record.serial=serial;
 std::strncpy(record.anchor,anchor,sizeof(record.anchor)-1);record.anchor[sizeof(record.anchor)-1]=0;
 std::strncpy(record.model,model->name,sizeof(record.model)-1);record.model[sizeof(record.model)-1]=0;
 std::strncpy(record.directionBasis,directionBasis,sizeof(record.directionBasis)-1);record.directionBasis[sizeof(record.directionBasis)-1]=0;
 for(int i=0;i<3;++i)record.direction[i]=direction[i];
}
void CarryDump()
{
 CheckUtilityLevel();
 gEngfuncs.Con_Printf("oc2_carry: time=%.3f level=%s snapshot=%d\n",gEngfuncs.GetClientTime(),utilityLevel.c_str(),int(utilitySnapshotStarted));
 for(int i=1;i<=32;++i)
 {
  if(!utilityValid[i]&&!carryDraw[i].count)continue;
  const auto &state=utilityState[i];const auto &draw=carryDraw[i];
  gEngfuncs.Con_Printf("oc2_carry_player: player=%d valid=%d userid=%d active=%d flash=%d he=%d smoke=%d fire=%d flags=%d draw_time=%.3f draw_count=%d\n",
   i,int(utilityValid[i]),state.userid,state.active,state.flash,state.he,state.smoke,state.fire,state.flags,draw.time,draw.count);
  for(int j=0;j<draw.count;++j)
  {
   const auto &record=draw.records[j];
   gEngfuncs.Con_Printf("oc2_carry_draw: player=%d weapon=%d serial=%d anchor=%s model=%s direction_basis=%s direction_world=%.5f,%.5f,%.5f\n",
    i,record.weapon,record.serial,record.anchor,record.model,record.directionBasis,record.direction[0],record.direction[1],record.direction[2]);
  }
 }
}
void ClearUtility()
{
 for(int i=0;i<33;++i){utilityState[i]=oc2_carry::State();utilityValid[i]=false;carryDraw[i]=CarryDrawSnapshot();}
 utilitySnapshotStarted=false;
}
void CheckUtilityLevel()
{
 const char *level=gEngfuncs.pfnGetLevelName();
 const std::string current=level?level:"";
 if(current!=utilityLevel){ClearUtility();utilityLevel=current;}
}
int UtilityMessage(const char *,int size,void *buffer)
{
 if(!buffer||size<oc2_carry::kResetBytes)return 1;
 BufferReader reader(oc2_carry::kMessage,buffer,size);
 if(reader.ReadByte()!=oc2_carry::kVersion)return 1;
 const int operation=reader.ReadByte();
 if(operation==oc2_carry::Reset&&size==oc2_carry::kResetBytes)
 {CheckUtilityLevel();ClearUtility();utilitySnapshotStarted=true;return 1;}
 if(operation==oc2_carry::Remove&&size==oc2_carry::kRemoveBytes)
 {
  const int player=reader.ReadByte(),userid=reader.ReadLong();
  CheckUtilityLevel();
  if(utilitySnapshotStarted&&player>0&&player<=32&&utilityValid[player]&&utilityState[player].userid==userid)utilityValid[player]=false;
  return 1;
 }
 if(operation!=oc2_carry::Upsert||size!=oc2_carry::kUpsertBytes)return 1;
 oc2_carry::State state;
 state.player=reader.ReadByte();state.userid=reader.ReadLong();state.active=reader.ReadByte();
 state.flash=reader.ReadByte();state.he=reader.ReadByte();state.smoke=reader.ReadByte();state.fire=reader.ReadByte();state.flags=reader.ReadByte();
 if(state.player<1||state.player>32||state.userid<=0||state.active>31||
    (state.flags&~(oc2_carry::HasC4|oc2_carry::Incendiary|oc2_carry::ThrowReleased)))return 1;
 CheckUtilityLevel();
 if(utilitySnapshotStarted){utilityState[state.player]=state;utilityValid[state.player]=true;}
 return 1;
}
// Measured from the currently deployed world models in their bind pose. Their
// individual ground orientations differ, so each has its own centre and axis.
struct CarriedModel { const char *path; float x,y,z,axisX,axisY; };
const CarriedModel carriedModels[24]={
 {"models/w_p228.mdl",.09457f,.23166f,.50998f,.94416f,-.32949f},
 {"models/w_scout.mdl",2.17640f,-1.90540f,.80453f,.92889f,-.37035f},
 {"models/w_xm1014.mdl",.86731f,-5.49831f,.69625f,.05777f,-.99833f},
 {"models/w_mac10.mdl",2.64176f,-2.57046f,.98256f,1.f,-.00092f},
 {"models/w_aug.mdl",.74347f,-.52042f,1.48213f,.86221f,.50656f},
 {"models/w_elite.mdl",2.28547f,.60242f,1.40935f,.98558f,.16922f},
 {"models/w_fiveseven.mdl",.05862f,.27651f,.55090f,.83705f,-.54713f},
 {"models/w_ump45.mdl",-2.68532f,-.59403f,.98377f,.93997f,-.34126f},
 {"models/w_sg550.mdl",.70047f,-1.32291f,1.12754f,.88452f,.46650f},
 {"models/w_galil.mdl",-.31226f,.20103f,1.59252f,.23574f,.97182f},
 {"models/w_famas.mdl",-2.26292f,.85462f,1.23450f,.51883f,-.85488f},
 {"models/w_usp.mdl",3.54143f,-1.45796f,.68178f,.97262f,-.23240f},
 {"models/w_glock18.mdl",.76715f,-.21649f,.59986f,.89969f,.43654f},
 {"models/w_awp.mdl",6.35218f,.67368f,.59460f,.95469f,.29759f},
 {"models/w_mp7.mdl",1.35110f,-3.04616f,1.24000f,.06191f,.99808f},
 {"models/w_m249.mdl",1.08390f,-.15285f,3.43996f,.99259f,.12150f},
 {"models/w_m3.mdl",-1.81354f,-2.73969f,.81769f,.78071f,.62489f},
 {"models/w_m4a1.mdl",1.42390f,-.69295f,1.26717f,.86390f,-.50367f},
 {"models/w_tmp.mdl",-4.22057f,-4.10695f,1.05883f,.70150f,.71267f},
 {"models/w_g3sg1.mdl",.44638f,-1.08454f,.90538f,.94616f,.32371f},
 {"models/w_deagle.mdl",1.23525f,-.32469f,.50063f,.89846f,.43906f},
 {"models/w_sg552.mdl",1.79721f,.77239f,.41185f,.74465f,.66745f},
 {"models/w_ak47.mdl",.06185f,.16749f,.08438f,.95179f,.30674f},
 {"models/w_p90.mdl",-.43972f,.32918f,.88291f,.76637f,.64240f}
};
struct CarriedKnife { int definition; float z,axisX,axisY; };
const CarriedKnife carriedKnives[]={
 {0,.84076f,.00652f,.99998f},{42,.55545f,.00973f,-.99995f},
 {500,.70072f,.02557f,-.99967f},{503,.46040f,.02627f,-.99965f},
 {505,.51545f,.07280f,-.99735f},{506,.73095f,.00733f,.99997f},
 {507,.40930f,.13327f,.99108f},{508,.69563f,.05149f,-.99867f},
 {509,.48249f,.01344f,-.99991f},{512,.36771f,.08047f,-.99676f},
 {514,.57064f,.09367f,-.99560f},{515,.44629f,.10531f,-.99444f},
 {516,.82838f,.69615f,.71789f},{517,.38053f,.06264f,-.99804f},
 {518,.38024f,.01870f,-.99983f},{519,.51987f,.03609f,-.99935f},
 {520,.41770f,.12520f,-.99213f},{521,.39065f,.08099f,-.99672f},
 {522,.62968f,.00402f,-.99999f},{523,.43771f,.16040f,.98705f},
 {525,.26881f,.04241f,-.99910f}
};
bool CarriedPistol(int id)
{ return id==1||id==10||id==11||id==16||id==17||id==26; }
bool HeaderRange(const studiohdr_t *header,int offset,int count,std::size_t stride)
{
 return header&&header->length>=0&&stride>0&&offset>=0&&count>=0&&std::size_t(offset)<=std::size_t(header->length)&&
  std::size_t(count)<=((std::size_t(header->length)-std::size_t(offset))/stride);
}
bool CarryDeathSequence(const studiohdr_t *header,int sequence)
{
 if(!HeaderRange(header,header?header->seqindex:0,header?header->numseq:0,sizeof(mstudioseqdesc_t))||
    sequence<0||sequence>=header->numseq)return true;
 const auto *seq=reinterpret_cast<const mstudioseqdesc_t *>(reinterpret_cast<const byte *>(header)+header->seqindex)+sequence;
 return !strncmp(seq->label,"death",5)||!strncmp(seq->label,"crouch_die",sizeof(seq->label))||
  !strncmp(seq->label,"head",sizeof(seq->label))||!strncmp(seq->label,"gutshot",sizeof(seq->label))||
  !strncmp(seq->label,"left",sizeof(seq->label))||!strncmp(seq->label,"back",sizeof(seq->label))||
  !strncmp(seq->label,"right",sizeof(seq->label))||!strncmp(seq->label,"forward",sizeof(seq->label));
}
void CarryConcat(const float a[3][4],const float b[3][4],float out[3][4])
{
 for(int i=0;i<3;++i)for(int j=0;j<4;++j)
  out[i][j]=a[i][0]*b[0][j]+a[i][1]*b[1][j]+a[i][2]*b[2][j]+(j==3?a[i][3]:0);
}
void CarryBind(const mstudiobone_t &bone,float out[3][4])
{
 const float sx=std::sin(bone.value[3]),cx=std::cos(bone.value[3]);
 const float sy=std::sin(bone.value[4]),cy=std::cos(bone.value[4]);
 const float sz=std::sin(bone.value[5]),cz=std::cos(bone.value[5]);
 out[0][0]=cz*cy;out[0][1]=cz*sy*sx-sz*cx;out[0][2]=cz*sy*cx+sz*sx;
 out[1][0]=sz*cy;out[1][1]=sz*sy*sx+cz*cx;out[1][2]=sz*sy*cx-cz*sx;
 out[2][0]=-sy;out[2][1]=cy*sx;out[2][2]=cy*cx;
 for(int i=0;i<3;++i)out[i][3]=bone.value[i];
}
struct UtilityModel { const char *path;float x,y,z,axisX,axisY,scale; };
const UtilityModel flashModel={"models/w_flashbang.mdl",2.85906f,-.50960f,1.57197f,.99970f,.02446f,.65f};
const UtilityModel heModel={"models/w_hegrenade.mdl",6.37906f,-.03460f,2.54697f,.96513f,.26176f,.80f};
const UtilityModel smokeModel={"models/w_smokegrenade.mdl",.98406f,-.02460f,2.14198f,.99460f,.10379f,.65f};
const UtilityModel molotovModel={"models/w_molotov.mdl",5.05342f,.57512f,3.05426f,1.f,-.00179f,.75f};
const UtilityModel incendiaryModel={"models/w_incendiary.mdl",2.34906f,.56540f,4.10698f,.99998f,-.00556f,.40f};

void DrawUtility(CStudioModelRenderer &r,alight_s &lighting,float *lightdir,int player,int weapon,int anchor,
 const UtilityModel *spec,const float target[3],bool backpack)
{
 CarryPerfScope utilityPerf(carryPerf.utilityMs,carryPerf.utilityCalls);
 if(anchor<0)return;
 model_t *model=IEngineStudio.Mod_ForName(backpack?"models/w_backpack.mdl":spec->path,false);
 auto *header=model&&model->type==mod_studio?static_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(model)):nullptr;
 if(!HeaderRange(header,header?header->boneindex:0,header?header->numbones:0,sizeof(mstudiobone_t))||
    !HeaderRange(header,header?header->seqindex:0,header?header->numseq:0,sizeof(mstudioseqdesc_t))||
    header->numbones<1||header->numbones>MAXSTUDIOBONES||header->numseq<1)return;
 const auto *bones=reinterpret_cast<const mstudiobone_t *>(reinterpret_cast<const byte *>(header)+header->boneindex);
 float pose[MAXSTUDIOBONES][3][4];
 for(int i=0;i<header->numbones;++i)
 {
  const int parent=bones[i].parent;
  if(parent < -1||parent>=i)return;
  float local[3][4];CarryBind(bones[i],local);
  if(parent<0)memcpy(pose[i],local,sizeof(local));else CarryConcat(pose[parent],local,pose[i]);
 }
 float orient[3][4]={},lightOrient[3][4]={};
 if(!backpack)
 {
  orient[0][0]=spec->axisX;orient[0][1]=spec->axisY;
  orient[1][2]=1;orient[2][0]=spec->axisY;orient[2][1]=-spec->axisX;
  memcpy(lightOrient,orient,sizeof(orient));
  for(int i=0;i<3;++i)for(int j=0;j<3;++j)orient[i][j]*=spec->scale;
  const float centre[3]={spec->x,spec->y,spec->z};
  for(int i=0;i<3;++i)orient[i][3]=target[i]-orient[i][0]*centre[0]-orient[i][1]*centre[1]-orient[i][2]*centre[2];
  for(int i=0;i<3;++i)lightOrient[i][3]=orient[i][3];
 }
 const cl_entity_t savedEntity=*r.m_pCurrentEntity;
 auto *savedHeader=r.m_pStudioHeader;auto *savedModel=r.m_pRenderModel;
 auto *savedPart=r.m_pBodyPart;auto *savedSub=r.m_pSubModel;
 const int faceFlags=IEngineStudio.GetForceFaceFlags();
 float savedBones[MAXSTUDIOBONES][3][4],savedLights[MAXSTUDIOBONES][3][4];
 memcpy(savedBones,*r.m_pbonetransform,header->numbones*sizeof(savedBones[0]));
 memcpy(savedLights,*r.m_plighttransform,header->numbones*sizeof(savedLights[0]));
 r.m_pCurrentEntity->model=model;r.m_pCurrentEntity->curstate.body=0;r.m_pCurrentEntity->curstate.skin=0;
 r.m_pCurrentEntity->curstate.sequence=0;r.m_pCurrentEntity->curstate.frame=0;
 r.m_pCurrentEntity->curstate.framerate=0;r.m_pCurrentEntity->curstate.animtime=r.m_clTime;
 r.m_pStudioHeader=header;r.m_pRenderModel=model;
 IEngineStudio.StudioSetHeader(header);IEngineStudio.SetRenderModel(model);
 if(backpack)r.StudioMergeBones(model);
 else for(int i=0;i<header->numbones;++i)
 {
  float local[3][4],lightLocal[3][4];CarryConcat(orient,pose[i],local);CarryConcat(lightOrient,pose[i],lightLocal);
  CarryConcat(r.m_rgCachedBoneTransform[anchor],local,(*r.m_pbonetransform)[i]);
  CarryConcat(r.m_rgCachedLightTransform[anchor],lightLocal,(*r.m_plighttransform)[i]);
 }
 IEngineStudio.StudioSetupLighting(&lighting);r.StudioRenderModel(lightdir);
 float direction[3];
 if(backpack)
  for(int i=0;i<3;++i)direction[i]=(*r.m_pbonetransform)[0][i][0];
 else
  for(int i=0;i<3;++i)
   direction[i]=r.m_rgCachedBoneTransform[anchor][i][0]*(lightOrient[0][0]*spec->axisX+lightOrient[0][1]*spec->axisY)+
    r.m_rgCachedBoneTransform[anchor][i][1]*(lightOrient[1][0]*spec->axisX+lightOrient[1][1]*spec->axisY)+
    r.m_rgCachedBoneTransform[anchor][i][2]*(lightOrient[2][0]*spec->axisX+lightOrient[2][1]*spec->axisY);
 RememberDraw(player,weapon,0,backpack?"Spine2":"Pelvis",model,backpack?"root_x":"model_axis",direction);
 *r.m_pCurrentEntity=savedEntity;r.m_pStudioHeader=savedHeader;r.m_pRenderModel=savedModel;
 r.m_pBodyPart=savedPart;r.m_pSubModel=savedSub;
 memcpy(*r.m_pbonetransform,savedBones,header->numbones*sizeof(savedBones[0]));
 memcpy(*r.m_plighttransform,savedLights,header->numbones*sizeof(savedLights[0]));
 IEngineStudio.StudioSetHeader(savedHeader);IEngineStudio.SetRenderModel(savedModel);
 IEngineStudio.StudioSetupLighting(&lighting);IEngineStudio.SetForceFaceFlags(faceFlags);
}
}

void OC2_CarryInit()
{
 ClearUtility();utilityLevel.clear();gEngfuncs.pfnHookUserMsg(oc2_carry::kMessage,UtilityMessage);
 gEngfuncs.pfnAddCommand("oc2_carry_dump",CarryDump);
 carriedEnabled=gEngfuncs.pfnRegisterVariable("cl_oc2_carried","1",0);
 gEngfuncs.pfnAddCommand("oc2_carry_perf_reset",CarryPerfReset);
 gEngfuncs.pfnAddCommand("oc2_carry_perf_dump",CarryPerfDump);
}
void OC2_CarryFrame() { CheckUtilityLevel(); }

void OC2_InventoryDrawCarried(CStudioModelRenderer &renderer,const entity_state_s *player,alight_s &lighting,float *lightdir)
{
 CarryPerfScope totalPerf(carryPerf.totalMs,carryPerf.totalCalls);
 if(carriedEnabled&&carriedEnabled->value==0)return;
 const auto *entity=renderer.m_pCurrentEntity;
 if(player&&player->number>=1&&player->number<=32&&entity&&entity->player&&entity->index==player->number)
 {
  carryDraw[player->number]=CarryDrawSnapshot();
  carryDraw[player->number].time=gEngfuncs.GetClientTime();
 }
 if(!player||player->number<1||player->number>32||!entity||!entity->player||entity->index!=player->number||player->spectator||
    g_PlayerExtraInfo[player->number].dead||
    entity->curstate.renderfx==kRenderFxDeadPlayer||CarryDeathSequence(renderer.m_pStudioHeader,player->sequence)||!IEngineStudio.IsHardware())return;
 const auto *local=gEngfuncs.GetLocalPlayer();
 if((local&&local->index==player->number&&!CL_IsThirdPerson())||
    (g_iUser1==OBS_IN_EYE&&g_iUser2==player->number))return;
 CheckUtilityLevel();
 const player_info_t *info=IEngineStudio.PlayerInfo(player->number-1);
 const oc2_carry::State *utility=utilityValid[player->number]&&info&&
  info->userid==utilityState[player->number].userid?&utilityState[player->number]:nullptr;
 oc2_item::Appearance owned[64];
 const int count=OC2_InventoryOwnedItems(player->number,owned,64);
 if(count<0||count>64)return;
 int spine=-1,pelvis=-1;
 for(int i=0;i<renderer.m_nCachedBones;++i)
 {
  if(!stricmp(renderer.m_nCachedBoneNames[i],"Bip01 Spine2"))spine=i;
  if(!stricmp(renderer.m_nCachedBoneNames[i],"Bip01 Pelvis"))pelvis=i;
 }
 bool drawnPrimary=false,drawnPistol=false,drawnKnife=false;
 for(int ownedIndex=0;ownedIndex<count;++ownedIndex)
 {
  const auto &item=owned[ownedIndex];
  const int index=oc2_accessories::Index(item.weapon);
  const bool knife=item.weapon==29;
  if(item.owner!=player->number||(item.flags&oc2_item::Active)||(!knife&&index<0))continue;
  const bool pistol=CarriedPistol(item.weapon);
  const int anchor=(pistol||knife)?pelvis:spine;
  if(anchor<0||(knife?drawnKnife:pistol?drawnPistol:drawnPrimary))continue;
  CarryPerfScope preparePerf(carryPerf.prepareMs,carryPerf.prepareCalls);
  CarriedModel spec;
  model_t *model=nullptr;
  int displayDefinition=0;
  if(knife)
  {
   model=OC2_InventoryWorldKnifeModel(item.definition,displayDefinition);
   const CarriedKnife *choice=nullptr;
   for(const auto &candidate:carriedKnives)if(candidate.definition==displayDefinition){choice=&candidate;break;}
   if(!choice)continue;
   spec={nullptr,-.00664f,.00134f,choice->z,choice->axisX,choice->axisY};
  }
  else {spec=carriedModels[index];model=IEngineStudio.Mod_ForName(spec.path,false);}
  auto *header=model&&model->type==mod_studio?static_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(model)):nullptr;
  if(!HeaderRange(header,header?header->boneindex:0,header?header->numbones:0,sizeof(mstudiobone_t))||
     header->numbones<1||header->numbones>MAXSTUDIOBONES||(!knife&&header->numbones!=1))continue;
  const auto *bones=reinterpret_cast<const mstudiobone_t *>(reinterpret_cast<const byte *>(header)+header->boneindex);
  float bindPose[MAXSTUDIOBONES][3][4];bool validBones=true;
  for(int i=0;i<header->numbones;++i)
  {
   const int parent=bones[i].parent;
   if(parent < -1||parent>=i){validBones=false;break;}
   float localBind[3][4];CarryBind(bones[i],localBind);
   if(parent<0)memcpy(bindPose[i],localBind,sizeof(localBind));
   else CarryConcat(bindPose[parent],localBind,bindPose[i]);
  }
  if(!validBones)continue;
  // W models rest on their side. Their measured long axes have arbitrary
  // signs: use the actual muzzle/blade end, including the paired models.
  float ax=spec.axisX,ay=spec.axisY;
  if((knife&&(displayDefinition==0||displayDefinition==506))||
     (!knife&&(item.weapon==14||item.weapon==19))) {ax=-ax;ay=-ay;}
  if(knife&&displayDefinition==516) {ax=-.966f;ay=.258f;}
  if(!knife&&item.weapon==10) {const float x=ax;ax=-ay;ay=x;}
  // Pelvis X points up, Y forward, Z to the player's left. Attach the
  // primary at the spine position but use pelvis axes: the two shipped player
  // models have substantially different Spine2 roll in their idle poses.
  float orient[3][4]={};
  if(knife)
  {
   orient[0][0]=-ax;orient[0][1]=-ay;
   orient[1][0]=ay;orient[1][1]=-ax;
   orient[2][2]=1;
  }
  else if(pistol)
  {
   orient[0][0]=-ax;orient[0][1]=-ay;
   orient[1][0]=-ay;orient[1][1]=ax;
   orient[2][2]=-1;
  }
  else
  {
   const float up=.8660254f,right=.5f;
   orient[0][0]=-up*ax+right*ay;orient[0][1]=-up*ay-right*ax;
   orient[1][2]=-1;
   orient[2][0]=right*ax+up*ay;orient[2][1]=right*ay-up*ax;
   if(item.weapon==15)
   {
    // FAMAS's sticker face is -Z in the W bind pose, unlike other guns.
    // Rotate its side and normal 180 degrees around the unchanged muzzle.
    orient[0][0]=-up*ax-right*ay;orient[0][1]=-up*ay+right*ax;
    orient[1][2]=1;
    orient[2][0]=right*ax-up*ay;orient[2][1]=right*ay+up*ax;
   }
  }
  const float centre[3]={spec.x,spec.y,spec.z};
  const bool backpack=utility&&(utility->flags&oc2_carry::HasC4)&&utility->active!=6;
  // Keep the weapon against the vest; the pack needs only a small extra
  // offset. The former 10/13-unit gap left the rifle visibly floating.
  const float target[3]={knife||pistol?0.f:2.f,knife||pistol?0.f:backpack?-6.5f:-6.f,knife?9.f:pistol?-9.f:0.f};
  for(int i=0;i<3;++i)orient[i][3]=target[i]-orient[i][0]*centre[0]-orient[i][1]*centre[1]-orient[i][2]*centre[2];
  const cl_entity_t savedEntity=*renderer.m_pCurrentEntity;
  auto *savedHeader=renderer.m_pStudioHeader;auto *savedModel=renderer.m_pRenderModel;
  auto *savedPart=renderer.m_pBodyPart;auto *savedSub=renderer.m_pSubModel;
  const int faceFlags=IEngineStudio.GetForceFaceFlags();
  float savedBones[MAXSTUDIOBONES][3][4],savedLights[MAXSTUDIOBONES][3][4];
  memcpy(savedBones,*renderer.m_pbonetransform,header->numbones*sizeof(savedBones[0]));
  memcpy(savedLights,*renderer.m_plighttransform,header->numbones*sizeof(savedLights[0]));
  renderer.m_pCurrentEntity->model=model;renderer.m_pCurrentEntity->curstate.body=0;renderer.m_pCurrentEntity->curstate.skin=0;
  renderer.m_pStudioHeader=header;renderer.m_pRenderModel=model;
  float carryBone[3][4],carryLight[3][4];
  memcpy(carryBone,renderer.m_rgCachedBoneTransform[anchor],sizeof(carryBone));
  memcpy(carryLight,renderer.m_rgCachedLightTransform[anchor],sizeof(carryLight));
  if(!knife&&!pistol&&pelvis>=0)
   for(int row=0;row<3;++row)for(int col=0;col<3;++col)
   {
    carryBone[row][col]=renderer.m_rgCachedBoneTransform[pelvis][row][col];
    carryLight[row][col]=renderer.m_rgCachedLightTransform[pelvis][row][col];
   }
  for(int i=0;i<header->numbones;++i)
  {
   float localMatrix[3][4];CarryConcat(orient,bindPose[i],localMatrix);
   CarryConcat(carryBone,localMatrix,(*renderer.m_pbonetransform)[i]);
   CarryConcat(carryLight,localMatrix,(*renderer.m_plighttransform)[i]);
  }
  preparePerf.Stop();
  IEngineStudio.StudioSetHeader(header);IEngineStudio.SetRenderModel(model);IEngineStudio.StudioSetupLighting(&lighting);
  { OC2_InventoryMaterialScope materials(renderer,model,player->number,item.serial);
    CarryPerfScope bodyPerf(carryPerf.bodyMs,carryPerf.bodyCalls);renderer.StudioRenderModel(lightdir); }
  float direction[3];
  for(int i=0;i<3;++i)
   direction[i]=carryBone[i][0]*(orient[0][0]*ax+orient[0][1]*ay)+
    carryBone[i][1]*(orient[1][0]*ax+orient[1][1]*ay)+
    carryBone[i][2]*(orient[2][0]*ax+orient[2][1]*ay);
  RememberDraw(player->number,item.weapon,item.serial,knife||pistol?"Pelvis":"Spine2",model,
   knife?"blade":"muzzle",direction);
  if(!knife)OC2_DrawWorldStickers(renderer,model,'c',player->number,item.serial,lightdir);
  if(!knife)OC2_DrawWorldAccessories(renderer,model,player->number,item.serial,lighting,lightdir);
  *renderer.m_pCurrentEntity=savedEntity;renderer.m_pStudioHeader=savedHeader;renderer.m_pRenderModel=savedModel;
  renderer.m_pBodyPart=savedPart;renderer.m_pSubModel=savedSub;
  memcpy(*renderer.m_pbonetransform,savedBones,header->numbones*sizeof(savedBones[0]));
  memcpy(*renderer.m_plighttransform,savedLights,header->numbones*sizeof(savedLights[0]));
  IEngineStudio.StudioSetHeader(savedHeader);IEngineStudio.SetRenderModel(savedModel);
  IEngineStudio.StudioSetupLighting(&lighting);IEngineStudio.SetForceFaceFlags(faceFlags);
  if(knife)drawnKnife=true;else if(pistol)drawnPistol=true;else drawnPrimary=true;
 }
 if(!utility)return;
 // Throw recovery keeps the weapon active after the release attempt. Successful
 // projectile creation decrements ammo; failure leaves it unchanged. In either
 // case the release animation is finishing, so the remaining ammo is carried.
 const bool inHand=(utility->flags&oc2_carry::ThrowReleased)==0;
 int flash=utility->flash-(inHand&&utility->active==25&&utility->flash>0?1:0);
 int he=utility->he-(inHand&&utility->active==4&&utility->he>0?1:0);
 int smoke=utility->smoke-(inHand&&utility->active==9&&utility->smoke>0?1:0);
 int fire=utility->fire-(inHand&&utility->active==31&&utility->fire>0?1:0);
 // Pelvis Y points toward the front of the player. Keep carried utilities on
 // the rear-facing belt plane so the body does not occlude them from behind.
 // The three inner slots sit between the existing fire and smoke positions.
 const float flashA[3]={-2.f,-3.f,-3.f},flashB[3]={-2.f,-3.f,0.f};
 const float hePos[3]={-2.f,-3.f,3.f},smokePos[3]={-2.f,-3.f,6.f},firePos[3]={-2.f,-3.f,-6.f};
 if((utility->flags&oc2_carry::HasC4)&&utility->active!=6)
  DrawUtility(renderer,lighting,lightdir,player->number,6,spine,nullptr,nullptr,true);
 if(flash>0)DrawUtility(renderer,lighting,lightdir,player->number,25,pelvis,&flashModel,flashA,false);
 if(flash>1)DrawUtility(renderer,lighting,lightdir,player->number,25,pelvis,&flashModel,flashB,false);
 if(he>0)DrawUtility(renderer,lighting,lightdir,player->number,4,pelvis,&heModel,hePos,false);
 if(smoke>0)DrawUtility(renderer,lighting,lightdir,player->number,9,pelvis,&smokeModel,smokePos,false);
 if(fire>0)DrawUtility(renderer,lighting,lightdir,player->number,31,pelvis,
  utility->flags&oc2_carry::Incendiary?&incendiaryModel:&molotovModel,firePos,false);
}
