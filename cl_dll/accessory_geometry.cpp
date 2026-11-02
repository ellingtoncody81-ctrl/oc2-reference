#include "accessory_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <string>

namespace oc2_accessory
{
namespace
{
struct V
{
    double x, y, z;
    V(double a = 0, double b = 0, double c = 0) : x(a), y(b), z(c) {}
    explicit V(const float* p) : x(p[0]), y(p[1]), z(p[2]) {}
    V operator+(V b) const { return V(x+b.x,y+b.y,z+b.z); }
    V operator-(V b) const { return V(x-b.x,y-b.y,z-b.z); }
    V operator*(double s) const { return V(x*s,y*s,z*s); }
    double dot(V b) const { return x*b.x+y*b.y+z*b.z; }
    V cross(V b) const { return V(y*b.z-z*b.y,z*b.x-x*b.z,x*b.y-y*b.x); }
    double length2() const { return dot(*this); }
    V unit() const { double l=std::sqrt(length2()); return l>1e-15 ? *this*(1/l) : V(); }
    bool finite() const { return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z); }
    void store(float* out) const { out[0]=float(x);out[1]=float(y);out[2]=float(z); }
};
double clamp(double x,double a,double b) { return std::max(a,std::min(b,x)); }
V point(const float m[3][4],V p,bool position=true)
{
    return V(m[0][0]*p.x+m[0][1]*p.y+m[0][2]*p.z+(position?m[0][3]:0),
             m[1][0]*p.x+m[1][1]*p.y+m[1][2]*p.z+(position?m[1][3]:0),
             m[2][0]*p.x+m[2][1]*p.y+m[2][2]*p.z+(position?m[2][3]:0));
}
V inverse(const float m[3][4],V p,bool position=true)
{
    if(position)p=p-V(m[0][3],m[1][3],m[2][3]);
    return V(m[0][0]*p.x+m[1][0]*p.y+m[2][0]*p.z,
             m[0][1]*p.x+m[1][1]*p.y+m[2][1]*p.z,
             m[0][2]*p.x+m[1][2]*p.y+m[2][2]*p.z);
}
template<class T> const T* at(const studiohdr_t* h,int offset,int count=1)
{
    if(!h||offset<0||count<0||h->length<int(sizeof(studiohdr_t)))return nullptr;
    const size_t bytes=size_t(count)*sizeof(T);
    if(size_t(offset)>size_t(h->length)||bytes>size_t(h->length)-size_t(offset))return nullptr;
    return reinterpret_cast<const T*>(reinterpret_cast<const unsigned char*>(h)+offset);
}
std::string lower(const char* p,size_t length)
{
    std::string s;
    for(size_t i=0;i<length&&p[i];++i)s.push_back(char(p[i]>='A'&&p[i]<='Z'?p[i]+32:p[i]));
    return s;
}
bool contains(const std::string& s,const char* needle) { return s.find(needle)!=std::string::npos; }
bool placeholder(const std::string& s)
{
    return s=="null"||s=="blank"||s=="empty"||s=="null.bmp"||s=="blank.bmp";
}
bool material(const StudioTriangleCache& c,int skin,int reference,int& flags,std::string& name)
{
    const studiohdr_t* h=c.header;
    if(!h||reference<0||reference>=h->numskinref||h->numskinfamilies<=0)return false;
    skin=skin>=0&&skin<h->numskinfamilies?skin:0;
    const short* skins=at<short>(h,h->skinindex,h->numskinref*h->numskinfamilies);
    const mstudiotexture_t* textures=at<mstudiotexture_t>(h,h->textureindex,h->numtextures);
    if(!skins||!textures)return false;
    int index=skins[skin*h->numskinref+reference];
    if(index<0||index>=h->numtextures)return false;
    flags=textures[index].flags;name=lower(textures[index].name,sizeof(textures[index].name));return true;
}
bool real(const StudioTriangleCache& c,const StudioTriangle& t,int skin)
{
    int flags;std::string name;
    return !t.decal&&!t.chrome&&!t.placeholder&&material(c,skin,t.material,flags,name)&&
        !(flags&STUDIO_NF_CHROME)&&!contains(name,"decal_")&&!placeholder(name);
}
bool allowedTriangle(const StudioTriangle& triangle,const std::vector<int>* allowedBones)
{
    if(!allowedBones||allowedBones->empty())return true;
    for(const auto& corner:triangle.corner)
        if(std::find(allowedBones->begin(),allowedBones->end(),corner.bone)!=allowedBones->end())return true;
    return false;
}
Triangle posed(const StudioTriangle& t,const float (*matrices)[3][4],bool positionsOnly=false)
{
    Triangle out;V normal;
    out.bone=t.corner[0].bone;
    for(int j=0;j<3;++j)
    {
        const StudioCorner& c=t.corner[j];
        point(matrices[c.bone],V(c.p)).store(out.p[j]);
        if(!positionsOnly)normal=normal+point(matrices[c.normalBone],V(c.normal),false);
        if(c.bone!=out.bone)out.bone=-1;
    }
    if(!positionsOnly)normal.unit().store(out.normal);return out;
}
bool buildBind(const studiohdr_t* h,std::vector<BindMatrix>& out)
{
    const mstudiobone_t* bones=at<mstudiobone_t>(h,h->boneindex,h->numbones);
    if(!bones||h->numbones<=0||h->numbones>256)return false;
    out.resize(h->numbones);std::vector<bool> done(h->numbones,false);int remaining=h->numbones;
    for(int pass=0;pass<h->numbones&&remaining;++pass)
        for(int i=0;i<h->numbones;++i)
        {
            int parent=bones[i].parent;
            if(done[i]||parent>=h->numbones||parent==i||(parent>=0&&!done[parent]))continue;
            const float* v=bones[i].value;
            double sx=std::sin(v[3]),cx=std::cos(v[3]),sy=std::sin(v[4]),cy=std::cos(v[4]),sz=std::sin(v[5]),cz=std::cos(v[5]);
            double m[3][4]={{cz*cy,cz*sy*sx-sz*cx,cz*sy*cx+sz*sx,v[0]},
                           {sz*cy,sz*sy*sx+cz*cx,sz*sy*cx-cz*sx,v[1]},
                           {-sy,cy*sx,cy*cx,v[2]}};
            for(int r=0;r<3;++r)for(int col=0;col<4;++col)
            {
                double value=m[r][col];
                if(parent>=0)
                {
                    value=col==3?out[parent].m[r][3]:0;
                    for(int k=0;k<3;++k)value+=out[parent].m[r][k]*m[k][col];
                }
                if(!std::isfinite(value))return false;
                out[i].m[r][col]=float(value);
            }
            done[i]=true;--remaining;
        }
    return remaining==0;
}
bool armBone(const mstudiobone_t& bone)
{
    const std::string name=lower(bone.name,sizeof(bone.name));
    return contains(name,"bip01")&&(contains(name,"hand")||contains(name,"finger")||contains(name,"foretwist")||contains(name,"arm"));
}
int weaponRoot(const mstudiobone_t* bones,int bone)
{
    while(bones[bone].parent>=0&&!armBone(bones[bones[bone].parent]))bone=bones[bone].parent;
    return bone;
}
V project(const Frame& f,V p)
{
    V d=p-V(f.origin);return V(d.dot(V(f.axisX)),d.dot(V(f.axisY)),d.dot(V(f.normal)));
}
double cross2(V a,V b) { return a.x*b.y-a.y*b.x; }
V closest2(V p,V a,V b)
{
    V d=b-a;double l=d.x*d.x+d.y*d.y;
    return a+d*(l>1e-24?clamp(((p.x-a.x)*d.x+(p.y-a.y)*d.y)/l,0,1):0);
}
int clipRectangle(V* polygon,int count,double minX,double maxX,double minY,double maxY)
{
    V clipped[16];
    for(int plane=0;plane<4&&count;++plane)
    {
        int nextCount=0;
        auto distance=[&](V p) { return plane==0?p.x-minX:plane==1?maxX-p.x:plane==2?p.y-minY:maxY-p.y; };
        V previous=polygon[count-1];double before=distance(previous);
        for(int i=0;i<count;++i)
        {
            V current=polygon[i];double after=distance(current);
            if((before>=0)!=(after>=0))clipped[nextCount++]=previous+(current-previous)*(before/(before-after));
            if(after>=0)clipped[nextCount++]=current;
            previous=current;before=after;
        }
        count=nextCount;std::copy(clipped,clipped+count,polygon);
    }
    return count;
}
template<class F> int clipPlane(V* polygon,int count,F distance)
{
    if(!count)return 0;
    V clipped[16],previous=polygon[count-1];double before=distance(previous);int n=0;
    for(int i=0;i<count;++i)
    {
        V current=polygon[i];double after=distance(current);
        if((before>=0)!=(after>=0))clipped[n++]=previous+(current-previous)*(before/(before-after));
        if(after>=0)clipped[n++]=current;
        previous=current;before=after;
    }
    std::copy(clipped,clipped+n,polygon);return n;
}
bool sideFacing(const MountCache& c,const Triangle& t)
{
    const V normal(t.normal),side(c.side.normal);
    const double dot=normal.dot(side),length2=normal.length2();
    return dot>0&&dot*dot>=kMinimumSideFacing*kMinimumSideFacing*length2*side.length2()&&length2>.5;
}
bool frontAt(const MountCache& c,V query,const Triangle*& result,V& pointOut,double& depth)
{
    result=nullptr;depth=-std::numeric_limits<double>::infinity();
    const std::vector<int>* bin=nullptr;
    if(c.surfaceGridSize>0&&query.x>=c.minUV[0]-1e-8&&query.x<=c.maxUV[0]+1e-8&&
       query.y>=c.minUV[1]-1e-8&&query.y<=c.maxUV[1]+1e-8)
    {
        int x=int((query.x-c.minUV[0])/(c.maxUV[0]-c.minUV[0])*c.surfaceGridSize);
        int y=int((query.y-c.minUV[1])/(c.maxUV[1]-c.minUV[1])*c.surfaceGridSize);
        x=std::max(0,std::min(c.surfaceGridSize-1,x));y=std::max(0,std::min(c.surfaceGridSize-1,y));
        bin=&c.surfaceBins[y*c.surfaceGridSize+x];
    }
    const size_t count=bin?bin->size():c.surfaces.size();
    for(size_t index=0;index<count;++index)
    {
        const Triangle& t=c.surfaces[bin?(*bin)[index]:index];
        if(!sideFacing(c,t))continue;
        V p[3]={project(c.side,V(t.p[0])),project(c.side,V(t.p[1])),project(c.side,V(t.p[2]))};
        if(query.x<std::min(p[0].x,std::min(p[1].x,p[2].x))-1e-9||query.x>std::max(p[0].x,std::max(p[1].x,p[2].x))+1e-9||
           query.y<std::min(p[0].y,std::min(p[1].y,p[2].y))-1e-9||query.y>std::max(p[0].y,std::max(p[1].y,p[2].y))+1e-9)continue;
        double den=cross2(p[1]-p[0],p[2]-p[0]);if(std::abs(den)<1e-16)continue;
        double b=cross2(query-p[0],p[2]-p[0])/den,d=cross2(p[1]-p[0],query-p[0])/den;
        if(b< -1e-8||d< -1e-8||b+d>1+1e-8)continue;
        double z=p[0].z*(1-b-d)+p[1].z*b+p[2].z*d;
        if(z>depth){result=&t;depth=z;pointOut=V(t.p[0])*(1-b-d)+V(t.p[1])*b+V(t.p[2])*d;}
    }
    return result!=nullptr;
}
bool sameActive(const StudioTriangleCache& c,const StudioTriangle& t,int body)
{
    const StudioPart& p=c.parts[t.part];
    return p.base>0&&p.models>0&&t.model==((std::max(0,body)/p.base)%p.models);
}
SwingOutput swingOutput(const SwingState& s,const SwingConfig& config,double time)
{
    const double step=1.0/120.0;
    const double alpha=clamp((time-s.simulationTime)/step,0,1);
    const double limit=std::isfinite(config.maxAngle)?clamp(config.maxAngle,0,75):35;
    SwingOutput out;
    out.pitch=float(clamp((s.previousAngle[0]+(s.angle[0]-s.previousAngle[0])*alpha)*180/3.14159265358979323846,-limit,limit));
    out.yaw=float(clamp((s.previousAngle[1]+(s.angle[1]-s.previousAngle[1])*alpha)*180/3.14159265358979323846,-limit,limit));
    return out;
}
}

