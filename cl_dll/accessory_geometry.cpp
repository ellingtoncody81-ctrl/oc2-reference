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
Triangle posed(const StudioTriangle& t,const float (*matrices)[3][4])
{
    Triangle out;V normal;
    out.bone=t.corner[0].bone;
    for(int j=0;j<3;++j)
    {
        const StudioCorner& c=t.corner[j];
        point(matrices[c.bone],V(c.p)).store(out.p[j]);
        normal=normal+point(matrices[c.normalBone],V(c.normal),false);
        if(c.bone!=out.bone)out.bone=-1;
    }
    normal.unit().store(out.normal);return out;
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
                    for(int i=0;i<n;++i)
                    {
                        int v=points[i*4],no=points[i*4+1];
                        if(v<0||v>=model.numverts||no<0||no>=model.numnorms||vb[v]>=h->numbones||nb[no]>=h->numbones)return false;
                        StudioCorner& c=corners[i];c.bone=vb[v];c.normalBone=nb[no];
                        std::copy(vertices[v],vertices[v]+3,c.p);std::copy(normals[no],normals[no]+3,c.normal);
                        if(!V(c.p).finite()||!V(c.normal).finite())return false;
                    }
                    for(int i=2;i<n;++i)
                    {
                        StudioTriangle t=meshTriangle;
                        int a=count<0?0:(i%2?i-1:i-2),b=count<0?i-1:(i%2?i-2:i-1);
                        t.corner[0]=corners[a];t.corner[1]=corners[b];t.corner[2]=corners[i];
                        result.triangles.push_back(t);++decoded;
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
                         std::vector<Triangle>& out,bool append)
{
    if(!append)out.clear();
    if(!c.header||!poseMatrices)return false;
    int group=0;for(const Triangle& t:out)group=std::max(group,t.group+1);
    for(const StudioTriangle& t:c.triangles)
        if(sameActive(c,t,body)&&(skin==0?t.skin0Real:real(c,t,skin)))
        {
            Triangle p=posed(t,poseMatrices);p.group=group+t.part;out.push_back(p);
        }
    return true;
}
bool ReadStudioTriangles(const studiohdr_t* h,int body,int skin,const float (*poseMatrices)[3][4],
                         std::vector<Triangle>& out,bool append)
{
    StudioTriangleCache cache;
    return DecodeStudioTriangles(h,cache)&&ReadStudioTriangles(cache,body,skin,poseMatrices,out,append);
}

bool BuildMountCache(const studiohdr_t* h,MountCache& out)
{
    out=MountCache();
    if(!DecodeStudioTriangles(h,out.studio)||!buildBind(h,out.bind))return false;
    const float (*matrices)[3][4]=&out.bind[0].m;
    std::vector<Triangle> decals;std::set<int> bones;V normal;
    V seed;
    for(const StudioTriangle& t:out.studio.triangles)
        if(t.decal&&sameActive(out.studio,t,0))
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
    const mstudiobone_t* sourceBones=at<mstudiobone_t>(h,h->boneindex,h->numbones);
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
        if(arm)continue;
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
        if(t.bone<0)continue;
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

static bool FindMountImpl(const MountCache& c,float u,float v,Mount& out,const MountViewConstraint* view)
{
    out=Mount();
    if(!c.valid||!std::isfinite(u)||!std::isfinite(v))return false;
    u=float(clamp(u,0,1));v=float(clamp(v,0,1));
    V query(c.minUV[0]+u*(c.maxUV[0]-c.minUV[0]),c.minUV[1]+v*(c.maxUV[1]-c.minUV[1]),0);
    double bestDistance=std::numeric_limits<double>::infinity(),bestDepth=-std::numeric_limits<double>::infinity();
    const Triangle* best=nullptr;V bestPoint,bestProjection;
    for(const Triangle& t:c.surfaces)
    {
        if(!sideFacing(c,t))continue;
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
        double distance=(q.x-query.x)*(q.x-query.x)+(q.y-query.y)*(q.y-query.y);
        if(distance<bestDistance-1e-14||(std::abs(distance-bestDistance)<=1e-14&&q.z>bestDepth))
        {
            const Triangle* selected=&t;V selectedPoint=V(t.p[0])*(1-b-d)+V(t.p[1])*b+V(t.p[2])*d;double selectedDepth=q.z;
            if(view)
            {
                // A safe hidden layer must never replace the actual frontmost
                // gun surface at this UV. Recheck the original full envelope.
                if(!frontAt(c,q,selected,selectedPoint,selectedDepth))continue;
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
        if(!sideFacing(c,t))continue;
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
    std::set<std::pair<int,int>> visited;
    for(const Candidate& candidate:candidates)
    {
        const V q=candidate.q;
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
        mount.actualUV[0]=float(clamp((q.x-c.minUV[0])/width,0,1));
        mount.actualUV[1]=float(clamp((q.y-c.minUV[1])/height,0,1));
        mount.snapDistance=float(std::hypot(q.x-query.x,q.y-query.y));out.push_back(mount);
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
    Bounds bounds=emptyBounds();for(int i=first;i<first+count;++i)extend(bounds,triangleBounds(c.triangles[c.order[i]]));
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
    for(size_t index=0;index<c.triangles.size();++index)
    {
        const Triangle& t=c.triangles[index];
        if(!groupIndex.count(t.group))
        {
            groupIndex[t.group]=int(c.groups.size());CollisionGroup g;g.id=t.group;g.bounds=emptyBounds();c.groups.push_back(g);
        }
        extend(c.groups[groupIndex[t.group]].bounds,triangleBounds(t));
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
bool planeSeparates(const Triangle& a,const Triangle& b,double gap)
{
    const V origin(a.p[0]),u=V(a.p[1])-origin,v=V(a.p[2])-origin,n=u.cross(v);
    const double normal[3]={n.x,n.y,n.z},edgeU[3]={u.x,u.y,u.z},edgeV[3]={v.x,v.y,v.z};
    const double eps=std::numeric_limits<double>::epsilon();
    const double length=std::sqrt(n.length2());if(length<=1e-20||!std::isfinite(length))return false;
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
    const double threshold=std::max(gap,1e-10)*(length+normalError+8*eps*length);
    bool above=true,below=true;
    for(int corner=0;corner<3;++corner)
    {
        double distance=0,error=0;
        for(int i=0;i<3;++i)
        {
            const double relative=double(b.p[corner][i])-a.p[0][i];
            const double relativeError=eps*(std::abs(double(b.p[corner][i]))+std::abs(double(a.p[0][i])));
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

bool BuildMeshCollider(const std::vector<Triangle>& triangles,MeshCollider& out)
{
    out=MeshCollider();
    for(const Triangle& t:triangles)for(int j=0;j<3;++j)if(!V(t.p[j]).finite())return false;
    out.triangles=triangles;out.order.resize(triangles.size());
    for(size_t i=0;i<triangles.size();++i)out.order[i]=int(i);
    if(!triangles.empty()){out.nodes.reserve(triangles.size()*2);collisionNode(out,0,int(triangles.size()));collisionGroups(out);}
    return true;
}
bool RefitMeshCollider(const std::vector<Triangle>& triangles,MeshCollider& c)
{
    if(triangles.size()!=c.triangles.size()||(!triangles.empty()&&c.nodes.empty()))return BuildMeshCollider(triangles,c);
    for(size_t i=0;i<triangles.size();++i)
    {
        if(triangles[i].group!=c.triangles[i].group)return BuildMeshCollider(triangles,c);
        for(int j=0;j<3;++j)if(!V(triangles[i].p[j]).finite())return false;
    }
    c.triangles=triangles;
    for(int index=int(c.nodes.size())-1;index>=0;--index)
    {
        CollisionNode& node=c.nodes[index];node.bounds=emptyBounds();
        if(node.count)for(int i=node.first;i<node.first+node.count;++i)extend(node.bounds,triangleBounds(c.triangles[c.order[i]]));
        else{extend(node.bounds,c.nodes[node.left].bounds);extend(node.bounds,c.nodes[node.right].bounds);}
    }
    for(CollisionGroup& g:c.groups){g.bounds=emptyBounds();g.closed=g.topologyClosed;}
    for(const Triangle& t:c.triangles)for(CollisionGroup& g:c.groups)if(g.id==t.group){extend(g.bounds,triangleBounds(t));break;}
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
    return true;
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
        const Bounds bounds=triangleBounds(t);stack.clear();stack.push_back(0);
        while(!stack.empty())
        {
            const CollisionNode& node=c.nodes[stack.back()];stack.pop_back();if(!overlap(bounds,node.bounds,gap))continue;
            if(node.count)for(int i=node.first;i<node.first+node.count;++i)
            {
                const int obstacle=c.order[i];const Triangle& other=c.triangles[obstacle];
                if(!overlap(bounds,triangleBounds(other),gap))continue;
                ++result.trianglePairs;
                if(planeSeparates(t,other,gap)||planeSeparates(other,t,gap)){++result.planeSeparatedPairs;continue;}
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
    // Test actual mesh vertices, never an empty AABB center. The first vertex
    // plus the six real extrema cover wholly contained accessories and common
    // disconnected clip/pendant pieces; open obstacle groups claim no interior.
    V samples[7];for(V& p:samples)p=V(accessory[0].p[0]);
    for(const Triangle& t:accessory)for(int j=0;j<3;++j)
    {
        const V p(t.p[j]);const double q[3]={p.x,p.y,p.z};
        for(int axis=0;axis<3;++axis)
        {
            const double lo[3]={samples[1+axis*2].x,samples[1+axis*2].y,samples[1+axis*2].z};
            const double hi[3]={samples[2+axis*2].x,samples[2+axis*2].y,samples[2+axis*2].z};
            if(q[axis]<lo[axis])samples[1+axis*2]=p;if(q[axis]>hi[axis])samples[2+axis*2]=p;
        }
    }
    for(const CollisionGroup& group:c.groups)if(group.closed)for(V p:samples)
    {
        const double q[3]={p.x,p.y,p.z};bool contained=true;
        for(int i=0;i<3;++i)contained&=q[i]>group.bounds.min[i]&&q[i]<group.bounds.max[i];
        if(!contained)continue;
        ++result.containmentSamples;
        if(insideGroup(c,p,group.id)){result.blocked=result.contained=true;result.containingGroup=group.id;return result;}
    }
    return result;
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
