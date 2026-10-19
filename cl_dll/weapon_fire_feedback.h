#pragma once
#include <cmath>

// Baked model bursts must not emit more muzzle sprites than real fire events.
namespace weapon_fire_feedback
{
struct Stats { unsigned accepted=0, rendered=0, blocked=0; };
struct State
{
    int weapon=0, count=0;
    float time=-1, pending[4]={};
    Stats stats[32];
    void Clear() { count=0; }
    bool Observe(int id,float now)
    {
        if (id<=0 || id>=32 || !std::isfinite(now)) { weapon=0;time=-1;Clear();return false; }
        if (weapon!=id || now<time) Clear();
        weapon=id;time=now;
        while (count && now-pending[0]>.25f)
        {
            for (int i=1;i<count;++i) pending[i-1]=pending[i];
            --count;
        }
        return true;
    }
    void Commit(int id,float now)
    {
        if (!Observe(id,now)) return;
        ++stats[id].accepted;
        if (count==4)
        {
            for (int i=1;i<count;++i) pending[i-1]=pending[i];
            --count;
        }
        pending[count++]=now;
    }
    bool Consume(int id,float now)
    {
        if (!Observe(id,now)) return false;
        if (!count) { ++stats[id].blocked;return false; }
        for (int i=1;i<count;++i) pending[i-1]=pending[i];
        --count;++stats[id].rendered;
        return true;
    }
};
}

void HUD_CommitWeaponFlash();
void HUD_ClearWeaponFlash();
void HUD_WeaponFireFeedbackDump();