bool DecodeStudioTriangles(const studiohdr_t* h,StudioTriangleCache& out)
{
    out=StudioTriangleCache();
    if(!h||h->id!=0x54534449||h->version!=10||h->length<int(sizeof(*h))||h->numbones<=0||h->numbones>256||
        h->numbodyparts<0||h->numbodyparts>128||h->numtextures<=0||h->numtextures>4096||
        h->numskinref<=0||h->numskinref>4096||h->numskinfamilies<=0||h->numskinfamilies>4096)return false;
    const mstudiobodyparts_t* parts=at<mstudiobodyparts_t>(h,h->bodypartindex,h->numbodyparts);
    if(!parts)return false;
    StudioTriangleCache result;result.header=h;
    for(int p=0;p<h->numbodyparts;++p)
    {
        if(parts[p].base<=0||parts[p].nummodels<0||parts[p].nummodels>256)return false;
        result.parts.push_back({parts[p].base,parts[p].nummodels});
        const mstudiomodel_t* models=at<mstudiomodel_t>(h,parts[p].modelindex,parts[p].nummodels);
        if(!models)return false;
        const std::string partName=lower(parts[p].name,sizeof(parts[p].name));
        for(int m=0;m<parts[p].nummodels;++m)
        {
            const mstudiomodel_t& model=models[m];
            if(model.nummesh<0||model.nummesh>4096||model.numverts<0||model.numnorms<0)return false;
            const mstudiomesh_t* meshes=at<mstudiomesh_t>(h,model.meshindex,model.nummesh);
            const float (*vertices)[3]=at<float[3]>(h,model.vertindex,model.numverts);
            const unsigned char* vb=at<unsigned char>(h,model.vertinfoindex,model.numverts);
            const float (*normals)[3]=at<float[3]>(h,model.normindex,model.numnorms);
            const unsigned char* nb=at<unsigned char>(h,model.norminfoindex,model.numnorms);
            if(!meshes||!vertices||!vb||!normals||!nb)return false;
            const int positionBase=int(result.positions.size());
            if(result.positions.size()+size_t(model.numverts)>1000000)return false;
            for(int vertex=0;vertex<model.numverts;++vertex)
            {
                StudioPosition position;position.bone=vb[vertex];std::copy(vertices[vertex],vertices[vertex]+3,position.p);result.positions.push_back(position);
            }
            const std::string modelName=lower(model.name,sizeof(model.name));
            for(int me=0;me<model.nummesh;++me)
            {
                StudioTriangle meshTriangle{};
                meshTriangle.part=p;meshTriangle.model=m;meshTriangle.material=meshes[me].skinref;
                meshTriangle.decal=contains(partName,"decal")||contains(modelName,"decal");
                meshTriangle.chrome=contains(partName,"chrome")||contains(modelName,"chrome");
                meshTriangle.placeholder=placeholder(partName)||placeholder(modelName);
                meshTriangle.skin0Real=real(result,meshTriangle,0);
                int cursor=meshes[me].triindex,decoded=0;
                for(;;)
                {
                    const short* command=at<short>(h,cursor);
                    if(!command)return false;
                    int count=*command;cursor+=2;
                    if(!count)break;
                    int n=std::abs(count);if(n<3||n>65535)return false;
                    const short* points=at<short>(h,cursor,n*4);if(!points)return false;
                    std::vector<StudioCorner> corners(n);
                    std::vector<int> positionIndices(n);
                    for(int i=0;i<n;++i)
                    {
                        int v=points[i*4],no=points[i*4+1];
                        if(v<0||v>=model.numverts||no<0||no>=model.numnorms||vb[v]>=h->numbones||nb[no]>=h->numbones)return false;
                        StudioCorner& c=corners[i];c.bone=vb[v];c.normalBone=nb[no];
                        positionIndices[i]=positionBase+v;
                        std::copy(vertices[v],vertices[v]+3,c.p);std::copy(normals[no],normals[no]+3,c.normal);
                        if(!V(c.p).finite()||!V(c.normal).finite())return false;
                    }
                    for(int i=2;i<n;++i)
                    {
                        StudioTriangle t=meshTriangle;
                        int a=count<0?0:(i%2?i-1:i-2),b=count<0?i-1:(i%2?i-2:i-1);
                        t.corner[0]=corners[a];t.corner[1]=corners[b];t.corner[2]=corners[i];
                        result.triangles.push_back(t);++decoded;
                        result.positionIndices.push_back({positionIndices[a],positionIndices[b],positionIndices[i]});
                        if(result.triangles.size()>1000000)return false;
                    }
                    cursor+=n*8;
                }
                if(decoded!=meshes[me].numtris)return false;
            }
        }
    }
    out=std::move(result);return true;
}

bool ReadStudioTriangles(const StudioTriangleCache& c,int body,int skin,const float (*poseMatrices)[3][4],
                          std::vector<Triangle>& out,bool append,bool positionsOnly,const std::vector<int>* allowedBones)
{
    if(!append)out.clear();
    if(!c.header||!poseMatrices)return false;
    skin=skin>=0&&skin<c.header->numskinfamilies?skin:0;
    if(skin!=0&&c.materialSkin!=skin)
    {
        c.realMaterials.assign(c.header->numskinref,0);
        for(int ref=0;ref<c.header->numskinref;++ref)
        {
            int flags;std::string name;
            c.realMaterials[ref]=material(c,skin,ref,flags,name)&&!(flags&STUDIO_NF_CHROME)&&
                !contains(name,"decal_")&&!placeholder(name);
        }
        c.materialSkin=skin;
    }
    int group=0;for(const Triangle& t:out)group=std::max(group,t.group+1);
    if(positionsOnly)
    {
        c.posedPositions.resize(c.positions.size());
        if(++c.poseGeneration==0){c.poseGeneration=1;for(auto& position:c.posedPositions)position.generation=0;}
    }
    for(size_t index=0;index<c.triangles.size();++index)
    {
        const StudioTriangle& t=c.triangles[index];
        if(sameActive(c,t,body)&&allowedTriangle(t,allowedBones)&&(skin==0?t.skin0Real:
           !t.decal&&!t.chrome&&!t.placeholder&&t.material>=0&&t.material<int(c.realMaterials.size())&&c.realMaterials[t.material]))
        {
            Triangle p;
            if(positionsOnly)
            {
                p.bone=t.corner[0].bone;
                for(int j=0;j<3;++j)
                {
                    const int id=c.positionIndices[index][j];auto& cached=c.posedPositions[id];
                    if(cached.generation!=c.poseGeneration)
                    {
                        const auto& raw=c.positions[id];point(poseMatrices[raw.bone],V(raw.p)).store(cached.p);cached.generation=c.poseGeneration;
                    }
                    std::copy(cached.p,cached.p+3,p.p[j]);if(t.corner[j].bone!=p.bone)p.bone=-1;
                }
            }
            else p=posed(t,poseMatrices);
            p.group=group+t.part;out.push_back(p);
        }
    }
    return true;
}
bool ReadStudioTriangles(const studiohdr_t* h,int body,int skin,const float (*poseMatrices)[3][4],
                         std::vector<Triangle>& out,bool append,const std::vector<int>* allowedBones)
{
    StudioTriangleCache cache;
    return DecodeStudioTriangles(h,cache)&&ReadStudioTriangles(cache,body,skin,poseMatrices,out,append,false,allowedBones);
}

static bool BuildMountRegion(const studiohdr_t* h,MountCache& out,int root,const StudioTriangleCache* orientation=nullptr)
{
    out=MountCache();
    if(!DecodeStudioTriangles(h,out.studio)||!buildBind(h,out.bind))return false;
    out.rigRoot=root;
    const auto* sourceBones=at<mstudiobone_t>(h,h->boneindex,h->numbones);
    if(!sourceBones)return false;
    const float (*matrices)[3][4]=&out.bind[0].m;
    std::vector<Triangle> decals;std::set<int> bones;V normal;
    V seed;
    const auto &side=orientation?*orientation:out.studio;
    for(const StudioTriangle& t:side.triangles)
        if(t.decal&&sameActive(side,t,0)&&weaponRoot(sourceBones,t.corner[0].bone)==root)
        {
            Triangle tri=posed(t,matrices);if(tri.bone<0)continue;
            V a(tri.p[0]),b(tri.p[1]),c(tri.p[2]),n(tri.normal);
            double area=(b-a).cross(c-a).length2();
            if(area<=1e-24||n.length2()<.5)continue;
            if(seed.length2()==0)seed=n;
            // All four slots contribute their actual bone set; only their
            // coherently facing side contributes to this side's 2D frame.
            bones.insert(tri.bone);
            if(n.dot(seed)>.15){decals.push_back(tri);normal=normal+n*std::sqrt(area);}
        }
    if(decals.empty()||normal.length2()<1e-20)return false;
    V n=normal.unit(),up=V(0,0,1)-n*n.z;
    if(up.length2()<1e-8)up=V(0,1,0)-n*n.y;
    up=up.unit();V right=up.cross(n).unit();up=n.cross(right).unit();
    right.store(out.side.axisX);up.store(out.side.axisY);n.store(out.side.normal);
    out.minUV[0]=out.minUV[1]=std::numeric_limits<float>::infinity();
    out.maxUV[0]=out.maxUV[1]=-std::numeric_limits<float>::infinity();
    out.originalDecalBones.assign(bones.begin(),bones.end());
    const mstudiobodyparts_t* sourceParts=at<mstudiobodyparts_t>(h,h->bodypartindex,h->numbodyparts);
    std::set<int> weaponBones;
    for(const StudioTriangle& raw:out.studio.triangles)
    {
        if(!sameActive(out.studio,raw,0)||!real(out.studio,raw,0))continue;
        int textureFlags;std::string textureName;
        if(lower(sourceParts[raw.part].name,sizeof(sourceParts[raw.part].name))=="scope"&&
           material(out.studio,0,raw.material,textureFlags,textureName)&&
           (textureName=="scope_aug.bmp"||textureName=="rif_sg556_scope.bmp"))continue;
        // Four decals define orientation only. Their historical rectangle and
        // bone ownership must never restrict the user's whole-gun placement.
        bool arm=false;
        for(const StudioCorner& corner:raw.corner)
        {
            std::string name=lower(sourceBones[corner.bone].name,sizeof(sourceBones[corner.bone].name));
            arm|=contains(name,"bip01")&&(contains(name,"hand")||contains(name,"finger")||contains(name,"foretwist")||contains(name,"arm"));
        }
        if(arm&&!orientation)continue;
        bool belongs=true;for(const auto& corner:raw.corner)belongs=belongs&&weaponRoot(sourceBones,corner.bone)==root;
        if(!belongs)continue;
        Triangle t=posed(raw,matrices);
        V gn=(V(t.p[1])-V(t.p[0])).cross(V(t.p[2])-V(t.p[0]));
        if(gn.length2()<1e-24)continue;
        for(const StudioCorner& corner:raw.corner)weaponBones.insert(corner.bone);
        for(int i=0;i<3;++i)
        {
            V p=project(out.side,V(t.p[i]));
            out.minUV[0]=std::min(out.minUV[0],float(p.x));out.maxUV[0]=std::max(out.maxUV[0],float(p.x));
            out.minUV[1]=std::min(out.minUV[1],float(p.y));out.maxUV[1]=std::max(out.maxUV[1],float(p.y));
        }
        // A rigid anchor cannot follow a face whose corners deform on different
        // bones. Such seams still contribute to the whole-gun domain; holes
        // and deforming seams snap to the nearest real rigid surface instead.
        gn=gn.unit();if(gn.dot(V(t.normal))<0)gn=gn*-1;
        if(gn.dot(n)<kMinimumSideFacing)continue;
        Triangle surface=t;gn.store(surface.normal);out.surfaces.push_back(surface);
    }
    out.weaponBones.assign(weaponBones.begin(),weaponBones.end());
    out.valid=!out.surfaces.empty()&&out.maxUV[0]>out.minUV[0]+1e-6f&&out.maxUV[1]>out.minUV[1]+1e-6f;
    if(out.valid)
    {
        out.surfaceGridSize=32;out.surfaceBins.resize(32*32);
        for(size_t index=0;index<out.surfaces.size();++index)
        {
            int lo[2]={31,31},hi[2]={0,0};
            for(int i=0;i<3;++i)
            {
                V p=project(out.side,V(out.surfaces[index].p[i]));const double q[2]={p.x,p.y};
                for(int axis=0;axis<2;++axis)
                {
                    int cell=int((q[axis]-out.minUV[axis])/(out.maxUV[axis]-out.minUV[axis])*32);
                    cell=std::max(0,std::min(31,cell));lo[axis]=std::min(lo[axis],cell);hi[axis]=std::max(hi[axis],cell);
                }
            }
            for(int y=lo[1];y<=hi[1];++y)for(int x=lo[0];x<=hi[0];++x)out.surfaceBins[y*32+x].push_back(int(index));
        }
    }
    return out.valid;
}

