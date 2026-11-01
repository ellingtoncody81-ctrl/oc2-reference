#include "precompiled.h"
#include "oc2_economy.h"
#include "oc2_bot_control.h"

namespace { oc2_economy::LossTier ct, t; }
int OC2_KillAward(entvars_t *attacker, entvars_t *inflictor)
{
    if (inflictor && FClassnameIs(inflictor,"weapon_zeus")) return 0;
    CBaseEntity *entity = CBaseEntity::Instance(attacker);
    if (attacker && attacker == inflictor && entity && entity->IsPlayer())
    {
        auto *weapon = static_cast<CBasePlayerWeapon *>(static_cast<CBasePlayer *>(entity)->m_pActiveItem);
        if (weapon) return oc2_economy::KillAward(weapon->m_iId);
    }
    return 300; // delayed grenade/fire kills never borrow the gun held at death
}
void OC2_EconomyRound(CHalfLifeMultiplay *rules, bool reset)
{
    if (reset) { ct = {}; t = {}; return; }
    if (rules->m_iRoundWinStatus == WINSTATUS_CTS)
    {
        ct.Win();
        if (!rules->m_bMapHasEscapeZone) rules->m_iAccountTerrorist += t.Lose();
    }
    else if (rules->m_iRoundWinStatus == WINSTATUS_TERRORISTS)
    {
        t.Win();
        rules->m_iAccountCT += ct.Lose();
    }
    rules->m_iNumConsecutiveCTLoses = ct.level;
    rules->m_iNumConsecutiveTerroristLoses = t.level;
}
void OC2_SuicideAward(CBasePlayer *victim)
{
    auto *rules = CSGameRules();
    if (!victim || !rules || !rules->IsGameStarted() || rules->IsFreezePeriod() || rules->m_bRoundTerminating
        || (victim->m_iTeam != CT && victim->m_iTeam != TERRORIST)) return;
    CBasePlayer *best = nullptr;
    int award = 0;
    for (int attempt=0; attempt<3 && !best; ++attempt)
        for (int i=1; i<=gpGlobals->maxClients; ++i)
        {
            auto *p = UTIL_PlayerByIndex(i);
            if (!UTIL_IsValidPlayer(p) || p->has_disconnected || !p->IsAlive() || OC2_BotParked(p)
                || p->m_iTeam == victim->m_iTeam || (p->m_iTeam != CT && p->m_iTeam != TERRORIST)
                || (attempt < 1 && p->IsBot()) || (attempt < 2 && OC2_ControlledBot(p))) continue;
            int amount = 300;
            for (int slot : {PRIMARY_WEAPON_SLOT, PISTOL_SLOT})
                for (auto *item = p->m_rgpPlayerItems[slot]; item; item = item->m_pNext)
                    amount = Q_max(amount, oc2_economy::KillAward(static_cast<CBasePlayerWeapon *>(item)->m_iId));
            if (!best || amount > award || (amount == award && p->m_iAccount < best->m_iAccount))
            { best = p; award = amount; }
        }
    if (best) best->AddAccount(award, RT_ENEMY_KILLED);
}
