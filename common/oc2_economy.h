#pragma once

// CS:GO 2019 competitive CFG and items_game kill awards, mapped to OC2 IDs.
namespace oc2_economy
{
constexpr int KillAward(int weapon)
{
    return weapon == 2 ? 0 : weapon == 18 ? 100 : weapon == 29 ? 1500
        : (weapon == 7 || weapon == 12 || weapon == 19 || weapon == 23) ? 600
        : (weapon == 5 || weapon == 21) ? 900 : 300;
}
constexpr int Plant = 300, Defuse = 300, TeamDefuse = 3500, TeamKill = -300;
constexpr int HostageTouch = 300, HostageTouchTeam = 600, HostageRescueTeam = 600;
constexpr int HostageVictory = 2900, HostageDamage = 30;
struct LossTier
{
    int level = 1;
    int Lose() { const int award = 1400+500*level; if (level < 4) ++level; return award; }
    void Win() { if (level > 0) --level; }
};
}