bool BuildMountCache(const studiohdr_t* h,MountCache& out,const studiohdr_t* orientationHeader)
{
    out=MountCache();StudioTriangleCache decoded,orientation;
    if(!DecodeStudioTriangles(h,decoded))return false;
    if(orientationHeader&&(!DecodeStudioTriangles(orientationHeader,orientation)||orientationHeader->numbones!=h->numbones))return false;
    const auto &side=orientationHeader?orientation:decoded;
    const auto* bones=at<mstudiobone_t>(h,h->boneindex,h->numbones);if(!bones)return false;
    std::set<int> roots;
    for(const auto& triangle:side.triangles)if(triangle.decal&&sameActive(side,triangle,0))
        roots.insert(weaponRoot(bones,triangle.corner[0].bone));
    if(roots.empty())return false;
    if(roots.size()==1)return BuildMountRegion(h,out,*roots.begin(),orientationHeader?&side:nullptr);
    std::vector<MountCache> regions;
    for(int root:roots){MountCache region;if(BuildMountRegion(h,region,root,orientationHeader?&side:nullptr))regions.push_back(std::move(region));}
    if(regions.empty())return false;
    out=regions.front();
    out.regions=std::move(regions);
    // Preserve all real body components for callers choosing a collision frame.
    for(size_t i=1;i<out.regions.size();++i)
    {
        const auto& region=out.regions[i];out.surfaces.insert(out.surfaces.end(),region.surfaces.begin(),region.surfaces.end());
        out.weaponBones.insert(out.weaponBones.end(),region.weaponBones.begin(),region.weaponBones.end());
    }
    return true;
}

bool BoneSocketDisplacement(const MountCache& c,int receiver,int part,
                            const float (*poseMatrices)[3][4],float& maxDistance)
{
    maxDistance=0;
    if(!c.studio.header||!poseMatrices||receiver<0||part<0||
       size_t(receiver)>=c.bind.size()||size_t(part)>=c.bind.size())return false;
    const auto relative=[](const float parent[3][4],const float child[3][4],double out[3][4])
    {
        const V rows[3]={V(parent[0]),V(parent[1]),V(parent[2])};
        const double determinant=rows[0].dot(rows[1].cross(rows[2]));
        if(!std::isfinite(determinant)||std::abs(determinant)<1e-12)return false;
        const V columns[3]={rows[1].cross(rows[2])*(1/determinant),
                            rows[2].cross(rows[0])*(1/determinant),
                            rows[0].cross(rows[1])*(1/determinant)};
        for(int column=0;column<4;++column)
        {
            V value(child[0][column],child[1][column],child[2][column]);
            if(column==3)value=value-V(parent[0][3],parent[1][3],parent[2][3]);
            const V local=columns[0]*value.x+columns[1]*value.y+columns[2]*value.z;
            if(!local.finite())return false;
            out[0][column]=local.x;out[1][column]=local.y;out[2][column]=local.z;
        }
        return true;
    };
    double current[3][4],bind[3][4];
    if(!relative(poseMatrices[receiver],poseMatrices[part],current)||
       !relative(c.bind[receiver].m,c.bind[part].m,bind))return false;
    double maximum2=0;bool found=false;
    for(const auto& position:c.studio.positions)if(position.bone==part)
    {
        double distance2=0;
        for(int row=0;row<3;++row)
        {
            double delta=current[row][3]-bind[row][3];
            for(int axis=0;axis<3;++axis)delta+=(current[row][axis]-bind[row][axis])*position.p[axis];
            distance2+=delta*delta;
        }
        if(!std::isfinite(distance2))return false;
        maximum2=std::max(maximum2,distance2);found=true;
    }
    maxDistance=float(std::sqrt(maximum2));
    return found&&std::isfinite(maxDistance);
}

int MountDomainAt(const MountCache& c,float u,float v)
{
    if(!c.valid||!std::isfinite(u)||!std::isfinite(v))return 0;
    if(!c.regions.empty())
    {
        const int region=std::min(int(c.regions.size())-1,int(clamp(u,0,1)*c.regions.size()));
        return MountDomainAt(c.regions[region],float(clamp(u,0,1)*c.regions.size()-region),v);
    }
    const V q(c.minUV[0]+clamp(u,0,1)*(c.maxUV[0]-c.minUV[0]),c.minUV[1]+clamp(v,0,1)*(c.maxUV[1]-c.minUV[1]),0);
    const Triangle* t;V p;double depth;
    if(!frontAt(c,q,t,p,depth))return 0;
    return t->bone>=0?1:0;
}

static bool FindMountImpl(const MountCache& c,float u,float v,Mount& out,const MountViewConstraint* view)
{
    out=Mount();
    if(!c.valid||!std::isfinite(u)||!std::isfinite(v))return false;
    if(!c.regions.empty())
    {
        const int index=std::min(int(c.regions.size())-1,int(clamp(u,0,1)*c.regions.size()));
        if(!FindMountImpl(c.regions[index],float(clamp(u,0,1)*c.regions.size()-index),v,out,view))return false;
        out.requestedUV[0]=u;out.actualUV[0]=(out.actualUV[0]+index)/float(c.regions.size());return true;
    }
    u=float(clamp(u,0,1));v=float(clamp(v,0,1));
    V query(c.minUV[0]+u*(c.maxUV[0]-c.minUV[0]),c.minUV[1]+v*(c.maxUV[1]-c.minUV[1]),0);
    double bestDistance=std::numeric_limits<double>::infinity(),bestDepth=-std::numeric_limits<double>::infinity();
    const Triangle* best=nullptr;V bestPoint,bestProjection;
    for(const Triangle& t:c.surfaces)
    {
        if(t.bone<0||!sideFacing(c,t))continue;
        V p[3]={project(c.side,V(t.p[0])),project(c.side,V(t.p[1])),project(c.side,V(t.p[2]))};
        double denominator=cross2(p[1]-p[0],p[2]-p[0]);
        if(std::abs(denominator)<1e-16)continue;
        V legal[16]={p[0],p[1],p[2]};
        int legalCount=clipRectangle(legal,3,c.minUV[0],c.maxUV[0],c.minUV[1],c.maxUV[1]);
        double cameraDepth[3]={};
        if(view)
        {
            for(int i=0;i<3;++i)
            {
                V local=inverse(c.bind[t.bone].m,V(t.p[i]));
                cameraDepth[i]=(point(view->poseMatrices[t.bone],local)-V(view->viewOrigin)).dot(V(view->viewForward));
            }
            auto cameraDistance=[&](V q)
            {
                double b=cross2(q-p[0],p[2]-p[0])/denominator,d=cross2(p[1]-p[0],q-p[0])/denominator;
                return cameraDepth[0]*(1-b-d)+cameraDepth[1]*b+cameraDepth[2]*d-view->minimumOriginDepth;
            };
            legalCount=clipPlane(legal,legalCount,cameraDistance);
        }
        if(!legalCount)continue;
        double b=cross2(query-p[0],p[2]-p[0])/denominator;
        double d=cross2(p[1]-p[0],query-p[0])/denominator;
        V q;
        if(b>=0&&d>=0&&b+d<=1&&(!view||cameraDepth[0]*(1-b-d)+cameraDepth[1]*b+cameraDepth[2]*d>=view->minimumOriginDepth))q=p[0]*(1-b-d)+p[1]*b+p[2]*d;
        else
        {
            q=closest2(query,legal[legalCount-1],legal[0]);
            for(int i=1;i<legalCount;++i)
            {
                V candidate=closest2(query,legal[i-1],legal[i]);
                if(std::hypot(candidate.x-query.x,candidate.y-query.y)<std::hypot(q.x-query.x,q.y-query.y))q=candidate;
            }
            b=cross2(q-p[0],p[2]-p[0])/denominator;d=cross2(p[1]-p[0],q-p[0])/denominator;
        }
        // Validate the UV that the caller will actually retain after float
        // rounding at a triangle boundary.
        const float actualU=float((q.x-c.minUV[0])/(c.maxUV[0]-c.minUV[0]));
        const float actualV=float((q.y-c.minUV[1])/(c.maxUV[1]-c.minUV[1]));
        q.x=c.minUV[0]+double(actualU)*(c.maxUV[0]-c.minUV[0]);
        q.y=c.minUV[1]+double(actualV)*(c.maxUV[1]-c.minUV[1]);
        double distance=(q.x-query.x)*(q.x-query.x)+(q.y-query.y)*(q.y-query.y);
        if(distance<bestDistance-1e-14||(std::abs(distance-bestDistance)<=1e-14&&q.z>bestDepth))
        {
            const Triangle* selected=&t;V selectedPoint=V(t.p[0])*(1-b-d)+V(t.p[1])*b+V(t.p[2])*d;double selectedDepth=q.z;
            // Moving parts retain their actual bone. A deforming seam cannot
            // carry a rigid anchor or expose a hidden layer beneath it.
            if(!frontAt(c,q,selected,selectedPoint,selectedDepth)||selected->bone<0)continue;
            if(view)
            {
                V local=inverse(c.bind[selected->bone].m,selectedPoint);
                double actual=(point(view->poseMatrices[selected->bone],local)-V(view->viewOrigin)).dot(V(view->viewForward));
                if(actual+1e-5<view->minimumOriginDepth)continue;
            }
            best=selected;bestDistance=distance;bestDepth=selectedDepth;bestProjection=q;bestProjection.z=selectedDepth;bestPoint=selectedPoint;
        }
    }
    if(!best||best->bone<0||size_t(best->bone)>=c.bind.size())return false;
    V n(best->normal),x=V(c.side.axisX)-n*n.dot(V(c.side.axisX));
    if(x.length2()<1e-10)x=V(c.side.axisY).cross(n);
    x=x.unit();V y=n.cross(x).unit();
    const float (*matrix)[4]=c.bind[best->bone].m;
    out.bone=best->bone;inverse(matrix,bestPoint).store(out.origin);
    inverse(matrix,x,false).unit().store(out.axisX);inverse(matrix,y,false).unit().store(out.axisY);inverse(matrix,n,false).unit().store(out.normal);
    out.requestedUV[0]=u;out.requestedUV[1]=v;
    out.actualUV[0]=float((bestProjection.x-c.minUV[0])/(c.maxUV[0]-c.minUV[0]));
    out.actualUV[1]=float((bestProjection.y-c.minUV[1])/(c.maxUV[1]-c.minUV[1]));
    out.snapDistance=float(std::sqrt(bestDistance));return true;
}
bool FindMount(const MountCache& c,float u,float v,Mount& out) { return FindMountImpl(c,u,v,out,nullptr); }
bool FindMountInFront(const MountCache& c,float u,float v,const MountViewConstraint& view,Mount& out)
{
    if(!view.poseMatrices||!V(view.viewOrigin).finite()||!V(view.viewForward).finite()||!std::isfinite(view.minimumOriginDepth))return false;
    return FindMountImpl(c,u,v,out,&view);
}

