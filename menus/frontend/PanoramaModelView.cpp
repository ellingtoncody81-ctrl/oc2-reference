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
    oc2_inventory::Catalog skins;
    oc2_gloves::Catalog gloves;
    oc2_accessories::Catalog patterns;
    oc2_inventory::Config loadout;
    oc2_accessories::Config accessories;
    oc2_accessories::Counts counts;
    void Load(bool force=false)
    {
        if(loaded&&!force)return;loaded=true;
        std::string text,error;
        if(skins.empty()&&Read(oc2_inventory::kCatalogPath,text))oc2_inventory::ParseCatalog(text.c_str(),skins,error);
        if(gloves.empty()&&Read(oc2_gloves::kCatalogPath,text))oc2_gloves::ParseCatalog(text,gloves,error);
        if(patterns.empty()&&Read(oc2_accessories::kCatalogPath,text))oc2_accessories::ParseCatalog(text.c_str(),patterns,error);
        loadout=oc2_inventory::DefaultConfig();accessories=oc2_accessories::DefaultConfig();counts=oc2_accessories::DefaultCounts();
        if(Read(oc2_inventory::kConfigPath,text))oc2_inventory::ParseConfig(text.c_str(),loadout,error);
        if(Read(oc2_accessories::kConfigPath,text))oc2_accessories::ParseConfig(text.c_str(),accessories,error);
        if(Read(oc2_accessories::kCountsPath,text))oc2_accessories::ParseCounts(text.c_str(),counts,error);
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
bool Bind(studiohdr_t *header,std::vector<geo::BindMatrix> &out)
{
    if(!header||header->numbones<1||header->numbones>MAXSTUDIOBONES)return false;
    auto *bones=reinterpret_cast<oc2_preview_bone_t *>(reinterpret_cast<byte *>(header)+header->boneindex);
    out.resize(header->numbones);
    for(int i=0;i<header->numbones;++i)
    {
        const auto &bone=bones[i];if(bone.parent>=i||bone.parent< -1)return false;
        const float x=bone.value[3],y=bone.value[4],z=bone.value[5];
        const float sx=std::sin(x),cx=std::cos(x),sy=std::sin(y),cy=std::cos(y),sz=std::sin(z),cz=std::cos(z);
        Matrix local;
        local.m[0][0]=cy*cz;local.m[0][1]=sx*sy*cz-cx*sz;local.m[0][2]=cx*sy*cz+sx*sz;
        local.m[1][0]=cy*sz;local.m[1][1]=sx*sy*sz+cx*cz;local.m[1][2]=cx*sy*sz-sx*cz;
        local.m[2][0]=-sy;local.m[2][1]=sx*cy;local.m[2][2]=cx*cy;
        for(int axis=0;axis<3;++axis)local.m[axis][3]=bone.value[axis];
        if(bone.parent>=0)
        {
            Matrix parent;std::memcpy(parent.m,out[bone.parent].m,sizeof(parent.m));local=Product(parent,local);
        }
        std::memcpy(out[i].m,local.m,sizeof(local.m));
    }
    return true;
}
struct Geometry
{
    struct CachedBounds {bool attempted=false,valid=false;float mins[3]{},maxs[3]{};};
    studiohdr_t *header=nullptr;
    geo::StudioTriangleCache mesh;
    std::vector<geo::BindMatrix> bind;
    geo::MountCache mounts;
    mutable std::map<std::pair<int,bool>,CachedBounds> bounds;
    bool valid=false;
};
std::map<std::string,std::unique_ptr<Geometry>> geometries;
Geometry *Mesh(const std::string &path,cl_entity_t &entity)
{
    EngFuncs::SetModel(&entity,path.c_str());if(!entity.model||entity.model->type!=mod_studio)return nullptr;
    PreviewAPI *api=API();if(!api)return nullptr;
    auto *header=static_cast<studiohdr_t *>(api->StudioHeader(&entity));
    if(!header)return nullptr;
    auto &entry=geometries[path];
    if(!entry||entry->header!=header)
    {
        entry.reset(new Geometry);entry->header=header;
        entry->valid=geo::DecodeStudioTriangles(header,entry->mesh)&&Bind(header,entry->bind);
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
        if(!Active(geometry.mesh,triangle,body)||triangle.placeholder)continue;
        const int material=Material(geometry.header,triangle.material);
        if(material<0||HiddenMaterial(materials[material].name,gloves))continue;
        for(const auto &corner:triangle.corner)
        {
            float point[3];Transform(geometry.bind[corner.bone].m,corner.p,point);
            for(int axis=0;axis<3;++axis){mins[axis]=std::min(mins[axis],point[axis]);maxs[axis]=std::max(maxs[axis],point[axis]);}
            found=true;
        }
    }
    cached.valid=found;std::memcpy(cached.mins,mins,sizeof(cached.mins));std::memcpy(cached.maxs,maxs,sizeof(cached.maxs));
    return found;
}
// Only the duration of RenderScene sees a frozen bind pose. This is the same
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
                    if(material>=0&&HiddenMaterial(materials[material].name,gloves))
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

struct CMenuPanoramaModelView::Scene
{
    int mode=1,team=2,weapon=28,definition=0,skin=0,glove=0;
    oc2_inventory::WeaponState appearance;
    oc2_accessories::WeaponConfig accessories;
    bool interactive=true,dragging=false,rendering=false;
    float yaw=0,pitch=0,zoom=1;
    int x=0,y=0,width=0,height=0,lastX=0,lastY=0;
    std::string lastModel;
    int lastModelType=-1,lastMaterialSwaps=0,lastAccessories=0;
    bool lastDrawSucceeded=false;
    cl_entity_t entity{},stat{},charm{};
    bool Hit() const{return uiStatic.cursorX>=x&&uiStatic.cursorY>=y&&uiStatic.cursorX<x+width&&uiStatic.cursorY<y+height;}
};
CMenuPanoramaModelView::CMenuPanoramaModelView():m_scene(new Scene)
{
    eFocusAnimation=QM_HIGHLIGHTIFFOCUS;
    SetWeapon(28);
}
CMenuPanoramaModelView::~CMenuPanoramaModelView(){delete m_scene;}
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
    if(config)m_scene->accessories=*config;
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
    Con_Printf("oc2_panorama_model: tag=%s mode=%d weapon=%d knife=%d glove=%d skin=%d draw_success=%d viewport=%d/%d/%d/%d model_type=%d material_swaps=%d accessory_entities=%d yaw=%.3f pitch=%.3f zoom=%.3f model=%s\n",
        tag?tag:"none",scene.mode,scene.weapon,scene.definition,scene.glove,scene.mode==0?0:scene.skin,
        int(scene.lastDrawSucceeded),scene.x,scene.y,scene.width,scene.height,scene.lastModelType,
        scene.lastMaterialSwaps,scene.lastAccessories,scene.yaw,scene.pitch,scene.zoom,
        scene.lastModel.empty()?"none":scene.lastModel.c_str());
}
void CMenuPanoramaModelView::VidInit(){CMenuBaseItem::VidInit();m_scene->dragging=false;}
void CMenuPanoramaModelView::Draw(){DrawAt(m_scPos.x,m_scPos.y,m_scSize.w,m_scSize.h);}
bool CMenuPanoramaModelView::DrawAt(int x,int y,int width,int height)
{
    Scene &scene=*m_scene;scene.x=x;scene.y=y;scene.width=width;scene.height=height;
    scene.lastDrawSucceeded=false;scene.lastMaterialSwaps=scene.lastAccessories=0;
    scene.lastModel.clear();scene.lastModelType=-1;
    if(!gpGlobals||scene.rendering||width<=0||height<=0)return false;
    if(x<0||y<0||x+width>gpGlobals->scrWidth||y+height>gpGlobals->scrHeight)return false;
    if(scene.mode==0)
    {
        PreviewScene preview;
        scene.lastDrawSucceeded=OC2_DrawBuyPreview(scene.team,scene.weapon,x,y,width,height);
        if(scene.lastDrawSucceeded)
        {
            scene.lastModel=scene.team==1?"models/lobby/phoenix_vara.mdl":"models/lobby/sas_vara.mdl";
            scene.lastModelType=mod_studio;
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
    Geometry *geometry=Mesh(path,scene.entity);if(!geometry)return false;
    scene.lastModel=scene.entity.model->name;scene.lastModelType=scene.entity.model->type;
    float mins[3],maxs[3];if(!Bounds(*geometry,body,scene.mode==3,mins,maxs))return false;
    float center[3];for(int axis=0;axis<3;++axis)center[axis]=(mins[axis]+maxs[axis])*.5f;
    float normal[3]={0,-1,0};
    if(scene.mode==1&&geometry->mounts.valid)std::memcpy(normal,geometry->mounts.side.normal,sizeof(normal));
    const float baseYaw=std::atan2(normal[1],normal[0]),basePitch=std::asin(std::max(-1.f,std::min(1.f,normal[2])));
    const float yaw=baseYaw+scene.yaw*kRadians,pitch=std::max(-1.4f,std::min(1.4f,basePitch+scene.pitch*kRadians));
    const float direction[3]={std::cos(pitch)*std::cos(yaw),std::cos(pitch)*std::sin(yaw),std::sin(pitch)};
    const float right[3]={-std::sin(yaw),std::cos(yaw),0};
    const float up[3]={-std::sin(pitch)*std::cos(yaw),-std::sin(pitch)*std::sin(yaw),std::cos(pitch)};
    ref_viewpass_t view={};view.viewport[0]=x;view.viewport[1]=y;view.viewport[2]=width;view.viewport[3]=height;
    view.fov_y=30;const float tanY=std::tan(15*kRadians),tanX=tanY*float(width)/float(height);
    view.fov_x=2*std::atan(tanX)*kDegrees;
    float distance=1;
    for(int corner=0;corner<8;++corner)
    {
        float delta[3];for(int axis=0;axis<3;++axis)delta[axis]=((corner&(1<<axis))?maxs[axis]:mins[axis])-center[axis];
        distance=std::max(distance,std::max(std::fabs(Dot(delta,right))/tanX,std::fabs(Dot(delta,up))/tanY)+Dot(delta,direction));
    }
    distance=std::max(5.f,distance*1.13f/scene.zoom);
    for(int axis=0;axis<3;++axis)view.vieworigin[axis]=center[axis]+direction[axis]*distance;
    view.viewangles[0]=pitch*kDegrees;view.viewangles[1]=yaw*kDegrees+180;
    DrawOverride weaponOverride;
    if(!weaponOverride.Freeze(*geometry,body,scene.mode==3))return false;
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
    const bool drawStat=scene.mode==1&&config.stattrak&&Attachment(*geometry,config,false,scene.stat,statGeometry);
    const bool drawCharm=scene.mode==1&&config.charm&&Attachment(*geometry,config,true,scene.charm,charmGeometry);
    DrawOverride statOverride,charmOverride;
    if(drawStat)statOverride.Freeze(*statGeometry,scene.stat.curstate.body,false);
    if(drawCharm)
    {
        charmOverride.Freeze(*charmGeometry,0,false);
        charmOverride.Scale(config.length);
        int pattern=StickerTexture(scene.appearance.charm);
        if(!pattern&&config.sticker>0)for(const auto &entry:resources.patterns)if(entry.id==config.sticker)
        {pattern=Texture(std::string(oc2_accessories::kStickerDirectory)+entry.file,true);break;}
        charmOverride.Replace("charm_sticker.bmp",pattern,true);
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
