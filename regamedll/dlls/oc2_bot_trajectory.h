#pragma once
#include <cmath>

// Bounded prediction of the Xash MOVETYPE_BOUNCE adapter. This predicts the
// effect, not the first ground contact. Dynamic/uncertain contacts are rejected.
namespace oc2_bot_arc
{
struct Vec
{
    float x=0,y=0,z=0;
    Vec()=default;
    Vec(float a,float b,float c):x(a),y(b),z(c){}
    Vec operator+(Vec b)const{return {x+b.x,y+b.y,z+b.z};}
    Vec operator-(Vec b)const{return {x-b.x,y-b.y,z-b.z};}
    Vec operator*(float s)const{return {x*s,y*s,z*s};}
    float dot(Vec b)const{return x*b.x+y*b.y+z*b.z;}
    float length2()const{return dot(*this);}
    bool finite()const{return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z);}
};
struct Hit { float fraction=1;Vec end,normal;bool invalid=false; };
struct Result { bool valid=false;Vec effect;float time=0;int walls=0,traces=0; };
enum Kind { HE,Flash,Smoke,Fire };
template<class Trace,class FireTouch>
Result Predict(Vec source,Vec velocity,float gravity,Kind kind,Trace trace,FireTouch touchingFire,float frameStep=.01f)
{
    Result out;out.effect=source;
    if(!source.finite()||!velocity.finite()||!std::isfinite(gravity)||gravity<=0)return out;
    if(!std::isfinite(frameStep)||frameStep<=0)return out;
    const float dt=frameStep<.005f?.005f:frameStep>.0333333f?.0333333f:frameStep;
    const float friction=kind==HE?.7f:kind==Fire?.55f:.8f;
    // TumbleThink selects Detonate at 1.5s, which runs on the next 0.1s think.
    const float thinkStep=std::ceil(.1f/dt-.0001f)*dt;
    const float fuse=kind==Fire?2.f:kind==Smoke?1.5f:(std::ceil(1.5f/thinkStep-.0001f)+1)*thinkStep;
    bool grounded=false;int bounces=0;
    for(int step=0;step<640&&step*dt<3.2f;++step)
    {
        out.time=(step+1)*dt;
        if(velocity.length2()>0)
        {
            if(velocity.z>0)grounded=false;
            velocity.z-=gravity*dt;
            const auto hit=trace(out.effect,out.effect+velocity*dt);++out.traces;
            if(hit.invalid||!hit.end.finite())return out;
            out.effect=hit.end;
            if(hit.fraction<1)
            {
                if(kind==Smoke&&touchingFire(out.effect)){out.valid=true;return out;}
                if(kind==Fire&&hit.normal.z>=.8660254f){out.valid=true;return out;}
                if(std::fabs(hit.normal.z)<.7f&&++out.walls>2)return out;
                if(kind!=Fire)
                {
                    if(grounded)velocity=velocity*.8f;
                    else if(++bounces>=10)velocity={};
                }
                velocity=velocity-hit.normal*(velocity.dot(hit.normal)*(2-friction));
                if(std::fabs(velocity.x)<.1f)velocity.x=0;
                if(std::fabs(velocity.y)<.1f)velocity.y=0;
                if(std::fabs(velocity.z)<.1f)velocity.z=0;
                grounded=false;
                if(hit.normal.z>.7f)
                {
                    const float speed2=velocity.length2();
                    if(velocity.z<gravity/0.4f*dt){grounded=true;velocity.z=0;}
                    if(speed2<900){grounded=true;velocity={};}
                    else
                    {
                        const auto slide=trace(out.effect,out.effect+velocity*((1-hit.fraction)*dt*.9f));++out.traces;
                        if(slide.invalid)return out;
                        out.effect=slide.end;
                    }
                }
                // Prevent the predictor's point trace from starting inside a
                // plane because of floating point rounding on the next step.
                out.effect=out.effect+hit.normal*.001f;
            }
        }
        if(out.time>=fuse)
        {
            if(kind==HE||kind==Flash)
            {
                const auto floor=trace(out.effect+Vec(0,0,8),out.effect-Vec(0,0,32));++out.traces;
                if(floor.invalid)return out;
                if(floor.fraction<1)out.effect=floor.end+floor.normal*(kind==HE?45.f:6.6f);
                out.valid=true;return out;
            }
            if(kind==Smoke&&grounded&&velocity.length2()<.01f){out.valid=true;return out;}
            if(kind==Fire)
            {
                const auto floor=trace(out.effect+Vec(0,0,10),out.effect-Vec(0,0,128));++out.traces;
                if(!floor.invalid&&floor.fraction<1&&floor.normal.z>.7f){out.effect=floor.end;out.valid=true;}
                return out;
            }
        }
    }
    return out;
}
}