bool FindMountCandidates(const MountCache& c,float u,float v,std::vector<Mount>& out,
                         unsigned maximumCandidates,const MountViewConstraint* view)
{
    out.clear();
    if(!c.valid||!std::isfinite(u)||!std::isfinite(v)||!maximumCandidates||
       (view&&(!view->poseMatrices||!V(view->viewOrigin).finite()||!V(view->viewForward).finite()||
               !std::isfinite(view->minimumOriginDepth))))return false;
    if(!c.regions.empty())
    {
        const int index=std::min(int(c.regions.size())-1,int(clamp(u,0,1)*c.regions.size()));
        if(!FindMountCandidates(c.regions[index],float(clamp(u,0,1)*c.regions.size()-index),v,out,maximumCandidates,view))return false;
        for(auto& mount:out){mount.requestedUV[0]=u;mount.actualUV[0]=(mount.actualUV[0]+index)/float(c.regions.size());}
        return true;
    }
    u=float(clamp(u,0,1));v=float(clamp(v,0,1));
    const double width=c.maxUV[0]-c.minUV[0],height=c.maxUV[1]-c.minUV[1];
    V query(c.minUV[0]+u*width,c.minUV[1]+v*height,0);
    struct Candidate { V q; double distance; };
    std::vector<Candidate> candidates;candidates.reserve(c.surfaces.size()*8);
    auto add=[&](V q)
    {
        const double x=(q.x-query.x)/width,y=(q.y-query.y)/height;
        candidates.push_back({q,x*x+y*y});
    };
    for(const Triangle& t:c.surfaces)
    {
        if(t.bone<0||!sideFacing(c,t))continue;
        V p[3]={project(c.side,V(t.p[0])),project(c.side,V(t.p[1])),project(c.side,V(t.p[2]))};
        double den=cross2(p[1]-p[0],p[2]-p[0]);if(std::abs(den)<1e-16)continue;
        V legal[16]={p[0],p[1],p[2]};int count=clipRectangle(legal,3,c.minUV[0],c.maxUV[0],c.minUV[1],c.maxUV[1]);
        double cameraDepth[3]={};
        if(view)
        {
            for(int i=0;i<3;++i)
                cameraDepth[i]=(point(view->poseMatrices[t.bone],inverse(c.bind[t.bone].m,V(t.p[i])))-V(view->viewOrigin)).dot(V(view->viewForward));
            count=clipPlane(legal,count,[&](V q)
            {
                double b=cross2(q-p[0],p[2]-p[0])/den,d=cross2(p[1]-p[0],q-p[0])/den;
                return cameraDepth[0]*(1-b-d)+cameraDepth[1]*b+cameraDepth[2]*d-view->minimumOriginDepth;
            });
        }
        if(!count)continue;
        const double b=cross2(query-p[0],p[2]-p[0])/den,d=cross2(p[1]-p[0],query-p[0])/den;
        if(b>=0&&d>=0&&b+d<=1&&(!view||cameraDepth[0]*(1-b-d)+cameraDepth[1]*b+cameraDepth[2]*d>=view->minimumOriginDepth))
            add(p[0]*(1-b-d)+p[1]*b+p[2]*d);
        V center;
        for(int i=0;i<count;++i)
        {
            V a=legal[i],next=legal[(i+1)%count];center=center+a;
            add(a);add((a+next)*.5);add(closest2(query,a,next));
        }
        add(center*(1.0/count));
    }
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b)
    {
        if(a.distance!=b.distance)return a.distance<b.distance;
        if(a.q.x!=b.q.x)return a.q.x<b.q.x;
        if(a.q.y!=b.q.y)return a.q.y<b.q.y;
        return a.q.z>b.q.z;
    });
    std::set<std::pair<int,int>> visited,occupied;
    for(const Candidate& candidate:candidates)
    {
        V q=candidate.q;
        const float actualU=float(clamp((q.x-c.minUV[0])/width,0,1));
        const float actualV=float(clamp((q.y-c.minUV[1])/height,0,1));
        const auto cell=std::make_pair(std::min(31,int(actualU*32)),std::min(31,int(actualV*32)));
        // A short fallback list must cover the receiver, rather than filling
        // all entries with adjacent vertices of one dense detail.
        if(maximumCandidates<=128&&occupied.count(cell))continue;
        q.x=c.minUV[0]+double(actualU)*width;q.y=c.minUV[1]+double(actualV)*height;
        const int ux=int(std::llround((q.x-c.minUV[0])/width*1000000));
        const int vy=int(std::llround((q.y-c.minUV[1])/height*1000000));
        if(!visited.insert(std::make_pair(ux,vy)).second)continue;
        const Triangle* t;V selected;double depth;
        if(!frontAt(c,q,t,selected,depth)||!t||t->bone<0||size_t(t->bone)>=c.bind.size())continue;
        V local=inverse(c.bind[t->bone].m,selected);
        if(view&&(point(view->poseMatrices[t->bone],local)-V(view->viewOrigin)).dot(V(view->viewForward))+1e-5<view->minimumOriginDepth)continue;
        V n(t->normal),x=V(c.side.axisX)-n*n.dot(V(c.side.axisX));
        if(x.length2()<1e-10)x=V(c.side.axisY).cross(n);
        x=x.unit();V y=n.cross(x).unit();
        Mount mount;mount.bone=t->bone;local.store(mount.origin);
        inverse(c.bind[t->bone].m,x,false).unit().store(mount.axisX);
        inverse(c.bind[t->bone].m,y,false).unit().store(mount.axisY);
        inverse(c.bind[t->bone].m,n,false).unit().store(mount.normal);
        mount.requestedUV[0]=u;mount.requestedUV[1]=v;
        mount.actualUV[0]=actualU;mount.actualUV[1]=actualV;
        mount.snapDistance=float(std::hypot(q.x-query.x,q.y-query.y));out.push_back(mount);
        occupied.insert(cell);
        if(out.size()>=maximumCandidates)break;
    }
    return !out.empty();
}

float ComputeOutwardClearance(const Frame& frame,const Bounds& bounds,const std::vector<Triangle>& triangles,float gap,float minimumOffset)
{
    if(!V(frame.origin).finite()||!V(frame.axisX).finite()||!V(frame.axisY).finite()||!V(frame.normal).finite())return 0;
    for(int i=0;i<3;++i)if(!std::isfinite(bounds.min[i])||!std::isfinite(bounds.max[i])||bounds.max[i]<bounds.min[i])return 0;
    struct Interval { double low,high; };
    std::vector<Interval> intervals;intervals.reserve(triangles.size());
    for(const Triangle& triangle:triangles)
    {
        V polygon[16];
        bool valid=true;
        for(int i=0;i<3;++i){polygon[i]=project(frame,V(triangle.p[i]));valid&=polygon[i].finite();}
        if(!valid)continue;
        int count=clipRectangle(polygon,3,bounds.min[0],bounds.max[0],bounds.min[1],bounds.max[1]);
        if(count)
        {
            Interval interval={polygon[0].z,polygon[0].z};
            for(int i=1;i<count;++i){interval.low=std::min(interval.low,polygon[i].z);interval.high=std::max(interval.high,polygon[i].z);}
            intervals.push_back(interval);
        }
    }
    const double clearance=std::max(.02,std::isfinite(gap)?double(gap):.02);
    double required=std::max(0.0,clearance-bounds.min[2]);
    if(std::isfinite(minimumOffset))required=std::max(required,double(minimumOffset));
    std::sort(intervals.begin(),intervals.end(),[](const Interval& a,const Interval& b)
              {return a.low<b.low||(a.low==b.low&&a.high<b.high);});
    // Only intersections with the current finite Z volume can block a move.
    // After moving outwards, an interval already below the volume stays below;
    // sorted lower endpoints therefore give the complete propagation in one
    // bounded scan. An unconnected distant forearm remains an occluder, not a
    // request to move the accessory in front of that forearm.
    for(const Interval& interval:intervals)
    {
        if(interval.low>bounds.max[2]+required+clearance)break;
        if(interval.high<bounds.min[2]+required-clearance)continue;
        required=std::max(required,interval.high+clearance-bounds.min[2]);
    }
    return required>0?std::nextafter(float(required),std::numeric_limits<float>::infinity()):0;
}

