#include "BaseMenu.h"
#include "PanoramaModelView.h"
#include "../client/BuyModelPreview.h"
#include "../../../../common/oc2_glove_inventory.h"
// GoldSrc's anonymous bone typedefs predate the menu SDK's tagged forward
// declarations. Keep their names local while sharing their on-disk layout.
#define mstudiobone_t oc2_preview_bone_t
#define mstudioanim_t oc2_preview_anim_t
#include "../../../../cl_dll/accessory_geometry.h"
#undef mstudioanim_t
#undef mstudiobone_t
#include "const.h"
#include "ref_params.h"
#include "cl_entity.h"
#include "com_model.h"
#include "entity_types.h"
#include "keydefs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace
{
namespace geo=oc2_accessory;
const float kRadians=.0174532925199433f,kDegrees=57.2957795130823f;
struct PreviewAPI
{
    unsigned version;
    void *(*StudioHeader)(cl_entity_t *);
    int (*LoadTexture)(const char *,int);
    void (*FreeTexture)(int);
    int (*OwnTexture)(int,const char *);
    void (*SetTransform)(cl_entity_t *,const float matrix[3][4]);
    void (*SetScene)(int);
};
PreviewAPI *API()
{
    if(!EngFuncs::textfuncs.pfnGetNativeObject)return nullptr;
    auto *api=static_cast<PreviewAPI *>(EngFuncs::textfuncs.pfnGetNativeObject("OC2ModelPreview002"));
    return api&&api->version==2?api:nullptr;
}
class PreviewScene
{
    PreviewAPI *api;
public:
    PreviewScene():api(API()){if(api)api->SetScene(1);}
    ~PreviewScene(){if(api)api->SetScene(0);}
};
bool Read(const char *path,std::string &text)
{
    int length=0;byte *bytes=EngFuncs::COM_LoadFile(path,&length);
    if(!bytes)return false;
    const bool valid=length>=0&&length<=4*1024*1024&&std::memchr(bytes,0,std::size_t(length))==nullptr;
    if(valid)text.assign(reinterpret_cast<char *>(bytes),std::size_t(length));
    EngFuncs::COM_FreeFile(bytes);return valid;
}
struct Resources
{
    bool loaded=false;
    std::uint64_t signature=1469598103934665603ULL;
    oc2_inventory::Catalog skins;
    oc2_gloves::Catalog gloves;
    oc2_accessories::Catalog patterns;
    oc2_inventory::Config loadout;
    oc2_gloves::Config gloveLoadout;
    oc2_accessories::Config accessories;
    oc2_accessories::Counts counts;
    void Load(bool force=false)
    {
        if(loaded&&!force)return;loaded=true;
        std::string text,error;
        if(skins.empty()&&Read(oc2_inventory::kCatalogPath,text))oc2_inventory::ParseCatalog(text.c_str(),skins,error);
        if(gloves.empty()&&Read(oc2_gloves::kCatalogPath,text))oc2_gloves::ParseCatalog(text,gloves,error);
        if(patterns.empty()&&Read(oc2_accessories::kCatalogPath,text))oc2_accessories::ParseCatalog(text.c_str(),patterns,error);
        loadout=oc2_inventory::DefaultConfig();gloveLoadout=oc2_gloves::DefaultConfig();accessories=oc2_accessories::DefaultConfig();counts=oc2_accessories::DefaultCounts();
        if(Read(oc2_inventory::kConfigPath,text))oc2_inventory::ParseConfig(text.c_str(),loadout,error);
        if(Read(oc2_gloves::kConfigPath,text))oc2_gloves::ParseConfig(text,gloveLoadout,error);
        if(Read(oc2_accessories::kConfigPath,text))oc2_accessories::ParseConfig(text.c_str(),accessories,error);
        if(Read(oc2_accessories::kCountsPath,text))oc2_accessories::ParseCounts(text.c_str(),counts,error);
        signature=1469598103934665603ULL;
        for(const char *path:{oc2_inventory::kConfigPath,oc2_gloves::kConfigPath,oc2_accessories::kConfigPath})
            if(Read(path,text))for(unsigned char c:text){signature^=c;signature*=1099511628211ULL;}
    }
};
Resources resources;
struct TextureEntry {int handle=0;unsigned used=0;};
std::map<std::string,TextureEntry> textures;
unsigned textureUse=0;
int Texture(const std::string &path,bool sticker=false)
{
    PreviewAPI *api=API();if(!api||path.empty())return 0;
    auto found=textures.find(path);
    if(found!=textures.end()&&api->OwnTexture(found->second.handle,path.c_str()))
    {found->second.used=++textureUse;return found->second.handle;}
    if(found!=textures.end())textures.erase(found);
    // These names are private to the front end; releasing a previous card's
    // texture cannot invalidate the client's in-game inventory texture cache.
    while(textures.size()>=48)
    {
        auto oldest=textures.begin();
        for(auto i=textures.begin();i!=textures.end();++i)if(i->second.used<oldest->second.used)oldest=i;
        if(api->OwnTexture(oldest->second.handle,oldest->first.c_str()))api->FreeTexture(oldest->second.handle);
        textures.erase(oldest);
    }
    const int handle=api->LoadTexture(path.c_str(),sticker?1:0);
    if(handle){TextureEntry entry;entry.handle=handle;entry.used=++textureUse;textures[path]=entry;}
    return handle;
}
int StickerTexture(int id)
{
    if(id<0)return Texture(oc2_inventory::kTransparentPath,true);
    const auto *entry=oc2_inventory::FindEntry(resources.skins,id);
    return entry&&entry->kind==oc2_inventory::Kind::Sticker?Texture(entry->file,true):0;
}
bool MaterialMatches(const std::string &list,const char *name)
{
    std::size_t start=0;
    for(;;)
    {
        const std::size_t end=list.find(',',start);
        if(!stricmp(list.substr(start,end==std::string::npos?end:end-start).c_str(),name))return true;
        if(end==std::string::npos)return false;start=end+1;
    }
}
bool HiddenMaterial(const char *name,bool gloves)
{
    const std::string lower=oc2_inventory::Lower(name);
    if(lower=="null.bmp")return true;
    if(gloves)return lower.find("sleeve")!=std::string::npos||lower=="view_skin.bmp"||lower.find("bare_arm")!=std::string::npos;
    return lower.find("glove")!=std::string::npos||lower.find("finger")!=std::string::npos||
        lower=="view_skin.bmp"||lower.find("bare_arm")!=std::string::npos||lower.find("sleeve")!=std::string::npos;
}
struct Matrix {float m[3][4]{};};
Matrix Product(const Matrix &a,const Matrix &b)
{
    Matrix result;
    for(int r=0;r<3;++r)for(int c=0;c<4;++c)
    {
        for(int k=0;k<3;++k)result.m[r][c]+=a.m[r][k]*b.m[k][c];
        if(c==3)result.m[r][c]+=a.m[r][3];
    }
    return result;
}
void Transform(const float m[3][4],const float p[3],float out[3],bool point=true)
{for(int i=0;i<3;++i)out[i]=m[i][0]*p[0]+m[i][1]*p[1]+m[i][2]*p[2]+(point?m[i][3]:0);}
float Dot(const float *a,const float *b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
void Normalize(float *p)
{const float length=std::sqrt(Dot(p,p));if(length>1e-6f)for(int i=0;i<3;++i)p[i]/=length;}
typedef std::vector<std::array<float,6>> BoneValues;
bool Bind(studiohdr_t *header,std::vector<geo::BindMatrix> &out,const BoneValues *pose=nullptr)
{
    if(!header||header->numbones<1||header->numbones>MAXSTUDIOBONES)return false;
    if(pose&&pose->size()!=std::size_t(header->numbones))return false;
    auto *bones=reinterpret_cast<oc2_preview_bone_t *>(reinterpret_cast<byte *>(header)+header->boneindex);
    out.resize(header->numbones);
    for(int i=0;i<header->numbones;++i)
    {
        const auto &bone=bones[i];if(bone.parent>=i||bone.parent< -1)return false;
        const float *value=pose?(*pose)[i].data():bone.value;
        const float x=value[3],y=value[4],z=value[5];
        const float sx=std::sin(x),cx=std::cos(x),sy=std::sin(y),cy=std::cos(y),sz=std::sin(z),cz=std::cos(z);
        Matrix local;
        local.m[0][0]=cy*cz;local.m[0][1]=sx*sy*cz-cx*sz;local.m[0][2]=cx*sy*cz+sx*sz;
        local.m[1][0]=cy*sz;local.m[1][1]=sx*sy*sz+cx*cz;local.m[1][2]=cx*sy*sz-sx*cz;
        local.m[2][0]=-sy;local.m[2][1]=sx*cy;local.m[2][2]=cx*cy;
        for(int axis=0;axis<3;++axis)local.m[axis][3]=value[axis];
        if(bone.parent>=0)
        {
            Matrix parent;std::memcpy(parent.m,out[bone.parent].m,sizeof(parent.m));local=Product(parent,local);
        }
        std::memcpy(out[i].m,local.m,sizeof(local.m));
    }
    return true;
}
bool HeaderRange(const studiohdr_t *header,int offset,int count,std::size_t stride)
{
    return header&&header->length>=int(sizeof(studiohdr_t))&&offset>=0&&count>=0&&offset<=header->length&&
        std::size_t(count)<=std::size_t(header->length-offset)/stride;
}
bool KnifeIdlePose(studiohdr_t *header,BoneValues &pose,int &selected)
{
    if(!header||header->numbones<1||header->numbones>MAXSTUDIOBONES)return false;
    if(!HeaderRange(header,header->boneindex,header->numbones,sizeof(oc2_preview_bone_t))||
       !HeaderRange(header,header->seqindex,header->numseq,sizeof(mstudioseqdesc_t)))return false;
    const auto *sequences=reinterpret_cast<const mstudioseqdesc_t *>(reinterpret_cast<const byte *>(header)+header->seqindex);
    selected=-1;
    for(int i=0;i<header->numseq;++i)
        if(!strnicmp(sequences[i].label,"idle",4)){selected=i;break;}
    if(selected<0)return false;
    const auto &sequence=sequences[selected];
    if(sequence.seqgroup!=0||sequence.numframes<1||sequence.numblends<1||
       !HeaderRange(header,sequence.animindex,header->numbones,sizeof(oc2_preview_anim_t)))return false;
    const auto *bones=reinterpret_cast<const oc2_preview_bone_t *>(reinterpret_cast<const byte *>(header)+header->boneindex);
    const auto *animation=reinterpret_cast<const oc2_preview_anim_t *>(reinterpret_cast<const byte *>(header)+sequence.animindex);
    pose.resize(header->numbones);
    // Sample the same first idle frame as the actual knife viewmodel. Several
    // folding knives store their blade closed in the reference bone values.
    for(int bone=0;bone<header->numbones;++bone)for(int axis=0;axis<6;++axis)
    {
        float value=bones[bone].value[axis];
        if(animation[bone].offset[axis])
        {
            const std::size_t offset=std::size_t(sequence.animindex)+bone*sizeof(oc2_preview_anim_t)+animation[bone].offset[axis];
            if(offset>std::size_t(header->length)-sizeof(mstudioanimvalue_t))return false;
            const auto *span=reinterpret_cast<const mstudioanimvalue_t *>(reinterpret_cast<const byte *>(header)+offset);
            if(!span->num.total||span->num.valid>span->num.total)return false;
            const int sample=span->num.valid?1:0;
            if(offset+std::size_t(sample+1)*sizeof(mstudioanimvalue_t)>std::size_t(header->length))return false;
            value+=span[sample].value*bones[bone].scale[axis];
        }
        if(!std::isfinite(value))return false;
        pose[bone][axis]=value;
    }
    return true;
}
struct Geometry
{
    struct CachedBounds
    {
        struct Hand {float axes[3][3]{},center[3]{},half[3]{},target[3]{};};
        bool attempted=false,valid=false,framed=false;
        float mins[3]{},maxs[3]{},center[3]{},half[3]{};
        float handSplit=0,handAxis[3]{};
        Hand hands[2];
        float axes[3][3]{}; // horizontal, depth, vertical
        std::vector<std::array<float,3>> points;
    };
    studiohdr_t *header=nullptr;
    geo::StudioTriangleCache mesh;
    std::vector<geo::BindMatrix> bind;
    BoneValues knifePose;
    int knifeSequence=-1;
    geo::MountCache mounts;
    mutable std::map<std::pair<int,bool>,CachedBounds> bounds;
    mutable std::map<std::pair<int,int>,CachedBounds> regionBounds;
    bool xm1014=false;
    bool valid=false;
};
std::map<std::string,std::unique_ptr<Geometry>> geometries;
Geometry *Mesh(const std::string &path,cl_entity_t &entity,bool knife=false)
{
    EngFuncs::SetModel(&entity,path.c_str());if(!entity.model||entity.model->type!=mod_studio)return nullptr;
    PreviewAPI *api=API();if(!api)return nullptr;
    auto *header=static_cast<studiohdr_t *>(api->StudioHeader(&entity));
    if(!header)return nullptr;
    // The same MDL may also be used outside a knife thumbnail. Its reference
    // cache and its sampled idle pose must never share bounds or mount data.
    auto &entry=geometries[knife?path+"#knife_idle":path];
    if(!entry||entry->header!=header)
    {
        entry.reset(new Geometry);entry->header=header;
        entry->xm1014=path=="models/v_xm1014.mdl"&&!knife;
        entry->valid=geo::DecodeStudioTriangles(header,entry->mesh)&&
            (!knife||KnifeIdlePose(header,entry->knifePose,entry->knifeSequence))&&
            Bind(header,entry->bind,knife?&entry->knifePose:nullptr);
        if(entry->valid)geo::BuildMountCache(header,entry->mounts);
    }
    return entry->valid?entry.get():nullptr;
}
const char *WeaponModel(int weapon)
{
    static const char *const names[24]={"p228","scout","xm1014","mac10","aug","elite","fiveseven","ump45","sg550","galil","famas","usp","glock18","awp","mp7","m249","m3","m4a1","tmp","g3sg1","deagle","sg552","ak47","p90"};
    static std::string path;const int index=oc2_accessories::Index(weapon);
    path=index<0?"models/v_knife.mdl":std::string("models/v_")+names[index]+".mdl";return path.c_str();
}
void Initialize(cl_entity_t &entity,int number)
{
    std::memset(&entity,0,sizeof(entity));entity.curstate.number=number;entity.curstate.scale=1;
    entity.curstate.rendermode=kRenderNormal;entity.curstate.renderamt=255;
    entity.curstate.rendercolor.r=entity.curstate.rendercolor.g=entity.curstate.rendercolor.b=255;
    entity.curstate.effects=EF_FULLBRIGHT|EF_NOINTERP;
    for(int i=0;i<4;++i)entity.curstate.controller[i]=entity.latched.prevcontroller[i]=127;
}
bool Active(const geo::StudioTriangleCache &mesh,const geo::StudioTriangle &triangle,int body)
{
    if(triangle.part<0||std::size_t(triangle.part)>=mesh.parts.size())return false;
    const auto &part=mesh.parts[triangle.part];return part.base>0&&part.models>0&&((body/part.base)%part.models)==triangle.model;
}
const mstudiotexture_t *Materials(studiohdr_t *header)
{return reinterpret_cast<const mstudiotexture_t *>(reinterpret_cast<const byte *>(header)+header->textureindex);}
int Material(studiohdr_t *header,int index)
{
    if(index<0||index>=header->numskinref)return -1;
    const auto *refs=reinterpret_cast<const short *>(reinterpret_cast<const byte *>(header)+header->skinindex);
    const int material=refs[index];return material>=0&&material<header->numtextures?material:-1;
}
// The XM reference pose contains four reload shells, including their separate
// chrome overlay. Keep this limited to the identified preview model and meshes.
bool PreviewShellMesh(const Geometry &geometry,int partIndex,int modelIndex,int skinref)
{
    auto *header=geometry.header;
    if(!geometry.xm1014||partIndex<0||partIndex>=header->numbodyparts)return false;
    const auto *parts=reinterpret_cast<const mstudiobodyparts_t *>(reinterpret_cast<const byte *>(header)+header->bodypartindex);
    const auto &part=parts[partIndex];
    if(modelIndex<0||modelIndex>=part.nummodels)return false;
    const auto *models=reinterpret_cast<const mstudiomodel_t *>(reinterpret_cast<const byte *>(header)+part.modelindex);
    const auto &model=models[modelIndex];
    const int material=Material(header,skinref);if(material<0)return false;
    const char *name=Materials(header)[material].name;
    return (!std::strncmp(part.name,"studio",sizeof(part.name))&&
            !std::strncmp(model.name,"v_xm1014",sizeof(model.name))&&
            !std::strncmp(name,"shell_shotgun.bmp",sizeof(Materials(header)[material].name)))||
           (!std::strncmp(part.name,"oc2_chrome_04",sizeof(part.name))&&
            !std::strncmp(model.name,"oc2_chrome_xm1014_03",sizeof(model.name))&&
            !std::strncmp(name,"chrom.bmp",sizeof(Materials(header)[material].name)));
}
bool Bounds(const Geometry &geometry,int body,bool gloves,float mins[3],float maxs[3])
{
    auto &cached=geometry.bounds[std::make_pair(body,gloves)];
    if(cached.attempted)
    {
        std::memcpy(mins,cached.mins,sizeof(cached.mins));std::memcpy(maxs,cached.maxs,sizeof(cached.maxs));
        return cached.valid;
    }
    cached.attempted=true;
    for(int axis=0;axis<3;++axis){mins[axis]=1e20f;maxs[axis]=-1e20f;}
    const auto *materials=Materials(geometry.header);bool found=false;
    for(const auto &triangle:geometry.mesh.triangles)
    {
        if(!Active(geometry.mesh,triangle,body)||triangle.placeholder||
           PreviewShellMesh(geometry,triangle.part,triangle.model,triangle.material))continue;
        const int material=Material(geometry.header,triangle.material);
        if(material<0||HiddenMaterial(materials[material].name,gloves))continue;
        for(const auto &corner:triangle.corner)
        {
            float point[3];Transform(geometry.bind[corner.bone].m,corner.p,point);
            cached.points.push_back({point[0],point[1],point[2]});
            for(int axis=0;axis<3;++axis){mins[axis]=std::min(mins[axis],point[axis]);maxs[axis]=std::max(maxs[axis],point[axis]);}
            found=true;
        }
    }
    cached.valid=found;std::memcpy(cached.mins,mins,sizeof(cached.mins));std::memcpy(cached.maxs,maxs,sizeof(cached.maxs));
    return found;
}
bool RegionBounds(const Geometry &geometry,int body,int region,float mins[3],float maxs[3])
{
    if(region<0||std::size_t(region)>=geometry.mounts.regions.size())return false;
    auto &cached=geometry.regionBounds[std::make_pair(body,region)];
    if(cached.attempted)
    {
        std::memcpy(mins,cached.mins,sizeof(cached.mins));std::memcpy(maxs,cached.maxs,sizeof(cached.maxs));
        return cached.valid;
    }
    cached.attempted=true;
    const auto &bones=geometry.mounts.regions[region].weaponBones;
    const auto *materials=Materials(geometry.header);bool found=false;
    for(int a=0;a<3;++a){mins[a]=1e20f;maxs[a]=-1e20f;}
    for(const auto &triangle:geometry.mesh.triangles)
    {
        if(!Active(geometry.mesh,triangle,body)||triangle.placeholder)continue;
        const int material=Material(geometry.header,triangle.material);
        if(material<0||HiddenMaterial(materials[material].name,false))continue;
        for(const auto &corner:triangle.corner)
        {
            if(std::find(bones.begin(),bones.end(),corner.bone)==bones.end())continue;
            float point[3];Transform(geometry.bind[corner.bone].m,corner.p,point);
            for(int a=0;a<3;++a){mins[a]=std::min(mins[a],point[a]);maxs[a]=std::max(maxs[a],point[a]);}
            found=true;
        }
    }
    cached.valid=found;std::memcpy(cached.mins,mins,sizeof(cached.mins));std::memcpy(cached.maxs,maxs,sizeof(cached.maxs));
    return found;
}
void Cross(const float *a,const float *b,float *out)
{out[0]=a[1]*b[2]-a[2]*b[1];out[1]=a[2]*b[0]-a[0]*b[2];out[2]=a[0]*b[1]-a[1]*b[0];}
void PrincipalAxes(const std::vector<std::array<float,3>> &points,float axes[3][3])
{
    double mean[3]{},covariance[3][3]{},vectors[3][3]{{1,0,0},{0,1,0},{0,0,1}};
    for(const auto &point:points)for(int a=0;a<3;++a)mean[a]+=point[a]/double(points.size());
    for(const auto &point:points)for(int a=0;a<3;++a)for(int b=0;b<3;++b)
        covariance[a][b]+=(point[a]-mean[a])*(point[b]-mean[b]);
    for(int iteration=0;iteration<24;++iteration)
    {
        int p=0,q=1;
        for(int a=0;a<3;++a)for(int b=a+1;b<3;++b)
            if(std::fabs(covariance[a][b])>std::fabs(covariance[p][q])){p=a;q=b;}
        if(std::fabs(covariance[p][q])<1e-8)break;
        const double angle=.5*std::atan2(2*covariance[p][q],covariance[q][q]-covariance[p][p]);
        const double c=std::cos(angle),s=std::sin(angle),pp=covariance[p][p],qq=covariance[q][q],pq=covariance[p][q];
        for(int k=0;k<3;++k)if(k!=p&&k!=q)
        {
            const double kp=covariance[k][p],kq=covariance[k][q];
            covariance[k][p]=covariance[p][k]=c*kp-s*kq;
            covariance[k][q]=covariance[q][k]=s*kp+c*kq;
        }
        covariance[p][p]=c*c*pp-2*s*c*pq+s*s*qq;
        covariance[q][q]=s*s*pp+2*s*c*pq+c*c*qq;covariance[p][q]=covariance[q][p]=0;
        for(int k=0;k<3;++k)
        {const double kp=vectors[k][p],kq=vectors[k][q];vectors[k][p]=c*kp-s*kq;vectors[k][q]=s*kp+c*kq;}
    }
    int longest=0,thinnest=0;
    for(int a=1;a<3;++a)
    {if(covariance[a][a]>covariance[longest][longest])longest=a;if(covariance[a][a]<covariance[thinnest][thinnest])thinnest=a;}
    for(int a=0;a<3;++a){axes[0][a]=float(vectors[a][longest]);axes[1][a]=float(vectors[a][thinnest]);}
    Cross(axes[1],axes[0],axes[2]);
}
void FrameExtents(const std::vector<std::array<float,3>> &points,const float axes[3][3],float *center,float *half)
{
    for(int a=0;a<3;++a)
    {
        float minimum=1e20f,maximum=-1e20f;
        for(const auto &point:points){const float value=Dot(point.data(),axes[a]);minimum=std::min(minimum,value);maximum=std::max(maximum,value);}
        half[a]=(maximum-minimum)*.5f;
        for(int b=0;b<3;++b)center[b]+=axes[a][b]*(minimum+maximum)*.5f;
    }
}
int HandSide(const Geometry::CachedBounds &frame,const float *point)
{return Dot(point,frame.handAxis)<frame.handSplit?0:1;}
void PresentHand(const Geometry::CachedBounds::Hand &hand,int side,const float *point,float *out,bool position)
{
    float relative[3];for(int a=0;a<3;++a)relative[a]=point[a]-(position?hand.center[a]:0);
    const float sign=side?1.f:-1.f;
    out[0]=sign*Dot(relative,hand.axes[0]);out[1]=-Dot(relative,hand.axes[1]);out[2]=sign*Dot(relative,hand.axes[2]);
    if(position)for(int a=0;a<3;++a)out[a]+=hand.target[a];
}
int FindBone(const Geometry &geometry,const char *suffix)
{
    const auto *bones=reinterpret_cast<const oc2_preview_bone_t *>(reinterpret_cast<const byte *>(geometry.header)+geometry.header->boneindex);
    const std::size_t size=std::strlen(suffix);
    for(int n=0;n<geometry.header->numbones;++n)
    {
        const std::size_t length=std::strlen(bones[n].name);
        if(length>=size&&!stricmp(bones[n].name+length-size,suffix))return n;
    }
    return -1;
}
const Geometry::CachedBounds &PresentationFrame(const Geometry &geometry,int body,bool gloves,int definition)
{
    auto &frame=geometry.bounds[std::make_pair(body,gloves)];if(frame.framed)return frame;frame.framed=true;
    PrincipalAxes(frame.points,frame.axes);
    // Stable signs keep reset and small thumbnails facing the same side.
    int signAxis=0;for(int a=1;a<3;++a)if(std::fabs(frame.axes[0][a])>std::fabs(frame.axes[0][signAxis]))signAxis=a;
    if(frame.axes[0][signAxis]<0)for(float &v:frame.axes[0])v=-v;
    signAxis=0;for(int a=1;a<3;++a)if(std::fabs(frame.axes[1][a])>std::fabs(frame.axes[1][signAxis]))signAxis=a;
    if(frame.axes[1][signAxis]>0)for(float &v:frame.axes[1])v=-v;
    // Knives face the opposite broad side with the same handle-to-tip axis.
    // Reversing depth also reverses up, keeping the spine above the edge
    // without the left/right reversal caused by a 180-degree image roll.
    if(!gloves)for(float &v:frame.axes[1])v=-v;
    if(!gloves&&definition==522)
    {
        // The Stiletto's handle dominates the whole-model covariance. Its
        // blade lies at the positive end of the signed longitudinal axis.
        float minimum=1e20f,maximum=-1e20f;
        for(const auto &point:frame.points)
        {const float value=Dot(point.data(),frame.axes[0]);minimum=std::min(minimum,value);maximum=std::max(maximum,value);}
        std::vector<std::array<float,3>> blade;
        for(const auto &point:frame.points)
            if(Dot(point.data(),frame.axes[0])>minimum+(maximum-minimum)*.6f)blade.push_back(point);
        if(blade.size()>=3)
        {
            float bladeAxes[3][3];PrincipalAxes(blade,bladeAxes);
            const float along=Dot(bladeAxes[1],frame.axes[0]);
            for(int a=0;a<3;++a)frame.axes[1][a]=bladeAxes[1][a]-along*frame.axes[0][a];
            Normalize(frame.axes[1]);
            int dominant=0;for(int a=1;a<3;++a)if(std::fabs(frame.axes[1][a])>std::fabs(frame.axes[1][dominant]))dominant=a;
            if(frame.axes[1][dominant]<0)for(float &v:frame.axes[1])v=-v;
        }
    }
    Cross(frame.axes[1],frame.axes[0],frame.axes[2]);
    if(gloves)
    {
        std::memcpy(frame.handAxis,frame.axes[0],sizeof(frame.handAxis));
        frame.handSplit=float(Dot(frame.axes[0],frame.mins)+Dot(frame.axes[0],frame.maxs))*.5f;
        std::vector<std::array<float,3>> hands[2];
        for(const auto &point:frame.points)
            hands[HandSide(frame,point.data())].push_back(point);
        for(int side=0;side<2;++side)PrincipalAxes(hands[side],frame.hands[side].axes);
        for(int left=0;left<2;++left)
        {
            const int wrist=FindBone(geometry,left?"_L_Hand":"_R_Hand");
            const int index=FindBone(geometry,left?"_L_Finger1":"_R_Finger1");
            const int middle=FindBone(geometry,left?"_L_Finger2":"_R_Finger2");
            const int pinky=FindBone(geometry,left?"_L_Finger4":"_R_Finger4");
            if(wrist<0||index<0||middle<0||pinky<0)continue;
            float position[3],finger[3],first[3],last[3],back[3];
            for(int a=0;a<3;++a)
            {
                position[a]=geometry.bind[wrist].m[a][3];
                finger[a]=geometry.bind[middle].m[a][3]-position[a];
                first[a]=geometry.bind[index].m[a][3]-position[a];
                last[a]=geometry.bind[pinky].m[a][3]-position[a];
            }
            auto &hand=frame.hands[HandSide(frame,position)];
            if(Dot(hand.axes[0],finger)<0)for(float &v:hand.axes[0])v=-v;
            Cross(first,last,back);if(!left)for(float &v:back)v=-v;
            const float along=Dot(back,hand.axes[0]);
            for(int a=0;a<3;++a)hand.axes[1][a]=back[a]-along*hand.axes[0][a];
            Normalize(hand.axes[1]);Cross(hand.axes[1],hand.axes[0],hand.axes[2]);
        }
        for(int side=0;side<2;++side)
        {
            auto &hand=frame.hands[side];FrameExtents(hands[side],hand.axes,hand.center,hand.half);
            hand.target[0]=(side?1.f:-1.f)*(hand.half[0]+1.f);
        }
        // Each hand has its own horizontal finger axis and anatomical dorsal
        // normal. This avoids the original V-shaped pair and palm-side flips.
        for(auto &point:frame.points)
        {
            const int side=HandSide(frame,point.data());float presented[3];PresentHand(frame.hands[side],side,point.data(),presented,true);
            std::copy(presented,presented+3,point.begin());
        }
        std::memset(frame.axes,0,sizeof(frame.axes));frame.axes[0][0]=frame.axes[2][2]=1;frame.axes[1][1]=-1;
    }
    FrameExtents(frame.points,frame.axes,frame.center,frame.half);
    return frame;
}
// Only the duration of RenderScene sees a frozen presentation pose. This is the same
// paired save/restore discipline as client-side inventory material overrides.
// The frozen bones are also those used by the shared mount resolver, so the
// full-side saved attachment coordinates stay on the actual visible gun mesh.
class DrawOverride
{
    struct SavedInt {int *address;int value;};
    struct SavedShort {short *address;short value;};
    struct SavedFloat {float *address;float value;};
    std::vector<SavedInt> saved;
    std::vector<SavedShort> savedCommands;
    std::vector<SavedFloat> savedVertices;
    int materialSwaps=0;
    studiohdr_t *header=nullptr;
    mstudioseqdesc_t *sequence=nullptr,original{};
    byte *animation=nullptr;std::vector<byte> originalAnimation;
public:
    void Use(studiohdr_t *value){header=value;}
    void ReplaceBody(int body,int texture)
    {
        if(!header||!texture||header->numbodyparts<1)return;
        auto *parts=reinterpret_cast<mstudiobodyparts_t *>(reinterpret_cast<byte *>(header)+header->bodypartindex);
        const auto &part=parts[0];
        if(part.base<=0||part.nummodels<1)return;
        auto *models=reinterpret_cast<mstudiomodel_t *>(reinterpret_cast<byte *>(header)+part.modelindex);
        const auto &model=models[(body/part.base)%part.nummodels];
        if(model.nummesh<1)return;
        const auto *meshes=reinterpret_cast<const mstudiomesh_t *>(reinterpret_cast<const byte *>(header)+model.meshindex);
        const int material=Material(header,meshes[0].skinref);
        if(material>=0)Replace(Materials(header)[material].name,texture);
    }
    bool Freeze(Geometry &geometry,int body,bool gloves)
    {
        header=geometry.header;if(header->numseq<1)return false;
        sequence=reinterpret_cast<mstudioseqdesc_t *>(reinterpret_cast<byte *>(header)+header->seqindex);
        if(sequence->seqgroup!=0||sequence->animindex<0||
            sequence->animindex>header->length-header->numbones*int(sizeof(oc2_preview_anim_t)))return false;
        original=*sequence;animation=reinterpret_cast<byte *>(header)+sequence->animindex;
        const int size=header->numbones*int(sizeof(oc2_preview_anim_t));originalAnimation.assign(animation,animation+size);
        std::memset(animation,0,std::size_t(size));sequence->fps=0;sequence->numframes=1;sequence->numblends=1;
        sequence->numevents=0;sequence->motiontype=0;
        if(!geometry.knifePose.empty())
        {
            auto *bones=reinterpret_cast<oc2_preview_bone_t *>(reinterpret_cast<byte *>(header)+header->boneindex);
            for(int bone=0;bone<header->numbones;++bone)for(int axis=0;axis<6;++axis)
            {
                SavedFloat entry;entry.address=&bones[bone].value[axis];entry.value=bones[bone].value[axis];savedVertices.push_back(entry);
                bones[bone].value[axis]=geometry.knifePose[bone][axis];
            }
        }
        float mins[3],maxs[3];if(Bounds(geometry,body,gloves,mins,maxs))
            for(int axis=0;axis<3;++axis){sequence->bbmin[axis]=mins[axis];sequence->bbmax[axis]=maxs[axis];}
        auto *parts=reinterpret_cast<mstudiobodyparts_t *>(reinterpret_cast<byte *>(header)+header->bodypartindex);
        const auto *materials=Materials(header);
        for(int part=0;part<header->numbodyparts;++part)
        {
            auto *models=reinterpret_cast<mstudiomodel_t *>(reinterpret_cast<byte *>(header)+parts[part].modelindex);
            for(int model=0;model<parts[part].nummodels;++model)
            {
                auto *meshes=reinterpret_cast<mstudiomesh_t *>(reinterpret_cast<byte *>(header)+models[model].meshindex);
                for(int mesh=0;mesh<models[model].nummesh;++mesh)
                {
                    const int material=Material(header,meshes[mesh].skinref);
                    if(material>=0&&(HiddenMaterial(materials[material].name,gloves)||
                        PreviewShellMesh(geometry,part,model,meshes[mesh].skinref)))
                    {
                        Set(meshes[mesh].numtris,0);
                        auto *command=reinterpret_cast<short *>(reinterpret_cast<byte *>(header)+meshes[mesh].triindex);
                        SavedShort entry;entry.address=command;entry.value=*command;savedCommands.push_back(entry);*command=0;
                    }
                }
            }
        }
        return true;
    }
    void Set(int &value,int replacement){SavedInt entry;entry.address=&value;entry.value=value;saved.push_back(entry);value=replacement;}
    void Replace(const std::string &material,int texture,bool masked=false)
    {
        if(!header||!texture||material.empty())return;
        auto *materials=reinterpret_cast<mstudiotexture_t *>(reinterpret_cast<byte *>(header)+header->textureindex);
        for(int i=0;i<header->numtextures;++i)if(MaterialMatches(material,materials[i].name))
        {Set(materials[i].index,texture);++materialSwaps;if(masked)Set(materials[i].flags,materials[i].flags|0x0040);}
    }
    int SwapCount() const{return materialSwaps;}
    void HideOtherRegion(const Geometry &geometry,int body,int region)
    {
        if(!header||region<0||std::size_t(region)>=geometry.mounts.regions.size())return;
        const auto &keep=geometry.mounts.regions[region].weaponBones;
        const auto *parts=reinterpret_cast<const mstudiobodyparts_t *>(reinterpret_cast<const byte *>(header)+header->bodypartindex);
        for(int p=0;p<header->numbodyparts;++p)
        {
            if(parts[p].base<=0||parts[p].nummodels<=0)continue;
            const auto *models=reinterpret_cast<const mstudiomodel_t *>(reinterpret_cast<const byte *>(header)+parts[p].modelindex);
            const auto &model=models[(body/parts[p].base)%parts[p].nummodels];
            auto *vertices=reinterpret_cast<float (*)[3]>(reinterpret_cast<byte *>(header)+model.vertindex);
            const auto *bones=reinterpret_cast<const byte *>(header)+model.vertinfoindex;
            for(int v=0;v<model.numverts;++v)
            {
                const int bone=bones[v];bool other=false;
                if(std::find(keep.begin(),keep.end(),bone)!=keep.end())continue;
                for(std::size_t r=0;r<geometry.mounts.regions.size();++r)if(int(r)!=region)
                {
                    const auto &list=geometry.mounts.regions[r].weaponBones;
                    if(std::find(list.begin(),list.end(),bone)!=list.end()){other=true;break;}
                }
                if(!other)continue;
                // Collapse the other pistol to one point outside the view. The
                // original vertex data is restored immediately after rendering.
                const auto &bind=geometry.bind[bone];const float target[3]={10000,10000,10000};
                for(int a=0;a<3;++a)
                {
                    SavedFloat entry;entry.address=&vertices[v][a];entry.value=vertices[v][a];savedVertices.push_back(entry);
                    vertices[v][a]=0;
                    for(int k=0;k<3;++k)vertices[v][a]+=bind.m[k][a]*(target[k]-bind.m[k][3]);
                }
            }
        }
    }
    void PresentGloves(const Geometry &geometry,int body,const Geometry::CachedBounds &frame)
    {
        if(!header)return;
        const auto *parts=reinterpret_cast<const mstudiobodyparts_t *>(reinterpret_cast<const byte *>(header)+header->bodypartindex);
        for(int p=0;p<header->numbodyparts;++p)
        {
            const auto *models=reinterpret_cast<const mstudiomodel_t *>(reinterpret_cast<const byte *>(header)+parts[p].modelindex);
            const auto &model=models[(body/parts[p].base)%parts[p].nummodels];
            auto *vertices=reinterpret_cast<float (*)[3]>(reinterpret_cast<byte *>(header)+model.vertindex);
            const auto *bones=reinterpret_cast<const byte *>(header)+model.vertinfoindex;
            std::vector<int> vertexSides(model.numverts),normalSides(model.numnorms,-1);
            for(int v=0;v<model.numverts;++v)
            {
                const auto &bind=geometry.bind[bones[v]];float world[3],presented[3];Transform(bind.m,vertices[v],world);
                const int side=vertexSides[v]=HandSide(frame,world);PresentHand(frame.hands[side],side,world,presented,true);
                for(int axis=0;axis<3;++axis)
                {
                    SavedFloat entry;entry.address=&vertices[v][axis];entry.value=vertices[v][axis];savedVertices.push_back(entry);
                    vertices[v][axis]=0;
                    for(int a=0;a<3;++a)vertices[v][axis]+=bind.m[a][axis]*(presented[a]-bind.m[a][3]);
                }
            }
            const auto *meshes=reinterpret_cast<const mstudiomesh_t *>(reinterpret_cast<const byte *>(header)+model.meshindex);
            for(int mesh=0;mesh<model.nummesh;++mesh)
            {
                const short *command=reinterpret_cast<const short *>(reinterpret_cast<const byte *>(header)+meshes[mesh].triindex);
                while(*command)
                {
                    const int count=std::abs(int(*command++));
                    for(int i=0;i<count;++i,command+=4)normalSides[command[1]]=vertexSides[command[0]];
                }
            }
            auto *normals=reinterpret_cast<float (*)[3]>(reinterpret_cast<byte *>(header)+model.normindex);
            const auto *normalBones=reinterpret_cast<const byte *>(header)+model.norminfoindex;
            for(int n=0;n<model.numnorms;++n)if(normalSides[n]>=0)
            {
                const auto &bind=geometry.bind[normalBones[n]];float world[3],presented[3];Transform(bind.m,normals[n],world,false);
                const int side=normalSides[n];PresentHand(frame.hands[side],side,world,presented,false);
                for(int axis=0;axis<3;++axis)
                {
                    SavedFloat entry;entry.address=&normals[n][axis];entry.value=normals[n][axis];savedVertices.push_back(entry);
                    normals[n][axis]=0;for(int a=0;a<3;++a)normals[n][axis]+=bind.m[a][axis]*presented[a];
                }
            }
        }
        if(sequence)for(int axis=0;axis<3;++axis)
        {
            float extent=0;for(int a=0;a<3;++a)extent+=std::fabs(frame.axes[a][axis])*frame.half[a];
            sequence->bbmin[axis]=frame.center[axis]-extent;sequence->bbmax[axis]=frame.center[axis]+extent;
        }
    }
    void Scale(float scale)
    {
        if(!header||scale==1)return;
        auto *parts=reinterpret_cast<mstudiobodyparts_t *>(reinterpret_cast<byte *>(header)+header->bodypartindex);
        for(int part=0;part<header->numbodyparts;++part)
        {
            auto *models=reinterpret_cast<mstudiomodel_t *>(reinterpret_cast<byte *>(header)+parts[part].modelindex);
            for(int model=0;model<parts[part].nummodels;++model)
            {
                auto *vertices=reinterpret_cast<float *>(reinterpret_cast<byte *>(header)+models[model].vertindex);
                for(int i=0;i<models[model].numverts*3;++i)
                {SavedFloat entry;entry.address=&vertices[i];entry.value=vertices[i];savedVertices.push_back(entry);vertices[i]*=scale;}
            }
        }
        if(sequence)for(int axis=0;axis<3;++axis){sequence->bbmin[axis]*=scale;sequence->bbmax[axis]*=scale;}
    }
    ~DrawOverride()
    {
        for(auto i=savedVertices.rbegin();i!=savedVertices.rend();++i)*i->address=i->value;
        for(auto i=savedCommands.rbegin();i!=savedCommands.rend();++i)*i->address=i->value;
        for(auto i=saved.rbegin();i!=saved.rend();++i)*i->address=i->value;
        if(sequence&&animation){std::memcpy(animation,originalAnimation.data(),originalAnimation.size());*sequence=original;}
    }
};
struct EquippedAppearance
{
    DrawOverride weapon,glove;
};
bool Attachment(const Geometry &weapon,const oc2_accessories::WeaponConfig &config,bool charm,
    cl_entity_t &entity,Geometry *&geometry)
{
    geo::Mount mount;
    if(!weapon.mounts.valid||!geo::FindMount(weapon.mounts,charm?config.charmU:config.statU,charm?config.charmV:config.statV,mount))return false;
    const char *path=charm?(config.weapon==20?"models/attachments/oc2_sticker_charm_m249.mdl":"models/attachments/oc2_sticker_charm.mdl"):"models/attachments/oc2_stattrak.mdl";
    Initialize(entity,charm?3:2);geometry=Mesh(path,entity);if(!geometry)return false;
    float frame[3][4],origin[3],x[3],y[3],normal[3];const auto &bone=weapon.bind[mount.bone];
    Transform(bone.m,mount.origin,origin);Transform(bone.m,mount.axisX,x,false);
    Transform(bone.m,mount.axisY,y,false);Transform(bone.m,mount.normal,normal,false);
    Normalize(x);Normalize(y);Normalize(normal);
    for(int i=0;i<3;++i)
    {
        frame[i][0]=x[i];frame[i][1]=y[i];frame[i][2]=normal[i];frame[i][3]=origin[i]+normal[i]*.01f;
        entity.origin[i]=entity.curstate.origin[i]=frame[i][3];
    }
    API()->SetTransform(&entity,frame);
    if(!charm){const int index=oc2_accessories::Index(config.weapon);if(index>=0)entity.curstate.body=int(resources.counts.values[index]);}
    return true;
}
}
int OC2_PreviewEquippedGloveBody(cl_entity_t &glove)
{
    resources.Load();
    const auto *entry=oc2_gloves::FindEntry(resources.gloves,resources.gloveLoadout.resource);
    auto *api=API();if(!entry||!api)return -1;
    auto *header=static_cast<studiohdr_t *>(api->StudioHeader(&glove));
    if(!header||header->numbodyparts<1)return -1;
    const auto *parts=reinterpret_cast<const mstudiobodyparts_t *>(reinterpret_cast<const byte *>(header)+header->bodypartindex);
    const auto *models=reinterpret_cast<const mstudiomodel_t *>(reinterpret_cast<const byte *>(header)+parts[0].modelindex);
    const auto *materials=Materials(header);
    for(int body=0;body<parts[0].nummodels;++body)
    {
        const auto *meshes=reinterpret_cast<const mstudiomesh_t *>(reinterpret_cast<const byte *>(header)+models[body].meshindex);
        bool left=false,right=false;
        for(int mesh=0;mesh<models[body].nummesh;++mesh)
        {
            const int material=Material(header,meshes[mesh].skinref);if(material<0)continue;
            left=left||MaterialMatches(entry->leftMaterial,materials[material].name);
            right=right||MaterialMatches(entry->rightMaterial,materials[material].name);
        }
        if(left&&right)return body*parts[0].base;
    }
    return -1;
}
std::uint64_t OC2_PreviewAppearanceSignature(int)
{resources.Load();return resources.signature;}
OC2_PreviewAppearance::OC2_PreviewAppearance(cl_entity_t &weapon,cl_entity_t *glove,int weaponId):m_state(new EquippedAppearance)
{
    resources.Load();auto &state=*static_cast<EquippedAppearance *>(m_state);auto *api=API();if(!api)return;
    state.weapon.Use(static_cast<studiohdr_t *>(api->StudioHeader(&weapon)));
    if(const auto *equipped=oc2_inventory::Find(resources.loadout,weaponId))
        if(const auto *skin=oc2_inventory::FindEntry(resources.skins,equipped->skin))
            if(skin->kind==oc2_inventory::Kind::GunSkin&&skin->weapon==weaponId)
                state.weapon.ReplaceBody(weapon.curstate.body,Texture(skin->file));
    if(glove)
    {
        state.glove.Use(static_cast<studiohdr_t *>(api->StudioHeader(glove)));
        if(const auto *entry=oc2_gloves::FindEntry(resources.gloves,resources.gloveLoadout.resource))
        {
            state.glove.Replace(entry->leftMaterial,Texture(entry->leftTexture));
            state.glove.Replace(entry->rightMaterial,Texture(entry->rightTexture));
        }
    }
}
OC2_PreviewAppearance::~OC2_PreviewAppearance(){delete static_cast<EquippedAppearance *>(m_state);}
int OC2_PreviewAppearance::Swaps() const
{const auto &state=*static_cast<const EquippedAppearance *>(m_state);return state.weapon.SwapCount()+state.glove.SwapCount();}

struct CMenuPanoramaModelView::Scene
{
    int mode=1,team=2,weapon=28,definition=0,skin=0,glove=0,accessoryRegion=-1;
    oc2_inventory::WeaponState appearance;
    oc2_accessories::WeaponConfig accessories;
    bool interactive=true,dragging=false,rendering=false,dualUnfold=false;
    float yaw=0,pitch=0,zoom=1;
    int x=0,y=0,width=0,height=0,lastX=0,lastY=0;
    std::string lastModel;
    int lastModelType=-1,lastMaterialSwaps=0,lastAccessories=0;
    bool lastDrawSucceeded=false;
    Geometry *geometry=nullptr;
    float camera[3]{},direction[3]{},right[3]{},up[3]{},tanX=1,tanY=1;
    float normalizedScale=0;
    float contentWidth=0,contentHeight=0;
    bool projectionValid=false,overridePreview=false;
    std::string previewPattern;
    unsigned previewCount=0;
    bool attachmentValid[2]{};
    float attachmentRect[2][4]{};
    bool Project(const float world[3],float &sx,float &sy,float *scale=nullptr) const
    {
        float delta[3];for(int i=0;i<3;++i)delta[i]=world[i]-camera[i];
        const float depth=-Dot(delta,direction);if(depth<=.01f)return false;
        sx=x+width*(.5f+.5f*Dot(delta,right)/(depth*tanX));
        sy=y+height*(.5f-.5f*Dot(delta,up)/(depth*tanY));
        if(scale)*scale=height/(2*depth*tanY);return true;
    }
    cl_entity_t entity{},stat{},charm{};
    bool Hit() const{return uiStatic.cursorX>=x&&uiStatic.cursorY>=y&&uiStatic.cursorX<x+width&&uiStatic.cursorY<y+height;}
};
CMenuPanoramaModelView::CMenuPanoramaModelView():m_scene(new Scene)
{
    eFocusAnimation=QM_HIGHLIGHTIFFOCUS;
    SetWeapon(28);
}
CMenuPanoramaModelView::~CMenuPanoramaModelView(){delete m_dualView;delete m_scene;}
void CMenuPanoramaModelView::SetLobby(int team,int weaponId)
{m_scene->mode=0;m_scene->team=team==1?1:2;m_scene->weapon=weaponId;}
void CMenuPanoramaModelView::SetWeapon(int weaponId,int skinId)
{
    resources.Load();const bool changed=m_scene->mode!=1||m_scene->weapon!=weaponId;
    m_scene->mode=1;m_scene->weapon=weaponId;m_scene->skin=skinId;
    m_scene->appearance=oc2_inventory::WeaponState();m_scene->appearance.weapon=weaponId;
    if(const auto *state=oc2_inventory::Find(resources.loadout,weaponId))m_scene->appearance=*state;
    m_scene->appearance.skin=skinId;
    if(const auto *config=oc2_accessories::Find(resources.accessories,weaponId))m_scene->accessories=*config;
    if(changed)ResetView();
}
void CMenuPanoramaModelView::SetKnife(int definition,int skinId)
{
    resources.Load();const bool changed=m_scene->mode!=2||m_scene->definition!=definition;
    m_scene->mode=2;m_scene->weapon=29;m_scene->definition=definition;m_scene->skin=skinId;
    if(changed)ResetView();
}
void CMenuPanoramaModelView::SetGloves(int resourceId)
{
    resources.Load();const bool changed=m_scene->mode!=3||m_scene->glove!=resourceId;
    m_scene->mode=3;m_scene->glove=resourceId;if(changed)ResetView();
}
void CMenuPanoramaModelView::SetAppearance(const oc2_inventory::WeaponState &state,const oc2_accessories::WeaponConfig *config)
{
    if(m_scene->mode!=1||m_scene->weapon!=state.weapon)SetWeapon(state.weapon,state.skin);
    m_scene->appearance=state;m_scene->skin=state.skin;
    if(config){m_scene->accessories=*config;if(!config->charm)m_scene->appearance.charm=-1;}
}
void CMenuPanoramaModelView::SetAccessoryPreview(const std::string &pattern,unsigned count)
{m_scene->overridePreview=true;m_scene->previewPattern=pattern;m_scene->previewCount=count;}
void CMenuPanoramaModelView::SetAccessoryRegion(int region){m_scene->accessoryRegion=region;}
void CMenuPanoramaModelView::SetDualUnfold(bool enabled){m_scene->dualUnfold=enabled;}
bool CMenuPanoramaModelView::ContentExtent(float &widthFraction,float &heightFraction) const
{
    const Scene &scene=*m_scene;
    if(scene.width<=0||scene.height<=0||scene.contentWidth<=0||scene.contentHeight<=0)return false;
    widthFraction=scene.contentWidth/float(scene.width);
    heightFraction=scene.contentHeight/float(scene.height);
    return true;
}
bool CMenuPanoramaModelView::ProjectAccessory(int,float u,float v,float &x,float &y,float &scale) const
{
    const Scene &scene=*m_scene;if(!scene.projectionValid||!scene.geometry)return false;
    const int n=int(scene.geometry->mounts.regions.size());
    if(scene.accessoryRegion>=0&&n>0&&std::min(n-1,int(std::max(0.f,std::min(1.f,u))*n))!=scene.accessoryRegion)return false;
    geo::Mount mount;if(!geo::FindMount(scene.geometry->mounts,u,v,mount))return false;
    float point[3];Transform(scene.geometry->bind[mount.bone].m,mount.origin,point);
    return scene.Project(point,x,y,&scale);
}
bool CMenuPanoramaModelView::UnprojectAccessory(float x,float y,float &u,float &v) const
{
    const Scene &scene=*m_scene;if(!scene.projectionValid||!scene.geometry)return false;
    const auto &cache=scene.geometry->mounts;if(!cache.valid)return false;
    float ray[3];for(int a=0;a<3;++a)ray[a]=-scene.direction[a]+scene.right[a]*((x-scene.x)/scene.width*2-1)*scene.tanX+scene.up[a]*(1-(y-scene.y)/scene.height*2)*scene.tanY;
    const int count=cache.regions.empty()?1:int(cache.regions.size());float best=1e30f;bool found=false;
    for(int region=0;region<count;++region)
    {
        if(scene.accessoryRegion>=0&&region!=scene.accessoryRegion)continue;
        const auto &part=cache.regions.empty()?cache:cache.regions[region];
        const float denom=Dot(ray,part.side.normal);if(std::fabs(denom)<1e-6f)continue;
        float relative[3];for(int a=0;a<3;++a)relative[a]=part.side.origin[a]-scene.camera[a];
        const float distance=Dot(relative,part.side.normal)/denom;if(distance<=0)continue;
        for(int a=0;a<3;++a)relative[a]=scene.camera[a]+ray[a]*distance-part.side.origin[a];
        const float pu=(Dot(relative,part.side.axisX)-part.minUV[0])/(part.maxUV[0]-part.minUV[0]);
        const float pv=(Dot(relative,part.side.axisY)-part.minUV[1])/(part.maxUV[1]-part.minUV[1]);
        // The split view selects a single gun up front. Its actual surface
        // can be in front of the reference plane, so a ray through an edge
        // can intersect that plane just outside its rectangle. Use a clamped
        // seed there; the final screen-space check still rejects empty space.
        // In the ordinary view keep the existing multi-region rejection.
        if(scene.accessoryRegion<0&&(pu<0.f||pu>1.f||pv<0.f||pv>1.f))continue;
        const float cu=std::max(0.f,std::min(1.f,pu)),cv=std::max(0.f,std::min(1.f,pv));
        float sx,sy,scale;const float regionMin=float(region)/count;
        const float regionMax=region+1<count?std::nextafter(float(region+1)/count,regionMin):1.f;
        const float candidateU=std::min(regionMax,(region+cu)/count);
        if(!ProjectAccessory(0,candidateU,cv,sx,sy,scale))continue;
        const float score=(sx-x)*(sx-x)+(sy-y)*(sy-y);
        if(score<best){best=score;u=candidateU;v=cv;found=true;}
    }
    if(found)for(int iteration=0;iteration<3;++iteration)
    {
        float px,py,scale,ux,uy,vx,vy;
        if(!ProjectAccessory(0,u,v,px,py,scale))break;
        if((px-x)*(px-x)+(py-y)*(py-y)<.04f)break;
        const float localMax=scene.accessoryRegion>=0?float(scene.accessoryRegion+1)/count:1.f;
        const float du=u+.001f<localMax?.001f:-.001f,dv=v<.999f?.001f:-.001f;
        if(!ProjectAccessory(0,u+du,v,ux,uy,scale)||!ProjectAccessory(0,u,v+dv,vx,vy,scale))break;
        const float a=(ux-px)/du,b=(vx-px)/dv,c=(uy-py)/du,d=(vy-py)/dv,det=a*d-b*c;
        if(std::fabs(det)<1e-4f)break;
        const float minU=scene.accessoryRegion>=0?float(scene.accessoryRegion)/count:0.f;
        const float maxU=scene.accessoryRegion>=0&&scene.accessoryRegion+1<count?
            std::nextafter(float(scene.accessoryRegion+1)/count,minU):1.f;
        u=std::max(minU,std::min(maxU,u+((x-px)*d-(y-py)*b)/det));
        v=std::max(0.f,std::min(1.f,v+((y-py)*a-(x-px)*c)/det));
    }
    if(!found)return false;
    float sx,sy,scale;
    if(!ProjectAccessory(0,u,v,sx,sy,scale))return false;
    // FindMount may snap a ray over empty space to a distant surface. Accept
    // only the actual surface under the pointer; otherwise retain the mount.
    return (sx-x)*(sx-x)+(sy-y)*(sy-y)<=9.f;
}
bool CMenuPanoramaModelView::AttachmentBounds(int kind,float &left,float &top,float &right,float &bottom) const
{
    if(kind<0||kind>1||!m_scene->attachmentValid[kind])return false;
    const float *rect=m_scene->attachmentRect[kind];left=rect[0];top=rect[1];right=rect[2];bottom=rect[3];return true;
}
void CMenuPanoramaModelView::SetInteractive(bool interactive)
{m_scene->interactive=interactive;if(!interactive)m_scene->dragging=false;}
void CMenuPanoramaModelView::ReloadConfiguration()
{
    resources.Load(true);
    if(m_scene->mode==1)SetWeapon(m_scene->weapon,m_scene->skin);
}
void CMenuPanoramaModelView::ResetView(){m_scene->yaw=m_scene->pitch=0;m_scene->zoom=1;m_scene->dragging=false;}
void CMenuPanoramaModelView::Dump(const char *tag) const
{
    const Scene &scene=*m_scene;
    Con_Printf("oc2_panorama_model: tag=%s mode=%d weapon=%d knife=%d glove=%d skin=%d draw_success=%d viewport=%d/%d/%d/%d model_type=%d material_swaps=%d accessory_entities=%d yaw=%.3f pitch=%.3f zoom=%.3f model=%s pose=%s pose_sequence=%d\n",
        tag?tag:"none",scene.mode,scene.weapon,scene.definition,scene.glove,scene.mode==0?0:scene.skin,
        int(scene.lastDrawSucceeded),scene.x,scene.y,scene.width,scene.height,scene.lastModelType,
        scene.lastMaterialSwaps,scene.lastAccessories,scene.yaw,scene.pitch,scene.zoom,
        scene.lastModel.empty()?"none":scene.lastModel.c_str(),scene.mode==2?"idle_frame0":"reference",
        scene.mode==2&&scene.geometry?scene.geometry->knifeSequence:-1);
    if(scene.mode==0)OC2_DumpPresentation(tag);
}
void CMenuPanoramaModelView::DumpStyle(const char *surface,int index) const
{
    const Scene &s=*m_scene;
    Con_Printf("panorama_item_style: surface=%s index=%d weapon=%d skin=%d sticker_a=%d sticker_b=%d sticker_c=%d sticker_d=%d stattrak=%d charm=%d pattern=%d normalized_scale=%.8f viewport=%d/%d/%d/%d zoom=%.5f drawn=%d\n",
        surface,index,s.weapon,s.skin,s.appearance.stickers[0],s.appearance.stickers[1],s.appearance.stickers[2],s.appearance.stickers[3],
        int(s.accessories.stattrak),int(s.accessories.charm&&s.appearance.charm>=0),s.appearance.charm,s.normalizedScale,s.x,s.y,s.width,s.height,s.zoom,int(s.lastDrawSucceeded));
}
void CMenuPanoramaModelView::VidInit(){CMenuBaseItem::VidInit();m_scene->dragging=false;}
void CMenuPanoramaModelView::Draw(){DrawAt(m_scPos.x,m_scPos.y,m_scSize.w,m_scSize.h);}
bool CMenuPanoramaModelView::DrawAt(int x,int y,int width,int height)
{
    Scene &scene=*m_scene;scene.x=x;scene.y=y;scene.width=width;scene.height=height;
    if(scene.dualUnfold&&scene.mode==1&&scene.weapon==10&&width>=2&&height>0&&
        gpGlobals&&!scene.rendering&&x>=0&&y>=0&&x+width<=gpGlobals->scrWidth&&y+height<=gpGlobals->scrHeight)
    {
        if(!m_dualView)m_dualView=new CMenuPanoramaModelView();
        scene.dualUnfold=false;scene.accessoryRegion=0;
        const int leftWidth=width/2;
        const bool left=DrawAt(x,y,leftWidth,height);
        Scene &right=*m_dualView->m_scene;
        right.mode=scene.mode;right.weapon=scene.weapon;right.skin=scene.skin;
        right.appearance=scene.appearance;right.accessories=scene.accessories;
        right.overridePreview=scene.overridePreview;right.previewPattern=scene.previewPattern;
        right.previewCount=scene.previewCount;right.accessoryRegion=1;
        right.yaw=scene.yaw;right.pitch=scene.pitch;right.zoom=scene.zoom;
        const bool drawnRight=m_dualView->DrawAt(x+width-leftWidth,y,leftWidth,height);
        scene.dualUnfold=true;scene.accessoryRegion=-1;
        // Mouse input for the full-size item includes both viewports.
        scene.x=x;scene.y=y;scene.width=width;scene.height=height;
        // A pair has no single projection or attachment rectangle.
        scene.projectionValid=false;scene.attachmentValid[0]=scene.attachmentValid[1]=false;
        scene.lastAccessories+=right.lastAccessories;
        scene.lastDrawSucceeded=left&&drawnRight;
        return scene.lastDrawSucceeded;
    }
    scene.lastDrawSucceeded=false;scene.lastMaterialSwaps=scene.lastAccessories=0;
    scene.projectionValid=false;scene.attachmentValid[0]=scene.attachmentValid[1]=false;
    scene.lastModel.clear();scene.lastModelType=-1;
    if(!gpGlobals||scene.rendering||width<=0||height<=0)return false;
    if(x<0||y<0||x+width>gpGlobals->scrWidth||y+height>gpGlobals->scrHeight)return false;
    if(scene.mode==0)
    {
        scene.lastDrawSucceeded=OC2_DrawLobbyPreview(scene.team,scene.weapon,x,y,width,height);
        if(scene.lastDrawSucceeded)
        {
            scene.lastModel=scene.team==1?"models/lobby/phoenix_vara.mdl":"models/lobby/sas_vara.mdl";
            scene.lastModelType=mod_studio;
            scene.lastMaterialSwaps=OC2_LastPresentationSwaps();
        }
        return scene.lastDrawSucceeded;
    }
    if(!API())return false;
    if(scene.dragging)
    {
        scene.yaw+=(uiStatic.cursorX-scene.lastX)*.45f;
        scene.pitch=std::max(-75.f,std::min(75.f,scene.pitch+(uiStatic.cursorY-scene.lastY)*.35f));
        scene.lastX=uiStatic.cursorX;scene.lastY=uiStatic.cursorY;
    }
    std::string path;
    const oc2_gloves::Entry *gloves=nullptr;
    const oc2_inventory::Entry *skin=oc2_inventory::FindEntry(resources.skins,scene.skin);
    int body=0;
    if(scene.mode==3)
    {
        // Slot zero keeps the game's team default. The out-of-match default
        // thumbnail explicitly presents the existing CT Hard Knuckle Black
        // model with its embedded original texture, without equipping it.
        if(scene.glove==0)path="models/arms/glove_hardknuckle/v_glove_hardknuckle_black.mdl";
        else
        {
            gloves=oc2_gloves::FindEntry(resources.gloves,scene.glove);
            if(!gloves)return false;path=gloves->model;body=gloves->body;
        }
        skin=nullptr;
    }
    else if(scene.mode==2)
    {
        const auto *knife=oc2_inventory::FindKnifeEntry(resources.skins,scene.definition);
        path=knife?knife->model:"models/v_knife.mdl";
        if(skin&&(skin->kind!=oc2_inventory::Kind::KnifeSkin||skin->definition!=scene.definition))skin=nullptr;
    }
    else
    {
        path=WeaponModel(scene.weapon);
        if(skin&&(skin->kind!=oc2_inventory::Kind::GunSkin||skin->weapon!=scene.weapon))skin=nullptr;
    }
    Initialize(scene.entity,1);scene.entity.curstate.body=body;
    Geometry *geometry=Mesh(path,scene.entity,scene.mode==2);if(!geometry)return false;
    scene.geometry=geometry;
    scene.lastModel=scene.entity.model->name;scene.lastModelType=scene.entity.model->type;
    float mins[3],maxs[3];
    const bool split=scene.mode==1&&scene.weapon==10&&scene.accessoryRegion>=0&&
        RegionBounds(*geometry,body,scene.accessoryRegion,mins,maxs);
    if(!split&&!Bounds(*geometry,body,scene.mode==3,mins,maxs))return false;
    float center[3];for(int axis=0;axis<3;++axis)center[axis]=(mins[axis]+maxs[axis])*.5f;
    float normal[3]={0,-1,0};
    if(scene.mode==1&&geometry->mounts.valid)
    {
        // The dual pistols have two opposing sticker sides. Frame the second
        // pistol's real side so its muzzle faces left like the other pistols.
        const auto &mounts=geometry->mounts;
        const auto &side=split?mounts.regions[scene.accessoryRegion].side:
            scene.weapon==10&&mounts.regions.size()>1?mounts.regions[1].side:mounts.side;
        std::memcpy(normal,side.normal,sizeof(normal));
    }
    const float baseYaw=std::atan2(normal[1],normal[0]),basePitch=std::asin(std::max(-1.f,std::min(1.f,normal[2])));
    const float yaw=baseYaw+scene.yaw*kRadians,pitch=std::max(-1.4f,std::min(1.4f,basePitch+scene.pitch*kRadians));
    float direction[3]={std::cos(pitch)*std::cos(yaw),std::cos(pitch)*std::sin(yaw),std::sin(pitch)};
    float right[3]={-std::sin(yaw),std::cos(yaw),0};
    float up[3]={-std::sin(pitch)*std::cos(yaw),-std::sin(pitch)*std::sin(yaw),std::cos(pitch)};
    const Geometry::CachedBounds *frame=nullptr;
    if(scene.mode==2||scene.mode==3)
    {
        frame=&PresentationFrame(*geometry,body,scene.mode==3,scene.definition);
        std::memcpy(center,frame->center,sizeof(center));
        const float sy=std::sin(scene.yaw*kRadians),cy=std::cos(scene.yaw*kRadians);
        const float sp=std::sin(scene.pitch*kRadians),cp=std::cos(scene.pitch*kRadians);
        for(int a=0;a<3;++a)
        {
            direction[a]=cp*(cy*frame->axes[1][a]+sy*frame->axes[0][a])+sp*frame->axes[2][a];
            right[a]=cy*frame->axes[0][a]-sy*frame->axes[1][a];
        }
        Cross(direction,right,up);
    }
    ref_viewpass_t view={};view.viewport[0]=x;view.viewport[1]=y;view.viewport[2]=width;view.viewport[3]=height;
    view.fov_y=30;const float tanY=std::tan(15*kRadians),tanX=tanY*float(width)/float(height);
    view.fov_x=2*std::atan(tanX)*kDegrees;
    float distance=1,extentRight=0,extentUp=0;
    for(int corner=0;corner<8;++corner)
    {
        float delta[3]{};
        if(frame)
        {for(int a=0;a<3;++a)for(int b=0;b<3;++b)delta[b]+=frame->axes[a][b]*frame->half[a]*((corner&(1<<a))?1.f:-1.f);}
        else for(int axis=0;axis<3;++axis)delta[axis]=((corner&(1<<axis))?maxs[axis]:mins[axis])-center[axis];
        extentRight=std::max(extentRight,std::fabs(Dot(delta,right)));
        extentUp=std::max(extentUp,std::fabs(Dot(delta,up)));
        distance=std::max(distance,std::max(std::fabs(Dot(delta,right))/tanX,std::fabs(Dot(delta,up))/tanY)+Dot(delta,direction));
    }
    if(split&&geometry->mounts.regions.size()==2)
    {
        // Both pistols use the larger required distance, so a model unit has
        // the same on-screen size in each half even when their bounds differ.
        const int other=1-scene.accessoryRegion;float otherMin[3],otherMax[3];
        if(RegionBounds(*geometry,body,other,otherMin,otherMax))
        {
            const auto &side=geometry->mounts.regions[other].side;
            const float otherYaw=std::atan2(side.normal[1],side.normal[0])+scene.yaw*kRadians;
            const float otherPitch=std::max(-1.4f,std::min(1.4f,
                std::asin(std::max(-1.f,std::min(1.f,side.normal[2])))+scene.pitch*kRadians));
            const float od[3]={std::cos(otherPitch)*std::cos(otherYaw),std::cos(otherPitch)*std::sin(otherYaw),std::sin(otherPitch)};
            const float oright[3]={-std::sin(otherYaw),std::cos(otherYaw),0};
            const float oup[3]={-std::sin(otherPitch)*std::cos(otherYaw),-std::sin(otherPitch)*std::sin(otherYaw),std::cos(otherPitch)};
            for(int corner=0;corner<8;++corner)
            {
                float delta[3];for(int a=0;a<3;++a)delta[a]=(otherMax[a]-otherMin[a])*.5f*((corner&(1<<a))?1.f:-1.f);
                distance=std::max(distance,std::max(std::fabs(Dot(delta,oright))/tanX,std::fabs(Dot(delta,oup))/tanY)+Dot(delta,od));
            }
        }
    }
    distance=std::max(5.f,distance*1.13f/scene.zoom);
    scene.normalizedScale=1.f/(2*distance*tanY);
    // The visible extent of the model in screen pixels. The inventory wheel
    // fits the model with it instead of the mostly empty card rectangle.
    scene.contentWidth=extentRight*2*scene.height*scene.normalizedScale;
    scene.contentHeight=extentUp*2*scene.height*scene.normalizedScale;
    for(int axis=0;axis<3;++axis)view.vieworigin[axis]=center[axis]+direction[axis]*distance;
    std::memcpy(scene.camera,view.vieworigin,sizeof(scene.camera));std::memcpy(scene.direction,direction,sizeof(direction));
    std::memcpy(scene.right,right,sizeof(right));std::memcpy(scene.up,up,sizeof(up));scene.tanX=tanX;scene.tanY=tanY;scene.projectionValid=scene.mode==1;
    const float cameraYaw=std::atan2(direction[1],direction[0]),cameraPitch=std::asin(std::max(-1.f,std::min(1.f,direction[2])));
    const float levelRight[3]={-std::sin(cameraYaw),std::cos(cameraYaw),0};
    const float levelUp[3]={-std::sin(cameraPitch)*std::cos(cameraYaw),-std::sin(cameraPitch)*std::sin(cameraYaw),std::cos(cameraPitch)};
    view.viewangles[0]=cameraPitch*kDegrees;view.viewangles[1]=cameraYaw*kDegrees+180;
    view.viewangles[2]=std::atan2(-Dot(right,levelUp),Dot(right,levelRight))*kDegrees;
    DrawOverride weaponOverride;
    if(!weaponOverride.Freeze(*geometry,body,scene.mode==3))return false;
    if(split)weaponOverride.HideOtherRegion(*geometry,body,scene.accessoryRegion);
    if(scene.mode==3&&frame)weaponOverride.PresentGloves(*geometry,body,*frame);
    if(skin)weaponOverride.Replace(skin->material,Texture(skin->file));
    if(gloves)
    {
        weaponOverride.Replace(gloves->leftMaterial,Texture(gloves->leftTexture));
        weaponOverride.Replace(gloves->rightMaterial,Texture(gloves->rightTexture));
    }
    if(scene.mode==1)for(int slot=0;slot<4;++slot)if(scene.appearance.stickers[slot])
    {
        char material[]="decal_A.bmp";material[6]=char('A'+slot);
        weaponOverride.Replace(material,StickerTexture(scene.appearance.stickers[slot]),true);
    }
    oc2_accessories::WeaponConfig config=scene.accessories;
    if(scene.appearance.charm)config.charm=scene.appearance.charm>0;
    Geometry *statGeometry=nullptr,*charmGeometry=nullptr;
    const auto inRegion=[&](float u){return !split||std::min(int(geometry->mounts.regions.size())-1,
        int(std::max(0.f,std::min(1.f,u))*geometry->mounts.regions.size()))==scene.accessoryRegion;};
    const bool drawStat=scene.mode==1&&config.stattrak&&inRegion(config.statU)&&Attachment(*geometry,config,false,scene.stat,statGeometry);
    const bool drawCharm=scene.mode==1&&config.charm&&inRegion(config.charmU)&&Attachment(*geometry,config,true,scene.charm,charmGeometry);
    if(drawStat&&scene.overridePreview)scene.stat.curstate.body=int(scene.previewCount);
    DrawOverride statOverride,charmOverride;
    if(drawStat)statOverride.Freeze(*statGeometry,scene.stat.curstate.body,false);
    if(drawCharm)
    {
        charmOverride.Freeze(*charmGeometry,0,false);
        charmOverride.Scale(config.length);
        int pattern=scene.overridePreview&&!scene.previewPattern.empty()?Texture(scene.previewPattern,true):StickerTexture(scene.appearance.charm);
        if(!pattern&&config.sticker>0)for(const auto &entry:resources.patterns)if(entry.id==config.sticker)
        {pattern=Texture(std::string(oc2_accessories::kStickerDirectory)+entry.file,true);break;}
        charmOverride.Replace("charm_sticker.bmp",pattern,true);
    }
    for(int kind=0;kind<2;++kind)
    {
        Geometry *attachment=kind?charmGeometry:statGeometry;if(!attachment)continue;
        geo::Mount mount;if(!geo::FindMount(geometry->mounts,kind?config.charmU:config.statU,kind?config.charmV:config.statV,mount))continue;
        float origin[3],axisX[3],axisY[3],normal[3];const auto &bone=geometry->bind[mount.bone];
        Transform(bone.m,mount.origin,origin);Transform(bone.m,mount.axisX,axisX,false);Transform(bone.m,mount.axisY,axisY,false);Transform(bone.m,mount.normal,normal,false);
        float *rect=scene.attachmentRect[kind];rect[0]=rect[1]=1e20f;rect[2]=rect[3]=-1e20f;
        const float size=kind?config.length:1.f;
        for(const auto &point:attachment->mesh.positions)
        {
            float local[3],world[3];Transform(attachment->bind[point.bone].m,point.p,local);
            for(int a=0;a<3;++a)world[a]=origin[a]+axisX[a]*local[0]*size+axisY[a]*local[1]*size+normal[a]*(local[2]*size+.01f);
            float sx,sy;if(!scene.Project(world,sx,sy))continue;
            rect[0]=std::min(rect[0],sx);rect[1]=std::min(rect[1],sy);rect[2]=std::max(rect[2],sx);rect[3]=std::max(rect[3],sy);scene.attachmentValid[kind]=true;
        }
    }
    PreviewScene preview;
    scene.rendering=true;EngFuncs::ClearScene();
    const bool added=EngFuncs::CL_CreateVisibleEntity(ET_NORMAL,&scene.entity)!=0;
    if(drawStat&&EngFuncs::CL_CreateVisibleEntity(ET_NORMAL,&scene.stat))++scene.lastAccessories;
    if(drawCharm&&EngFuncs::CL_CreateVisibleEntity(ET_NORMAL,&scene.charm))++scene.lastAccessories;
    EngFuncs::RenderScene(&view);scene.rendering=false;
    scene.lastMaterialSwaps=weaponOverride.SwapCount()+statOverride.SwapCount()+charmOverride.SwapCount();
    scene.lastDrawSucceeded=added;return added;
}
bool CMenuPanoramaModelView::KeyDown(int key)
{
    Scene &scene=*m_scene;if(!scene.interactive||!scene.Hit())return false;
    if(UI::Key::IsLeftMouse(key)){scene.dragging=true;scene.lastX=uiStatic.cursorX;scene.lastY=uiStatic.cursorY;return true;}
    if(key==K_MWHEELUP||key==K_MWHEELDOWN)
    {scene.zoom=std::max(.6f,std::min(2.4f,scene.zoom*(key==K_MWHEELUP?1.1f:1.f/1.1f)));return true;}
    if(key=='r'||key=='R'){ResetView();return true;}
    if(UI::Key::IsLeftArrow(key)){scene.yaw-=10;return true;}
    if(UI::Key::IsRightArrow(key)){scene.yaw+=10;return true;}
    return false;
}
bool CMenuPanoramaModelView::KeyUp(int key)
{
    if(UI::Key::IsLeftMouse(key)&&m_scene->dragging){m_scene->dragging=false;return true;}return false;
}
bool CMenuPanoramaModelView::MouseMove(int x,int y)
{
    if(!m_scene->interactive)return false;
    if(m_scene->dragging)return true;
    return CMenuBaseItem::MouseMove(x,y);
}