namespace
{
std::uint64_t colliderSerial=0;
Bounds emptyBounds()
{
    Bounds b;for(int i=0;i<3;++i){b.min[i]=std::numeric_limits<float>::infinity();b.max[i]=-b.min[i];}return b;
}
void extend(Bounds& a,const Bounds& b)
{
    for(int i=0;i<3;++i){a.min[i]=std::min(a.min[i],b.min[i]);a.max[i]=std::max(a.max[i],b.max[i]);}
}
Bounds triangleBounds(const Triangle& t)
{
    Bounds b=emptyBounds();
    for(int j=0;j<3;++j)for(int i=0;i<3;++i){b.min[i]=std::min(b.min[i],t.p[j][i]);b.max[i]=std::max(b.max[i],t.p[j][i]);}return b;
}
bool overlap(const Bounds& a,const Bounds& b,double gap)
{
    for(int i=0;i<3;++i)if(double(a.max[i])+gap<b.min[i]||double(b.max[i])+gap<a.min[i])return false;return true;
}
int collisionNode(MeshCollider& c,int first,int count)
{
    const int index=int(c.nodes.size());c.nodes.push_back(CollisionNode());
    Bounds bounds=emptyBounds();for(int i=first;i<first+count;++i)extend(bounds,c.triangleBounds[c.order[i]]);
    c.nodes[index].bounds=bounds;
    if(count<=8){c.nodes[index].first=first;c.nodes[index].count=count;return index;}
    int axis=0;for(int i=1;i<3;++i)if(bounds.max[i]-bounds.min[i]>bounds.max[axis]-bounds.min[axis])axis=i;
    const int half=first+count/2;
    std::nth_element(c.order.begin()+first,c.order.begin()+half,c.order.begin()+first+count,[&](int a,int b)
    {
        const Triangle& x=c.triangles[a];const Triangle& y=c.triangles[b];
        const float u=x.p[0][axis]+x.p[1][axis]+x.p[2][axis],v=y.p[0][axis]+y.p[1][axis]+y.p[2][axis];
        return u<v||(u==v&&a<b);
    });
    const int left=collisionNode(c,first,half-first),right=collisionNode(c,half,first+count-half);
    c.nodes[index].left=left;c.nodes[index].right=right;return index;
}
void collisionGroups(MeshCollider& c)
{
    using Key=std::array<float,3>;using Edge=std::pair<Key,Key>;
    struct Ref { int triangle,corner;bool reverse; };
    std::map<int,std::map<Edge,std::vector<Ref>>> edges;
    std::map<int,int> groupIndex;
    c.groups.clear();c.closedEdges.clear();
    c.triangleGroups.resize(c.triangles.size());
    for(size_t index=0;index<c.triangles.size();++index)
    {
        const Triangle& t=c.triangles[index];
        if(!groupIndex.count(t.group))
        {
            groupIndex[t.group]=int(c.groups.size());CollisionGroup g;g.id=t.group;g.bounds=emptyBounds();c.groups.push_back(g);
        }
        c.triangleGroups[index]=groupIndex[t.group];
        extend(c.groups[c.triangleGroups[index]].bounds,c.triangleBounds[index]);
        if((V(t.p[1])-V(t.p[0])).cross(V(t.p[2])-V(t.p[0])).length2()<1e-24)continue;
        for(int j=0;j<3;++j)
        {
            Key a={{t.p[j][0],t.p[j][1],t.p[j][2]}},b={{t.p[(j+1)%3][0],t.p[(j+1)%3][1],t.p[(j+1)%3][2]}};
            bool reverse=b<a;if(reverse)std::swap(a,b);
            edges[t.group][std::make_pair(a,b)].push_back({int(index),j,reverse});
        }
    }
    for(auto& item:groupIndex)
    {
        const int group=item.second;bool closed=!edges[item.first].empty();
        for(const auto& edge:edges[item.first])if(edge.second.size()!=2){closed=false;break;}
        c.groups[group].closed=c.groups[group].topologyClosed=closed;
        if(closed)for(const auto& edge:edges[item.first])
        {
            const Ref a=edge.second[0],b=edge.second[1];
            c.closedEdges.push_back({group,a.triangle,a.corner,b.triangle,b.corner,a.reverse!=b.reverse});
        }
    }
}
double segmentDistance2(V p,V q,V a,V b)
{
    V u=q-p,v=b-a,w=p-a;double A=u.dot(u),B=u.dot(v),C=v.dot(v),D=u.dot(w),E=v.dot(w),s=0,t=0;
    if(A<=1e-28&&C<=1e-28)return w.length2();
    if(A<=1e-28)t=clamp(E/C,0,1);
    else if(C<=1e-28)s=clamp(-D/A,0,1);
    else
    {
        double den=A*C-B*B;s=den>1e-28?clamp((B*E-C*D)/den,0,1):0;t=(B*s+E)/C;
        if(t<0){t=0;s=clamp(-D/A,0,1);}else if(t>1){t=1;s=clamp((B-D)/A,0,1);}
    }
    return (w+u*s-v*t).length2();
}
double pointTriangleDistance2(V p,const Triangle& t)
{
    V a(t.p[0]),b(t.p[1]),c(t.p[2]),ab=b-a,ac=c-a,n=ab.cross(ac);
    double best=std::min(segmentDistance2(p,p,a,b),std::min(segmentDistance2(p,p,b,c),segmentDistance2(p,p,c,a)));
    double nn=n.length2();if(nn<1e-28)return best;
    V q=p-n*((p-a).dot(n)/nn),ap=q-a;
    const double aa=ab.dot(ab),bb=ab.dot(ac),cc=ac.dot(ac),d=ap.dot(ab),e=ap.dot(ac),den=aa*cc-bb*bb;
    if(den<=1e-28)return best;
    const double u=(d*cc-e*bb)/den,v=(e*aa-d*bb)/den;
    if(u>=-1e-12&&v>=-1e-12&&u+v<=1+1e-12)best=std::min(best,(p-q).length2());return best;
}
bool segmentTriangle(V a,V b,const Triangle& t)
{
    V p(t.p[0]),u=V(t.p[1])-p,v=V(t.p[2])-p,n=u.cross(v),direction=b-a;
    double den=direction.dot(n);if(std::abs(den)<1e-24)return false;
    double alpha=(p-a).dot(n)/den;if(alpha<0||alpha>1)return false;
    return pointTriangleDistance2(a+direction*alpha,t)<1e-20;
}
// A triangle is contained in its supporting plane. If every point of the
// other triangle is farther than gap on one side, convexity proves the two
// finite triangles cannot contact. Error bounds make uncertain/thin cases
// fall through to the complete distance test instead of treating roundoff as
// a separating plane. Inputs are float positions, arithmetic here is double.
CollisionPlane makeCollisionPlane(const Triangle& a)
{
    CollisionPlane plane;std::copy(a.p[0],a.p[0]+3,plane.origin);
    const V origin(a.p[0]),u=V(a.p[1])-origin,v=V(a.p[2])-origin,n=u.cross(v);
    const double normal[3]={n.x,n.y,n.z},edgeU[3]={u.x,u.y,u.z},edgeV[3]={v.x,v.y,v.z};
    const double eps=std::numeric_limits<double>::epsilon();
    const double length=std::sqrt(n.length2());if(length<=1e-20||!std::isfinite(length))return plane;
    double errorU[3],errorV[3],errorN[3];
    for(int i=0;i<3;++i)
    {
        errorU[i]=eps*(std::abs(double(a.p[1][i]))+std::abs(double(a.p[0][i])));
        errorV[i]=eps*(std::abs(double(a.p[2][i]))+std::abs(double(a.p[0][i])));
    }
    double normalError=0;
    for(int i=0;i<3;++i)
    {
        const int j=(i+1)%3,k=(i+2)%3;
        errorN[i]=16*eps*(std::abs(edgeU[j]*edgeV[k])+std::abs(edgeU[k]*edgeV[j]))+
            4*(std::abs(edgeU[j])*errorV[k]+std::abs(edgeV[k])*errorU[j]+errorU[j]*errorV[k]+
               std::abs(edgeU[k])*errorV[j]+std::abs(edgeV[j])*errorU[k]+errorU[k]*errorV[j]);
        normalError+=errorN[i];
    }
    std::copy(normal,normal+3,plane.normal);std::copy(errorN,errorN+3,plane.errorN);
    plane.marginLength=length+normalError+8*eps*length;plane.valid=true;return plane;
}
bool planeSeparates(const CollisionPlane& plane,const Triangle& b,double gap)
{
    if(!plane.valid)return false;
    const auto& normal=plane.normal;const auto& errorN=plane.errorN;
    const double eps=std::numeric_limits<double>::epsilon();
    const double threshold=std::max(gap,1e-10)*plane.marginLength;
    bool above=true,below=true;
    for(int corner=0;corner<3;++corner)
    {
        double distance=0,error=0;
        for(int i=0;i<3;++i)
        {
            const double relative=double(b.p[corner][i])-plane.origin[i];
            const double relativeError=eps*(std::abs(double(b.p[corner][i]))+std::abs(double(plane.origin[i])));
            const double term=relative*normal[i];distance+=term;
            error+=8*eps*std::abs(term)+std::abs(normal[i])*relativeError+
                   std::abs(relative)*errorN[i]+relativeError*errorN[i];
        }
        above&=distance>threshold+error;below&=distance< -threshold-error;
        if(!above&&!below)return false;
    }
    return above||below;
}
bool additionalAxisSeparates(const Triangle& a,const Triangle& b,double gap)
{
    // Any axis is a valid separation witness. Computed edge-cross axes need
    // not equal an ideal exact cross product: both actual triangles are
    // explicitly projected onto the same chosen double-precision axis.
    V p[6],error[6];
    const double eps=std::numeric_limits<double>::epsilon();
    for(int vertex=0;vertex<6;++vertex)
    {
        const float* q=vertex<3?a.p[vertex]:b.p[vertex-3];p[vertex]=V(q)-V(a.p[0]);
        error[vertex]=V(eps*(std::abs(double(q[0]))+std::abs(double(a.p[0][0]))),
                        eps*(std::abs(double(q[1]))+std::abs(double(a.p[0][1]))),
                        eps*(std::abs(double(q[2]))+std::abs(double(a.p[0][2]))));
    }
    auto separated=[&](V axis)
    {
        const double length2=axis.length2();if(length2<1e-40||!std::isfinite(length2))return false;
        double low[2]={},high[2]={},uncertainty[2]={};
        for(int vertex=0;vertex<6;++vertex)
        {
            const double x=p[vertex].x*axis.x,y=p[vertex].y*axis.y,z=p[vertex].z*axis.z;
            const double projected=x+y+z;
            const double bound=8*eps*(std::abs(x)+std::abs(y)+std::abs(z))+
                std::abs(axis.x)*error[vertex].x+std::abs(axis.y)*error[vertex].y+std::abs(axis.z)*error[vertex].z;
            const int triangle=vertex/3;
            if(vertex%3==0)low[triangle]=high[triangle]=projected;
            else{low[triangle]=std::min(low[triangle],projected);high[triangle]=std::max(high[triangle],projected);}
            uncertainty[triangle]=std::max(uncertainty[triangle],bound);
        }
        const double separation=std::max(low[1]-high[0],low[0]-high[1])-uncertainty[0]-uncertainty[1];
        const double margin=std::max(gap,1e-10);
        return separation>0&&separation*separation>margin*margin*length2*(1+32*eps);
    };
    const V edgeA[3]={p[1]-p[0],p[2]-p[1],p[0]-p[2]};
    const V edgeB[3]={p[4]-p[3],p[5]-p[4],p[3]-p[5]};
    const V normalA=edgeA[0].cross(edgeA[1]),normalB=edgeB[0].cross(edgeB[1]);
    // Parallel normals need in-plane axes: all edge-cross axes would otherwise
    // repeat the supporting plane. These axes are also safe when nearly parallel.
    if(normalA.cross(normalB).length2()<=1e-8*normalA.length2()*normalB.length2())
        for(int i=0;i<3;++i)if(separated(normalA.cross(edgeA[i]))||separated(normalB.cross(edgeB[i])))return true;
    for(V x:edgeA)for(V y:edgeB)if(separated(x.cross(y)))return true;
    return false;
}
double triangleDistance2(const Triangle& a,const Triangle& b)
{
    double best=std::numeric_limits<double>::infinity();
    for(int i=0;i<3;++i)
    {
        if(segmentTriangle(V(a.p[i]),V(a.p[(i+1)%3]),b)||segmentTriangle(V(b.p[i]),V(b.p[(i+1)%3]),a))return 0;
        best=std::min(best,std::min(pointTriangleDistance2(V(a.p[i]),b),pointTriangleDistance2(V(b.p[i]),a)));
        for(int j=0;j<3;++j)best=std::min(best,segmentDistance2(V(a.p[i]),V(a.p[(i+1)%3]),V(b.p[j]),V(b.p[(j+1)%3])));
    }
    return best;
}
bool rayBounds(V p,V d,const Bounds& b)
{
    const double q[3]={p.x,p.y,p.z},direction[3]={d.x,d.y,d.z};double lo=0,hi=std::numeric_limits<double>::infinity();
    for(int i=0;i<3;++i)
    {
        if(std::abs(direction[i])<1e-20){if(q[i]<b.min[i]||q[i]>b.max[i])return false;continue;}
        double a=(b.min[i]-q[i])/direction[i],c=(b.max[i]-q[i])/direction[i];if(a>c)std::swap(a,c);
        lo=std::max(lo,a);hi=std::min(hi,c);if(hi<lo)return false;
    }
    return true;
}
bool rayTriangle(V p,V d,const Triangle& t,double& distance)
{
    V a(t.p[0]),u=V(t.p[1])-a,v=V(t.p[2])-a,h=d.cross(v);double det=u.dot(h);
    if(std::abs(det)<1e-20)return false;
    V s=p-a;double b=s.dot(h)/det;if(b< -1e-10||b>1+1e-10)return false;
    V q=s.cross(u);double c=d.dot(q)/det;if(c< -1e-10||b+c>1+1e-10)return false;
    distance=v.dot(q)/det;return distance>1e-7;
}
bool insideGroup(const MeshCollider& c,V p,int group)
{
    const V directions[3]={V(1,.371390676,.137128534).unit(),V(.193723137,1,.417731191).unit(),V(.319172413,.241391977,1).unit()};
    int votes=0;
    for(V direction:directions)
    {
        std::vector<int> stack(1,0);std::vector<double> hits;
        while(!stack.empty())
        {
            const CollisionNode& node=c.nodes[stack.back()];stack.pop_back();if(!rayBounds(p,direction,node.bounds))continue;
            if(node.count)for(int i=node.first;i<node.first+node.count;++i)
            {
                const Triangle& t=c.triangles[c.order[i]];double distance;
                if(t.group==group&&rayTriangle(p,direction,t,distance))hits.push_back(distance);
            }
            else{stack.push_back(node.left);stack.push_back(node.right);}
        }
        std::sort(hits.begin(),hits.end());int crossings=0;double previous=-1;
        for(double distance:hits)if(previous<0||distance-previous>1e-7*std::max(1.0,std::abs(distance)))
            {++crossings;previous=distance;}
        if(crossings%2)++votes;
    }
    return votes>=2;
}
}

bool BuildMeshCollider(const std::vector<Triangle>& triangles,MeshCollider& out,bool closedGroups)
{
    out=MeshCollider();out.surfaceOnly=!closedGroups;
    for(const Triangle& t:triangles)for(int j=0;j<3;++j)if(!V(t.p[j]).finite())return false;
    out.triangles=triangles;out.order.resize(triangles.size());out.planes.resize(triangles.size());
    out.triangleBounds.reserve(triangles.size());for(const auto& t:triangles)out.triangleBounds.push_back(triangleBounds(t));
    for(size_t i=0;i<triangles.size();++i)out.order[i]=int(i);
    if(!triangles.empty()){out.nodes.reserve(triangles.size()*2);collisionNode(out,0,int(triangles.size()));if(closedGroups)collisionGroups(out);}
    out.geometrySerial=++colliderSerial;return true;
}
bool RefitMeshCollider(const std::vector<Triangle>& triangles,MeshCollider& c,bool closedGroups)
{
    const bool initialiseGroups=c.surfaceOnly&&closedGroups;
    if((c.surfaceOnly!=!closedGroups&&!initialiseGroups)||triangles.size()!=c.triangles.size()||(!triangles.empty()&&c.nodes.empty()))return BuildMeshCollider(triangles,c,closedGroups);
    for(size_t i=0;i<triangles.size();++i)
    {
        if(triangles[i].group!=c.triangles[i].group)return BuildMeshCollider(triangles,c,closedGroups);
        for(int j=0;j<3;++j)if(!V(triangles[i].p[j]).finite())return false;
    }
    c.triangles=triangles;
    if(++c.planeEpoch==0){c.planeEpoch=1;for(auto& plane:c.planes)plane.epoch=0;}
    for(size_t i=0;i<triangles.size();++i)c.triangleBounds[i]=triangleBounds(triangles[i]);
    for(int index=int(c.nodes.size())-1;index>=0;--index)
    {
        CollisionNode& node=c.nodes[index];Bounds rebuilt=emptyBounds();
        if(node.count)for(int i=node.first;i<node.first+node.count;++i)extend(rebuilt,c.triangleBounds[c.order[i]]);
        else{extend(rebuilt,c.nodes[node.left].bounds);extend(rebuilt,c.nodes[node.right].bounds);}
        node.bounds=rebuilt;
    }
    if(!closedGroups){c.geometrySerial=++colliderSerial;return true;}
    // Preheated BVH nodes are safe search structure only. Resolve closed
    // volumes from the first real pose, never from the authored bind pose.
    if(initialiseGroups){c.surfaceOnly=false;collisionGroups(c);}
    for(CollisionGroup& g:c.groups){g.bounds=emptyBounds();g.closed=g.topologyClosed;}
    for(size_t i=0;i<c.triangles.size();++i)extend(c.groups[c.triangleGroups[i]].bounds,c.triangleBounds[i]);
    for(const CollisionEdgePair& edge:c.closedEdges)
    {
        CollisionGroup& group=c.groups[edge.group];if(!group.closed)continue;
        const Triangle& a=c.triangles[edge.triangleA];const Triangle& b=c.triangles[edge.triangleB];
        for(int i=0;i<2;++i)
        {
            int ai=(edge.cornerA+i)%3,bi=(edge.cornerB+(edge.reverse?1-i:i))%3;
            if((V(a.p[ai])-V(b.p[bi])).length2()>1e-12)group.closed=false;
        }
    }
    c.geometrySerial=++colliderSerial;return true;
}
namespace
{
CollisionResult finishCollision(const MeshCollider& c,const std::vector<Triangle>& accessory,CollisionResult result)
{
    // A pendant, chain and mounting foot can be disconnected mesh components.
    // Global extrema can miss a small component wholly inside a closed shell.
    // Test their actual vertices; open obstacle groups claim no interior.
    if(!result.closedGroups)return result;
    for(const CollisionGroup& group:c.groups)if(group.closed)
    for(const Triangle& triangle:accessory)for(const auto& sample:triangle.p)
    {
        const V p(sample);
        const double q[3]={p.x,p.y,p.z};bool contained=true;
        for(int i=0;i<3;++i)contained&=q[i]>group.bounds.min[i]&&q[i]<group.bounds.max[i];
        if(!contained)continue;
        ++result.containmentSamples;
        if(insideGroup(c,p,group.id)){result.blocked=result.contained=true;result.containingGroup=group.id;return result;}
    }
    return result;
}
}
CollisionResult CheckMeshCollision(const MeshCollider& c,const std::vector<Triangle>& accessory,float minimumSeparation)
{
    CollisionResult result;
    const double gap=std::isfinite(minimumSeparation)?std::max(0.0,double(minimumSeparation)):.0001;
    for(const CollisionGroup& group:c.groups)if(group.closed)++result.closedGroups;
    if(c.nodes.empty()||accessory.empty())return result;
    std::vector<int> stack;stack.reserve(64);
    for(size_t index=0;index<accessory.size();++index)
    {
        const Triangle& t=accessory[index];
        for(int j=0;j<3;++j)if(!V(t.p[j]).finite()){result.blocked=true;result.accessoryTriangle=int(index);return result;}
        const Bounds bounds=triangleBounds(t);CollisionPlane queryPlane;bool planeReady=false;stack.clear();stack.push_back(0);
        while(!stack.empty())
        {
            const CollisionNode& node=c.nodes[stack.back()];stack.pop_back();if(!overlap(bounds,node.bounds,gap))continue;
            if(node.count)for(int i=node.first;i<node.first+node.count;++i)
            {
                const int obstacle=c.order[i];const Triangle& other=c.triangles[obstacle];
                if(!overlap(bounds,c.triangleBounds[obstacle],gap))continue;
                ++result.trianglePairs;
                if(!planeReady){queryPlane=makeCollisionPlane(t);planeReady=true;}
                if(planeSeparates(queryPlane,other,gap)){++result.planeSeparatedPairs;continue;}
                auto& obstaclePlane=c.planes[obstacle];
                if(obstaclePlane.epoch!=c.planeEpoch){obstaclePlane=makeCollisionPlane(other);obstaclePlane.epoch=c.planeEpoch;}
                if(planeSeparates(obstaclePlane,t,gap)){++result.planeSeparatedPairs;continue;}
                if(additionalAxisSeparates(t,other,gap)){++result.axisSeparatedPairs;continue;}
                ++result.distancePairs;const double distance=triangleDistance2(t,other);
                if(distance<=std::max(1e-20,gap*gap))
                {
                    result.blocked=result.surfaceContact=true;result.accessoryTriangle=int(index);result.obstacleTriangle=obstacle;
                    result.distance=float(std::sqrt(std::max(0.0,distance)));return result;
                }
            }
            else{stack.push_back(node.left);stack.push_back(node.right);}
        }
    }
    return finishCollision(c,accessory,result);
}

CollisionResult CheckMeshCollision(const MeshCollider& c,const MeshCollider& query,float minimumSeparation)
{
    CollisionResult result;for(const auto& group:c.groups)if(group.closed)++result.closedGroups;
    if(c.nodes.empty()||query.nodes.empty())return result;
    const double gap=std::isfinite(minimumSeparation)?std::max(0.0,double(minimumSeparation)):.0001;
    auto contact=[&](int obstacle,int index)
    {
        if(!overlap(query.triangleBounds[index],c.triangleBounds[obstacle],gap))return false;
        const auto& t=query.triangles[index];const auto& other=c.triangles[obstacle];++result.trianglePairs;
        auto& queryPlane=query.planes[index];if(queryPlane.epoch!=query.planeEpoch){queryPlane=makeCollisionPlane(t);queryPlane.epoch=query.planeEpoch;}
        if(planeSeparates(queryPlane,other,gap)){++result.planeSeparatedPairs;return false;}
        auto& obstaclePlane=c.planes[obstacle];if(obstaclePlane.epoch!=c.planeEpoch){obstaclePlane=makeCollisionPlane(other);obstaclePlane.epoch=c.planeEpoch;}
        if(planeSeparates(obstaclePlane,t,gap)){++result.planeSeparatedPairs;return false;}
        if(additionalAxisSeparates(t,other,gap)){++result.axisSeparatedPairs;return false;}
        ++result.distancePairs;const double distance=triangleDistance2(t,other);
        if(!(distance<=std::max(1e-20,gap*gap)))return false;
        result.blocked=result.surfaceContact=true;result.accessoryTriangle=index;result.obstacleTriangle=obstacle;
        result.distance=float(std::sqrt(std::max(0.0,distance)));
        query.contactObstacle=obstacle;query.contactAccessory=index;return true;
    };
    if(query.contactObstacle>=0&&size_t(query.contactObstacle)<c.triangles.size()&&
       query.contactAccessory>=0&&size_t(query.contactAccessory)<query.triangles.size()&&
       contact(query.contactObstacle,query.contactAccessory))return result;
    std::vector<std::pair<int,int>> stack;stack.reserve(64);stack.push_back({0,0});
    while(!stack.empty())
    {
        const auto pair=stack.back();stack.pop_back();const auto& a=c.nodes[pair.first];const auto& b=query.nodes[pair.second];
        if(!overlap(a.bounds,b.bounds,gap))continue;
        if(a.count&&b.count)
        {
            for(int j=b.first;j<b.first+b.count;++j)for(int i=a.first;i<a.first+a.count;++i)
            {
                const int index=query.order[j],obstacle=c.order[i];
                if(contact(obstacle,index))return result;
            }
        }
        else
        {
            auto area=[](const Bounds& bounds){const double x=double(bounds.max[0])-bounds.min[0],y=double(bounds.max[1])-bounds.min[1],z=double(bounds.max[2])-bounds.min[2];return x*y+y*z+z*x;};
            if(!a.count&&(b.count||area(a.bounds)>=area(b.bounds)))
            {stack.push_back({a.left,pair.second});stack.push_back({a.right,pair.second});}
            else{stack.push_back({pair.first,b.left});stack.push_back({pair.first,b.right});}
        }
    }
    return finishCollision(c,query.triangles,result);
}

bool ReuseMeshCollision(const MeshCollider& c,const std::vector<Triangle>& addon,const CollisionMemo& memo,CollisionResult& result,float minimumSeparation,const std::vector<int>* uniqueCorners)
{
    const auto& old=memo.obstacles;
    const bool unchanged=memo.owner==&c&&c.geometrySerial!=0&&memo.geometrySerial==c.geometrySerial;
    if(!memo.valid||!std::isfinite(memo.separation)||!std::isfinite(minimumSeparation)||minimumSeparation<0||memo.separation<=minimumSeparation||addon.empty()||addon.size()!=memo.accessory.size()||(!unchanged&&(c.triangles.size()!=old.triangles.size()||
       c.nodes.size()!=old.nodes.size()||c.groups.size()!=old.groups.size()||c.order!=old.order)))return false;
    if(!unchanged)for(size_t i=0;i<c.groups.size();++i)
        if(c.groups[i].id!=old.groups[i].id||c.groups[i].closed!=old.groups[i].closed)return false;
    if(!unchanged)for(size_t i=0;i<c.triangles.size();++i)if(c.triangles[i].group!=old.triangles[i].group)return false;
    const double queryBudget=double(memo.separation)-minimumSeparation-.00001;
    if(queryBudget<=0)return false;
    double motion2=0;Bounds swept=memo.bounds;
    auto inspect=[&](int index)
    {
        const size_t i=size_t(index)/3;const int j=index%3;
        if(index<0||i>=addon.size())return false;
        const V point(addon[i].p[j]);if(!point.finite())return false;
        motion2=std::max(motion2,(point-V(memo.accessory[i].p[j])).length2());
        if(motion2>=queryBudget*queryBudget)return false;
        if(!unchanged)for(int axis=0;axis<3;++axis)
        {swept.min[axis]=std::min(swept.min[axis],addon[i].p[j][axis]);swept.max[axis]=std::max(swept.max[axis],addon[i].p[j][axis]);}
        return true;
    };
    if(uniqueCorners)
    {
        if(uniqueCorners->empty())return false;
        for(int index:*uniqueCorners)if(!inspect(index))return false;
    }
    else for(size_t i=0;i<addon.size();++i)for(int j=0;j<3;++j)if(!inspect(int(i*3)+j))return false;
    if(unchanged)
    {result=CollisionResult();for(const auto& group:c.groups)if(group.closed)++result.closedGroups;return true;}
    // .00001 absorbs roundoff in displacement/bounds; the original certified
    // .001 gap minus both Hausdorff displacement bounds still exceeds .0001.
    const double remaining=double(memo.separation)-minimumSeparation-.00001-std::sqrt(motion2);
    if(remaining<=0)return false;
    std::vector<int> stack;if(!c.nodes.empty())stack.push_back(0);
    while(!stack.empty())
    {
        const int index=stack.back();stack.pop_back();
        const CollisionNode& node=c.nodes[index];const CollisionNode& before=old.nodes[index];
        if(node.count!=before.count||node.first!=before.first||node.left!=before.left||node.right!=before.right)return false;
        Bounds envelope=node.bounds;extend(envelope,before.bounds);
        // The union AABB contains the entire linear sweep of this subtree.
        // A disjoint subtree cannot cross the accessory, even if a distant arm
        // moves much farther than the local clearance budget.
        if(!overlap(envelope,swept,minimumSeparation+.00001))continue;
        if(node.count)for(int i=node.first;i<node.first+node.count;++i)
        {
            const int triangle=c.order[i];
            for(int j=0;j<3;++j)
                if((V(c.triangles[triangle].p[j])-V(old.triangles[triangle].p[j])).length2()>=remaining*remaining)return false;
        }
        else{stack.push_back(node.left);stack.push_back(node.right);}
    }
    result=CollisionResult();for(const auto& group:c.groups)if(group.closed)++result.closedGroups;
    return true;
}
void RememberMeshCollision(const MeshCollider& c,const std::vector<Triangle>& addon,CollisionMemo& memo,float separation)
{
    memo.obstacles=c;memo.accessory=addon;memo.bounds=emptyBounds();
    for(const auto& triangle:addon)extend(memo.bounds,triangleBounds(triangle));
    memo.owner=&c;memo.geometrySerial=c.geometrySerial;
    memo.separation=separation;memo.valid=!addon.empty()&&std::isfinite(separation)&&separation>0;
}

bool BuildPoseGeometry(const StudioTriangleCache& mesh,int body,int skin,PoseGeometry& out,bool collectCorners,const std::vector<int>* allowedBones)
{
    out=PoseGeometry();if(!mesh.header)return false;
    std::map<int,Bounds> bounds;std::set<std::pair<int,int>> links;std::vector<bool> seen;if(collectCorners)seen.resize(mesh.positions.size());
    for(size_t i=0;i<mesh.triangles.size();++i)
    {
        const auto& t=mesh.triangles[i];if(!sameActive(mesh,t,body)||!real(mesh,t,skin)||!allowedTriangle(t,allowedBones))continue;
        out.triangles.push_back(int(i));
        for(int a=0;a<3;++a)for(int b=a+1;b<3;++b)
        {
            const int x=t.corner[a].bone,y=t.corner[b].bone;if(x!=y)links.insert({std::min(x,y),std::max(x,y)});
        }
        if(collectCorners)for(int j=0;j<3;++j)
        {
            int id=mesh.positionIndices[i][j];if(!seen[id]){seen[id]=true;out.uniqueCorners.push_back(int(out.triangles.size()-1)*3+j);}
        }
        for(const auto& corner:t.corner)
        {
            auto found=bounds.find(corner.bone);
            if(found==bounds.end())found=bounds.insert({corner.bone,emptyBounds()}).first;
            for(int a=0;a<3;++a){found->second.min[a]=std::min(found->second.min[a],corner.p[a]);found->second.max[a]=std::max(found->second.max[a],corner.p[a]);}
        }
    }
    std::map<int,int> indices;
    for(const auto& item:bounds){indices[item.first]=int(out.bones.size());out.bones.push_back({item.first,item.second});}
    for(const auto& pair:links)out.links.push_back({indices[pair.first],indices[pair.second]});return true;
}
bool BuildPoseMetrics(const std::vector<PoseSource>& sources,const Frame& frame,std::vector<BoneMetric>& out)
{
    out.clear();const float* axes[3]={frame.axisX,frame.axisY,frame.normal};
    const double eps=std::numeric_limits<float>::epsilon()/2.0,gamma=5*eps/(1-5*eps);
    for(const auto& source:sources)
    {
        if(!source.geometry)return false;
        const size_t first=out.size();
        for(const auto& bone:source.geometry->bones)
        {
            if(bone.bone<0||size_t(bone.bone)>=source.matrices.size())return false;
            const auto& m=source.matrices[bone.bone].m;BoneMetric metric;metric.local=bone.local;
            double worldError[3],relative[3],subError[3];
            for(int row=0;row<3;++row)
            {
                double lo=m[row][3],hi=lo,absolute=std::abs(lo);
                for(int a=0;a<3;++a)
                {
                    const double x=double(m[row][a])*bone.local.min[a],y=double(m[row][a])*bone.local.max[a];
                    lo+=std::min(x,y);hi+=std::max(x,y);absolute+=std::max(std::abs(x),std::abs(y));
                }
                worldError[row]=eps/(1-eps)*std::max(std::abs(lo),std::abs(hi))+32*std::numeric_limits<double>::epsilon()*absolute+1e-37;
                relative[row]=std::max(std::abs(lo-frame.origin[row]),std::abs(hi-frame.origin[row]))+worldError[row];
                subError[row]=eps/(1-eps)*relative[row]+1e-37;
            }
            for(int row=0;row<3;++row)
            {
                double projection=0;
                for(int a=0;a<3;++a)
                {
                    for(int k=0;k<3;++k)metric.matrix[row][a]+=double(axes[row][k])*m[k][a];
                    metric.matrix[row][3]+=double(axes[row][a])*(double(m[a][3])-frame.origin[a]);
                    metric.rounding[row]+=std::abs(double(axes[row][a]))*(worldError[a]+subError[a]);
                    projection+=std::abs(double(axes[row][a]))*(relative[a]+subError[a]);
                }
                metric.rounding[row]+=gamma*projection+1e-7;
                double lo=metric.matrix[row][3],hi=lo;
                for(int a=0;a<3;++a)
                {
                    double x=metric.matrix[row][a]*bone.local.min[a],y=metric.matrix[row][a]*bone.local.max[a];
                    lo+=std::min(x,y);hi+=std::max(x,y);
                }
                metric.bounds.min[row]=std::nextafter(float(lo-metric.rounding[row]),-std::numeric_limits<float>::infinity());
                metric.bounds.max[row]=std::nextafter(float(hi+metric.rounding[row]),std::numeric_limits<float>::infinity());
                if(!std::isfinite(metric.bounds.min[row])||!std::isfinite(metric.bounds.max[row]))return false;
            }
            out.push_back(metric);
        }
        // A mixed-bone triangle can span the query while every individual
        // bone's vertex box is far away. Each incident bone must therefore use
        // the full triangle envelope for relevance, not just its own corners.
        std::vector<Bounds> original;for(size_t i=first;i<out.size();++i)original.push_back(out[i].bounds);
        for(const auto& pair:source.geometry->links)
        {
            extend(out[first+pair.first].bounds,original[pair.second]);
            extend(out[first+pair.second].bounds,original[pair.first]);
        }
    }
    return true;
}
bool PoseDisplacementBound(const std::vector<BoneMetric>& old,const std::vector<BoneMetric>& current,const std::vector<Triangle>& query,float& error,bool allBones)
{
    error=0;if(old.size()!=current.size()||query.empty())return false;
    Bounds box=emptyBounds();if(!allBones)for(const auto& t:query)extend(box,triangleBounds(t));
    double maximum=0;
    for(size_t i=0;i<old.size();++i)
    {
        Bounds sweep=old[i].bounds;extend(sweep,current[i].bounds);
        if(!allBones&&!overlap(sweep,box,.00011))continue;
        double distance2=0;
        for(int row=0;row<3;++row)
        {
            double lo=current[i].matrix[row][3]-old[i].matrix[row][3],hi=lo;
            for(int a=0;a<3;++a)
            {
                const double delta=current[i].matrix[row][a]-old[i].matrix[row][a];
                const double x=delta*old[i].local.min[a],y=delta*old[i].local.max[a];lo+=std::min(x,y);hi+=std::max(x,y);
            }
            const double bound=std::max(std::abs(lo),std::abs(hi))+old[i].rounding[row]+current[i].rounding[row];distance2+=bound*bound;
        }
        maximum=std::max(maximum,std::sqrt(distance2));if(maximum>.003)return false;
    }
    error=std::nextafter(float(maximum+.00001),std::numeric_limits<float>::infinity());return true;
}
bool BuildPoseClosures(const MeshCollider& collider,const std::vector<PoseSource>& sources,std::vector<PoseClosure>& out)
{
    out.clear();std::vector<std::pair<int,const StudioTriangle*>> raw;
    for(size_t source=0;source<sources.size();++source)
    {
        if(!sources[source].mesh||!sources[source].geometry)return false;
        for(int index:sources[source].geometry->triangles)raw.push_back({int(source),&sources[source].mesh->triangles[index]});
    }
    if(raw.size()!=collider.triangles.size())return false;
    for(const auto& edge:collider.closedEdges)for(int i=0;i<2;++i)
    {
        const auto& a=raw[edge.triangleA];const auto& b=raw[edge.triangleB];
        const auto& ac=a.second->corner[(edge.cornerA+i)%3];const auto& bc=b.second->corner[(edge.cornerB+(edge.reverse?1-i:i))%3];
        if(a.first==b.first&&ac.bone==bc.bone&&!std::memcmp(ac.p,bc.p,sizeof(ac.p)))continue;
        PoseClosure check;check.group=edge.group;check.a.source=a.first;check.b.source=b.first;check.a.bone=ac.bone;check.b.bone=bc.bone;
        std::copy(ac.p,ac.p+3,check.a.p);std::copy(bc.p,bc.p+3,check.b.p);out.push_back(check);
    }
    return true;
}
bool PoseClosuresUnchanged(const MeshCollider& collider,const std::vector<PoseClosure>& closures,const std::vector<PoseSource>& sources,const Frame& frame)
{
    std::vector<bool> closed;for(const auto& group:collider.groups)closed.push_back(group.topologyClosed);
    auto position=[&](const ClosurePoint& input)
    {
        float world[3],relative[3],local[3];point(sources[input.source].matrices[input.bone].m,V(input.p)).store(world);
        for(int a=0;a<3;++a)relative[a]=world[a]-frame.origin[a];
        const float* axes[3]={frame.axisX,frame.axisY,frame.normal};
        for(int row=0;row<3;++row)local[row]=relative[0]*axes[row][0]+relative[1]*axes[row][1]+relative[2]*axes[row][2];
        return V(local);
    };
    for(const auto& edge:closures)if(closed[edge.group]&&(position(edge.a)-position(edge.b)).length2()>1e-12)closed[edge.group]=false;
    for(size_t i=0;i<closed.size();++i)if(closed[i]!=collider.groups[i].closed)return false;return true;
}

bool BuildBoneScene(const std::vector<PoseSource>& sources,const MeshCollider& reference,BoneScene& out)
{
    out=BoneScene();if(reference.surfaceOnly)return false;
    std::map<int,int> groupIndices;
    for(const auto& group:reference.groups)
    {
        groupIndices[group.id]=int(out.groups.size());BoneSceneGroup g;
        g.id=group.id;g.topologyClosed=group.topologyClosed;g.rigid=-2;out.groups.push_back(g);
    }
    size_t original=0;int firstMetric=0;
    for(size_t sourceIndex=0;sourceIndex<sources.size();++sourceIndex)
    {
        const auto& source=sources[sourceIndex];if(!source.mesh||!source.geometry)return false;
        BoneSceneSource stored;stored.mesh=source.mesh;stored.geometry=source.geometry;stored.body=source.body;stored.skin=source.skin;
        stored.firstMetric=firstMetric;stored.firstTriangle=int(original);out.sources.push_back(stored);
        std::map<int,int> metricIndices,rigidIndices;
        for(size_t b=0;b<source.geometry->bones.size();++b)metricIndices[source.geometry->bones[b].bone]=firstMetric+int(b);
        for(int rawIndex:source.geometry->triangles)
        {
            if(original>=reference.triangles.size()||rawIndex<0||size_t(rawIndex)>=source.mesh->triangles.size())return false;
            const auto& raw=source.mesh->triangles[rawIndex];const auto& posedTriangle=reference.triangles[original];
            const auto groupIt=groupIndices.find(posedTriangle.group);if(groupIt==groupIndices.end())return false;
            auto& group=out.groups[groupIt->second];int bone=raw.corner[0].bone;
            for(const auto& corner:raw.corner)
            {
                const auto metric=metricIndices.find(corner.bone);if(metric==metricIndices.end())return false;
                group.metrics.push_back(metric->second);if(corner.bone!=raw.corner[0].bone)bone=-1;
            }
            if(bone<0)
            {
                out.mixed.push_back({int(sourceIndex),rawIndex,int(original)});group.rigid=-1;
            }
            else
            {
                auto found=rigidIndices.find(bone);
                if(found==rigidIndices.end())
                {
                    const int index=int(out.rigid.size());BoneSceneRigid rigid;
                    rigid.source=int(sourceIndex);rigid.bone=bone;rigid.metric=metricIndices[bone];
                    out.rigid.push_back(std::move(rigid));found=rigidIndices.insert({bone,index}).first;
                }
                auto& rigid=out.rigid[found->second];Triangle local;local.bone=bone;local.group=posedTriangle.group;
                for(int j=0;j<3;++j)std::copy(raw.corner[j].p,raw.corner[j].p+3,local.p[j]);
                rigid.collider.triangles.push_back(local);rigid.originalTriangles.push_back(int(original));
                if(group.rigid==-2)group.rigid=found->second;else if(group.rigid!=found->second)group.rigid=-1;
            }
            ++original;
        }
        firstMetric+=int(source.geometry->bones.size());
    }
    if(original!=reference.triangles.size())return false;
    for(auto& group:out.groups)
    {
        std::sort(group.metrics.begin(),group.metrics.end());group.metrics.erase(std::unique(group.metrics.begin(),group.metrics.end()),group.metrics.end());
        group.unsupported=group.topologyClosed&&group.rigid<0;
    }
    for(size_t index=0;index<out.rigid.size();++index)
    {
        auto& rigid=out.rigid[index];const auto triangles=std::move(rigid.collider.triangles);
        if(!BuildMeshCollider(triangles,rigid.collider))return false;
        for(auto& localGroup:rigid.collider.groups)
        {
            auto& group=out.groups[groupIndices[localGroup.id]];
            const bool allowed=group.topologyClosed&&group.rigid==int(index);
            if(allowed&&!localGroup.topologyClosed)group.unsupported=true;
            localGroup.closed=localGroup.topologyClosed=allowed&&localGroup.topologyClosed;
        }
    }
    out.valid=true;return true;
}

namespace
{
// Exact inverse of the near-rigid matrix, with a norm bound from A^T A.
// This handles reflection and avoids assuming float bone axes are perfect.
bool boneInverse(const BoneMetric& metric,double inverseMatrix[3][4],double& inverseNorm)
{
    const auto& a=metric.matrix;double defect=0;
    for(int i=0;i<3;++i)
    {
        double row=0;
        for(int j=0;j<3;++j){double v=0;for(int k=0;k<3;++k)v+=a[k][i]*a[k][j];row+=std::abs(v-(i==j?1:0));}
        defect=std::max(defect,row);
    }
    if(!std::isfinite(defect)||defect>1e-4)return false;
    const V r0(a[0][0],a[0][1],a[0][2]),r1(a[1][0],a[1][1],a[1][2]),r2(a[2][0],a[2][1],a[2][2]);
    const double determinant=r0.dot(r1.cross(r2));if(!std::isfinite(determinant)||std::abs(determinant)<.99)return false;
    const V columns[3]={r1.cross(r2)*(1/determinant),r2.cross(r0)*(1/determinant),r0.cross(r1)*(1/determinant)};
    for(int c=0;c<3;++c){inverseMatrix[0][c]=columns[c].x;inverseMatrix[1][c]=columns[c].y;inverseMatrix[2][c]=columns[c].z;}
    for(int r=0;r<3;++r){inverseMatrix[r][3]=0;for(int c=0;c<3;++c)inverseMatrix[r][3]-=inverseMatrix[r][c]*a[c][3];}
    inverseNorm=1/std::sqrt(1-defect)+1e-12;return true;
}
V boneQueryPoint(const double inverseMatrix[3][4],V input,double& error)
{
    const double p[3]={input.x,input.y,input.z};double output[3],error2=0;
    for(int r=0;r<3;++r)
    {
        double value=inverseMatrix[r][3],absolute=std::abs(value);
        for(int c=0;c<3;++c){const double term=inverseMatrix[r][c]*p[c];value+=term;absolute+=std::abs(term);}
        output[r]=float(value);
        const double rounding=std::abs(output[r]-value)+128*std::numeric_limits<double>::epsilon()*(absolute+std::abs(value)+1);
        error2+=rounding*rounding;
    }
    error=std::max(error,std::sqrt(error2));return V(output[0],output[1],output[2]);
}
void addBoneCounts(CollisionResult& total,const CollisionResult& item)
{
    total.trianglePairs+=item.trianglePairs;total.containmentSamples+=item.containmentSamples;
    total.closedGroups+=item.closedGroups;total.planeSeparatedPairs+=item.planeSeparatedPairs;
    total.axisSeparatedPairs+=item.axisSeparatedPairs;total.distancePairs+=item.distancePairs;
}
}
bool CheckBoneScene(const BoneScene& scene,const std::vector<PoseSource>& sources,
                    const std::vector<BoneMetric>& metrics,const Frame& frame,
                    const MeshCollider& query,CollisionResult& result,float gap)
{
    result=CollisionResult();if(!scene.valid||scene.sources.size()!=sources.size()||!std::isfinite(gap)||gap<0)return false;
    size_t metricCount=0;
    for(size_t i=0;i<sources.size();++i)
    {
        const auto& source=sources[i];const auto& stored=scene.sources[i];
        if(source.mesh!=stored.mesh||source.geometry!=stored.geometry||source.body!=stored.body||source.skin!=stored.skin||!source.geometry)return false;
        metricCount+=source.geometry->bones.size();
    }
    if(metricCount!=metrics.size())return false;
    if(query.nodes.empty())return true;
    auto scratch=std::find_if(scene.localQueries.begin(),scene.localQueries.end(),[&](const auto& item){return item.first==&query;});
    if(scratch==scene.localQueries.end())
    {
        scene.localQueries.push_back({&query,MeshCollider()});scratch=scene.localQueries.end()-1;
        scene.rigidMemos.emplace_back(scene.rigid.size());scene.certifyCooldown.emplace_back(scene.rigid.size(),0);
    }
    const size_t slot=size_t(scratch-scene.localQueries.begin());
    auto& localQuery=scratch->second;
    const Bounds& queryBounds=query.nodes[0].bounds;
    for(const auto& group:scene.groups)if(group.unsupported)
    {
        Bounds bounds=emptyBounds();for(int metric:group.metrics)extend(bounds,metrics[metric].bounds);
        // A closed shell can contain the query while all of its faces/bones
        // are distant. Use the entire group envelope, never face proximity.
        if(overlap(bounds,queryBounds,gap))return false;
    }
    float samples[7][3];for(auto& p:samples)std::copy(query.triangles[0].p[0],query.triangles[0].p[0]+3,p);
    for(const auto& t:query.triangles)for(const auto& p:t.p)for(int a=0;a<3;++a)
    {
        if(p[a]<samples[1+a*2][a])std::copy(p,p+3,samples[1+a*2]);
        if(p[a]>samples[2+a*2][a])std::copy(p,p+3,samples[2+a*2]);
    }
    for(size_t rigidIndex=0;rigidIndex<scene.rigid.size();++rigidIndex)
    {
        const auto& rigid=scene.rigid[rigidIndex];
        const auto& metric=metrics[rigid.metric];if(!overlap(metric.bounds,queryBounds,gap))continue;
        double inverseMatrix[3][4],inverseNorm;if(!boneInverse(metric,inverseMatrix,inverseNorm))return false;
        double error=0;scene.transformed=query.triangles;
        for(auto& triangle:scene.transformed)for(auto& p:triangle.p)boneQueryPoint(inverseMatrix,V(p),error).store(p);
        double rounding2=0;for(double e:metric.rounding)rounding2+=e*e;
        const float localGap=std::nextafter(float(inverseNorm*(gap+std::sqrt(rounding2))+error+1e-7),std::numeric_limits<float>::infinity());
        if(!std::isfinite(localGap))return false;
        auto& memo=scene.rigidMemos[slot][rigidIndex];auto& cooldown=scene.certifyCooldown[slot][rigidIndex];
        if(cooldown)--cooldown;
        CollisionResult hit;
        // The obstacle is permanently bone-local. Reuse still measures every
        // current accessory vertex; only its proved clearance can skip work.
        const bool reused=ReuseMeshCollision(rigid.collider,scene.transformed,memo,hit,localGap);
        if(!reused)
        {
            if(!RefitMeshCollider(scene.transformed,localQuery,false))return false;
            hit=CheckMeshCollision(rigid.collider,localQuery,localGap);
        }
        // Preserve the original collision-frame extrema, too. Rotating the
        // query changes which vertices are extremal, especially on multi-part
        // pendants; local extrema alone cannot replace those original samples.
        if(!hit.blocked)for(const auto& group:rigid.collider.groups)if(group.closed)for(const auto& sample:samples)
        {
            const V p=boneQueryPoint(inverseMatrix,V(sample),error);const double q[3]={p.x,p.y,p.z};bool inBounds=true;
            for(int a=0;a<3;++a)inBounds=inBounds&&q[a]>group.bounds.min[a]&&q[a]<group.bounds.max[a];
            if(inBounds){++hit.containmentSamples;if(insideGroup(rigid.collider,p,group.id)){hit.blocked=hit.contained=true;hit.containingGroup=group.id;break;}}
        }
        if(!reused&&!hit.blocked&&!cooldown)
        {
            cooldown=16;
            for(float extra:{.002f,.0005f})
            {
                const float certified=std::nextafter(localGap+extra,std::numeric_limits<float>::infinity());
                const auto check=CheckMeshCollision(rigid.collider,localQuery,certified);
                hit.trianglePairs+=check.trianglePairs;hit.containmentSamples+=check.containmentSamples;
                hit.planeSeparatedPairs+=check.planeSeparatedPairs;hit.axisSeparatedPairs+=check.axisSeparatedPairs;hit.distancePairs+=check.distancePairs;
                if(check.blocked)continue;
                // Same owner/serial makes the old obstacle snapshot unnecessary.
                // Store only the local addon vertices, their bounds and margin.
                memo.accessory=scene.transformed;memo.bounds=emptyBounds();
                for(const auto& triangle:scene.transformed)extend(memo.bounds,triangleBounds(triangle));
                memo.owner=&rigid.collider;memo.geometrySerial=rigid.collider.geometrySerial;
                memo.separation=certified;memo.valid=true;break;
            }
        }
        addBoneCounts(result,hit);
        if(hit.blocked)
        {
            const auto counts=result;result=hit;
            result.trianglePairs=counts.trianglePairs;result.containmentSamples=counts.containmentSamples;
            result.closedGroups=counts.closedGroups;result.planeSeparatedPairs=counts.planeSeparatedPairs;
            result.axisSeparatedPairs=counts.axisSeparatedPairs;result.distancePairs=counts.distancePairs;
            if(hit.obstacleTriangle>=0)result.obstacleTriangle=rigid.originalTriangles[hit.obstacleTriangle];
            return true;
        }
    }
    scene.mixedTriangles.clear();std::vector<int> mixedOriginal;
    const float* axes[3]={frame.axisX,frame.axisY,frame.normal};
    for(const auto& mixed:scene.mixed)
    {
        const auto& source=sources[mixed[0]];const auto& stored=scene.sources[mixed[0]];const auto& raw=source.mesh->triangles[mixed[1]];
        bool relevant=false;
        for(const auto& corner:raw.corner)
            for(size_t b=0;b<source.geometry->bones.size();++b)if(source.geometry->bones[b].bone==corner.bone)
                relevant=relevant||overlap(metrics[stored.firstMetric+b].bounds,queryBounds,gap);
        if(!relevant)continue;
        Triangle triangle;triangle.bone=-1;triangle.group=raw.part;
        for(int j=0;j<3;++j)
        {
            const auto& corner=raw.corner[j];if(corner.bone<0||size_t(corner.bone)>=source.matrices.size())return false;
            float world[3],relative[3];point(source.matrices[corner.bone].m,V(corner.p)).store(world);
            for(int a=0;a<3;++a)relative[a]=world[a]-frame.origin[a];
            for(int a=0;a<3;++a)triangle.p[j][a]=relative[0]*axes[a][0]+relative[1]*axes[a][1]+relative[2]*axes[a][2];
        }
        scene.mixedTriangles.push_back(triangle);mixedOriginal.push_back(mixed[2]);
    }
    if(!scene.mixedTriangles.empty())
    {
        if(!RefitMeshCollider(scene.mixedTriangles,scene.mixedCollider,false))return false;
        auto hit=CheckMeshCollision(scene.mixedCollider,query,gap);addBoneCounts(result,hit);
        if(hit.blocked)
        {
            const auto counts=result;result=hit;result.trianglePairs=counts.trianglePairs;
            result.planeSeparatedPairs=counts.planeSeparatedPairs;result.axisSeparatedPairs=counts.axisSeparatedPairs;result.distancePairs=counts.distancePairs;
            if(hit.obstacleTriangle>=0)result.obstacleTriangle=mixedOriginal[hit.obstacleTriangle];
        }
    }
    return true;
}

void ResetSwing(SwingState& state,const SwingInput& input)
{
    unsigned resets=state.resets+1;state=SwingState();state.initialized=true;state.resets=resets;
    state.lastTime=state.simulationTime=std::isfinite(input.time)?input.time:0;state.resetToken=input.resetToken;
    for(int i=0;i<3;++i)
    {
        state.anchor[i]=std::isfinite(input.anchor[i])?input.anchor[i]:0;
        state.acceleration[i]=std::isfinite(input.anchorAcceleration[i])?input.anchorAcceleration[i]:0;
        state.gravity[i]=std::isfinite(input.gravity[i])?input.gravity[i]:0;
    }
}
SwingOutput UpdateSwing(SwingState& state,const SwingConfig& config,const SwingInput& input)
{
    const double step=1.0/120.0,pi=3.14159265358979323846;
    bool valid=std::isfinite(input.time)&&V(input.anchor).finite()&&V(input.anchorAcceleration).finite()&&V(input.gravity).finite();
    if(!state.initialized||!valid||input.teleport||input.resetToken!=state.resetToken||input.time<state.lastTime||
        input.time-state.lastTime>.5||(V(input.anchor)-V(state.anchor)).length2()>64*64)
    {ResetSwing(state,input);return SwingOutput();}
    if(std::isfinite(config.maxAngle)&&config.maxAngle<=0)
    {
        std::fill(state.angle,state.angle+2,0.0f);std::fill(state.previousAngle,state.previousAngle+2,0.0f);
        std::fill(state.velocity,state.velocity+2,0.0f);
        state.lastTime=state.simulationTime=input.time;
        std::copy(input.anchor,input.anchor+3,state.anchor);
        std::copy(input.anchorAcceleration,input.anchorAcceleration+3,state.acceleration);
        std::copy(input.gravity,input.gravity+3,state.gravity);
        return SwingOutput();
    }
    const double elapsed=input.time-state.lastTime;
    if(elapsed==0)return swingOutput(state,config,input.time);
    const double amplitude=std::isfinite(config.amplitude)?clamp(config.amplitude,0,4):1;
    const double length=std::isfinite(config.length)?clamp(config.length,.05,100):2;
    const double stiffness=std::isfinite(config.stiffness)?clamp(config.stiffness,0,400):45;
    const double damping=std::isfinite(config.damping)?clamp(config.damping,0,80):8;
    const double limit=(std::isfinite(config.maxAngle)?clamp(config.maxAngle,0,75):35)*pi/180;
    while(state.simulationTime+step<=input.time+1e-12)
    {
        const double alpha=clamp((state.simulationTime+step*.5-state.lastTime)/elapsed,0,1);
        V force;
        double* f[3]={&force.x,&force.y,&force.z};
        for(int i=0;i<3;++i)
        {
            double gravity=state.gravity[i]+(input.gravity[i]-state.gravity[i])*alpha;
            double acceleration=state.acceleration[i]+(input.anchorAcceleration[i]-state.acceleration[i])*alpha;
            *f[i]=clamp(gravity,-2000,2000)-clamp(acceleration,-10000,10000);
        }
        double a[2]={(force.y*std::sin(state.angle[0])-force.z*std::cos(state.angle[0]))/length,
                     (force.x*std::cos(state.angle[1])+force.y*std::sin(state.angle[1]))/length};
        for(int i=0;i<2;++i)
        {
            state.previousAngle[i]=state.angle[i];
            // CSSO jigglebone principle: local spring error and damping, then
            // velocity before position, with a finite angle constraint. Here
            // an accumulator gives true fixed 120 Hz steps and render interpolation.
            double velocity=state.velocity[i]+step*(amplitude*a[i]-stiffness*state.angle[i]-damping*state.velocity[i]);
            double angle=state.angle[i]+step*velocity;
            if(angle>limit){angle=limit;if(velocity>0)velocity=0;}
            if(angle<-limit){angle=-limit;if(velocity<0)velocity=0;}
            state.angle[i]=float(angle);state.velocity[i]=float(velocity);
        }
        state.simulationTime+=step;++state.steps;
    }
    state.lastTime=input.time;
    std::copy(input.anchor,input.anchor+3,state.anchor);
    std::copy(input.anchorAcceleration,input.anchorAcceleration+3,state.acceleration);
    std::copy(input.gravity,input.gravity+3,state.gravity);
    return swingOutput(state,config,input.time);
}
}
