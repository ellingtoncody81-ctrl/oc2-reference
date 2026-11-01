#include "precompiled.h"
#include "oc2_match_rules.h"
#include "oc2_bot_control.h"
#include <utility>

// CS:GO 2019 competitive CFG and cs_gamerules phase changes, adapted to GoldSrc.
namespace
{
cvar_t competitive={"oc2_competitive","1",FCVAR_SERVER};
cvar_t halftime={"mp_halftime","1",FCVAR_SERVER};
cvar_t halfDuration={"mp_halftime_duration","15",FCVAR_SERVER};
bool halfDone=false;
bool halfWaiting=false;
int completedRounds=0;
bool endRecorded=false;
struct Pending { EHANDLE human,bot; int humanId=0,botId=0,from=0,to=0; };
Pending pending[33];
bool Valid(CBasePlayer *p) { return UTIL_IsValidPlayer(p)&&!p->has_disconnected; }
int Count(int team)
{
    int n=0;
    for(int i=1;i<=gpGlobals->maxClients;++i)
    { auto *p=UTIL_PlayerByIndex(i); if(Valid(p)&&p->m_iTeam==team)++n; }
    return n;
}
void Recount(CHalfLifeMultiplay *r) { r->m_iNumCT=Count(CT);r->m_iNumTerrorist=Count(TERRORIST); }
void ClearEquipment(CBasePlayer *p)
{
    p->RemoveAllItems(TRUE);p->m_bNotKilled=false;p->m_bReceivesNoMoneyNextRound=false;
}
bool Reserved(CBasePlayer *bot)
{
    for(const auto &q:pending)if(q.human&&q.bot==bot)return true;
    return false;
}
void ApplyRequests(CHalfLifeMultiplay *r)
{
    for(auto &q:pending)
    {
        auto *human=static_cast<CBasePlayer *>(static_cast<CBaseEntity *>(q.human));
        auto *bot=static_cast<CBasePlayer *>(static_cast<CBaseEntity *>(q.bot));
        if(Valid(human)&&Valid(bot)&&!human->IsBot()&&bot->IsBot()
            &&GETPLAYERUSERID(human->edict())==q.humanId&&GETPLAYERUSERID(bot->edict())==q.botId
            &&human->m_iTeam==q.from&&bot->m_iTeam==q.to&&!OC2_BotParked(bot))
        {
            OC2_BotControlTeam(human);OC2_BotControlTeam(bot);
            ClearEquipment(human);ClearEquipment(bot);
            human->ResetMenu();bot->ResetMenu();
            bot->SwitchTeam();human->SwitchTeam();
            human->m_bTeamChanged=bot->m_bTeamChanged=false;
            ClientPrint(human->pev,HUD_PRINTCENTER,u8"\u5df2\u4e0e\u673a\u5668\u4eba\u4ea4\u6362\u961f\u4f0d");
            ALERT(at_console,"oc2match_swap: human=%d bot=%d from=%d to=%d quota=%.0f\n",human->entindex(),bot->entindex(),q.from,q.to,cv_bot_quota.value);
        }
        q={};
    }
    Recount(r);
}
}
bool OC2_MatchEnabled()
{
    return competitive.value!=0&&g_pGameRules&&g_pGameRules->IsMultiplayer()&&!CSGameRules()->IsCareer();
}
void OC2_MatchRegister()
{
    CVAR_REGISTER(&competitive);CVAR_REGISTER(&halftime);CVAR_REGISTER(&halfDuration);
    ADD_SERVER_COMMAND("oc2_match_dump",OC2_MatchDump);
}
int OC2_MatchChooseTeam(CBasePlayer *human,int team)
{
    if(!OC2_MatchEnabled()||!Valid(human)||human->IsBot()||gpGlobals->maxClients!=10
        ||(team!=CT&&team!=TERRORIST)||team==human->m_iTeam)return 0;
    if(Count(team)<5)return 0;
    if((human->m_iTeam!=CT&&human->m_iTeam!=TERRORIST)||human->m_bIsVIP)
    {ClientPrint(human->pev,HUD_PRINTCENTER,u8"\u76ee\u6807\u961f\u4f0d\u5df2\u6ee1");return -1;}
    if((human->m_bTeamChanged&&!human->IsAlive())
        ||(!Q_stricmp(humans_join_team.string,"CT")&&team!=CT)
        ||(!Q_stricmp(humans_join_team.string,"T")&&team!=TERRORIST))return -1;
    auto &q=pending[human->entindex()];q={};
    CBasePlayer *chosen=nullptr;
    for(int i=1;i<=gpGlobals->maxClients;++i)
    {
        auto *p=UTIL_PlayerByIndex(i);
        if(!Valid(p)||!p->IsBot()||p->m_iTeam!=team||p->m_bIsVIP||OC2_BotParked(p)||Reserved(p))continue;
        const auto *profile=static_cast<CCSBot *>(p)->GetProfile();
        if(profile&&!profile->IsValidForTeam(human->m_iTeam==CT?BOT_TEAM_CT:BOT_TEAM_T))continue;
        if(!chosen||(chosen->IsAlive()&&!p->IsAlive()))chosen=p;
    }
    if(!chosen)
    {ClientPrint(human->pev,HUD_PRINTCENTER,u8"\u76ee\u6807\u961f\u4f0d\u5df2\u6ee1\uff0c\u6ca1\u6709\u53ef\u4ea4\u6362\u7684\u673a\u5668\u4eba");return -1;}
    q.human=human;q.bot=chosen;q.humanId=GETPLAYERUSERID(human->edict());q.botId=GETPLAYERUSERID(chosen->edict());
    q.from=human->m_iTeam;q.to=team;
    ClientPrint(human->pev,HUD_PRINTCENTER,u8"\u5c06\u5728\u4e0b\u56de\u5408\u4e0e\u673a\u5668\u4eba\u4ea4\u6362\u961f\u4f0d");
    return 1;
}
bool OC2_MatchBeforeRound(CHalfLifeMultiplay *r)
{
    endRecorded=false;
    if(r->m_bCompleteReset){halfDone=halfWaiting=false;completedRounds=0;for(auto &q:pending)q={};return false;}
    if(!OC2_MatchEnabled())return false;
    const int rounds=completedRounds;
    const int half=Q_max(1,r->m_iMaxRounds/2);
    if(halftime.value&&r->m_iMaxRounds>0&&!halfDone&&rounds>=half)
    {
        halfDone=true;for(auto &q:pending)q={};
        for(int i=1;i<=gpGlobals->maxClients;++i)
        {
            auto *p=UTIL_PlayerByIndex(i);
            if(!Valid(p)||(p->m_iTeam!=CT&&p->m_iTeam!=TERRORIST))continue;
            ClearEquipment(p);p->SwitchTeam();
            p->m_iAccount=int(startmoney.value);p->AddAccount(0,RT_NONE,false);
        }
        std::swap(r->m_iNumCTWins,r->m_iNumTerroristWins);
        r->m_iAccountCT=r->m_iAccountTerrorist=0;r->m_iRoundWinStatus=WINSTATUS_NONE;
        r->UpdateTeamScores();Recount(r);
        UTIL_ClientPrintAll(HUD_PRINTCENTER,u8"\u4e0a\u534a\u573a\u7ed3\u675f\uff0c\u4ea4\u6362\u9635\u8425");
        ALERT(at_console,"oc2match_half: rounds=%d ct=%d t=%d startmoney=%d\n",rounds,r->m_iNumCTWins,r->m_iNumTerroristWins,int(startmoney.value));
        return true;
    }
    return false;
}
void OC2_MatchApplyRequests(CHalfLifeMultiplay *r){if(OC2_MatchEnabled())ApplyRequests(r);}
bool OC2_MatchTeamPending(CBasePlayer *p)
{
    return p&&p->entindex()>0&&p->entindex()<33&&pending[p->entindex()].human==p
        &&pending[p->entindex()].humanId==GETPLAYERUSERID(p->edict());
}
bool OC2_MatchThink(CHalfLifeMultiplay *r)
{
    if(!OC2_MatchEnabled()||!r->IsGameStarted()||r->m_bCompleteReset||!r->m_bRoundTerminating||r->m_flRestartRoundTime<=0)return false;
    if(r->m_iRoundWinStatus!=WINSTATUS_NONE&&!endRecorded)
    {++completedRounds;endRecorded=true;}
    if(gpGlobals->time<r->m_flRestartRoundTime)return false;
    const int rounds=completedRounds;
    const bool clinch=r->m_iMaxRoundsWon>0&&(r->m_iNumCTWins>=r->m_iMaxRoundsWon||r->m_iNumTerroristWins>=r->m_iMaxRoundsWon);
    if(!clinch&&(r->m_iMaxRounds<=0||rounds<r->m_iMaxRounds))
    {
        if(halftime.value&&!halfDone&&!halfWaiting&&r->m_iMaxRounds>0&&rounds>=r->m_iMaxRounds/2)
        {
            halfWaiting=true;r->m_flRestartRoundTime=gpGlobals->time+Q_max(0.0f,halfDuration.value);
            for(int i=1;i<=gpGlobals->maxClients;++i){auto *p=UTIL_PlayerByIndex(i);if(Valid(p))p->pev->flags|=FL_FROZEN;}
            UTIL_ClientPrintAll(HUD_PRINTCENTER,u8"\u534a\u573a\u4f11\u606f\uff0c\u5373\u5c06\u4ea4\u6362\u9635\u8425");
        }
        return false;
    }
    UTIL_ClientPrintAll(HUD_PRINTCENTER,r->m_iNumCTWins==r->m_iNumTerroristWins
        ?u8"\u6bd4\u8d5b\u7ed3\u675f\uff1a\u5e73\u5c40":u8"\u6bd4\u8d5b\u7ed3\u675f");
    ALERT(at_console,"oc2match_end: ct=%d t=%d draw=%d\n",r->m_iNumCTWins,r->m_iNumTerroristWins,r->m_iNumCTWins==r->m_iNumTerroristWins);
    r->GoToIntermission();return true;
}
void OC2_MatchRoundReady(CHalfLifeMultiplay *r)
{
    if(!OC2_MatchEnabled())return;
    if(halfDone&&r->m_iMaxRoundsWon>0&&(r->m_iNumCTWins==r->m_iMaxRoundsWon-1||r->m_iNumTerroristWins==r->m_iMaxRoundsWon-1))
        UTIL_ClientPrintAll(HUD_PRINTCENTER,u8"\u8d5b\u70b9\uff1a\u672c\u56de\u5408\u53ef\u51b3\u5b9a\u80dc\u8d1f");
}
bool OC2_MatchCommand(CBasePlayer *p,const char *command)
{
    if(Q_strcmp(command,"oc2_match_test"))return false;
    if(!p||p->entindex()!=1||IS_DEDICATED_SERVER()||CVAR_GET_FLOAT("sv_cheats")==0)return true;
    auto *r=CSGameRules();
    if(CMD_ARGC()==4&&!Q_strcmp(CMD_ARGV(1),"score"))
    {r->m_iNumCTWins=Q_max(0,Q_min(16,Q_atoi(CMD_ARGV(2))));r->m_iNumTerroristWins=Q_max(0,Q_min(16,Q_atoi(CMD_ARGV(3))));completedRounds=r->m_iNumCTWins+r->m_iNumTerroristWins;r->UpdateTeamScores();}
    if(CMD_ARGC()==3&&!Q_strcmp(CMD_ARGV(1),"rounds"))completedRounds=Q_max(0,Q_min(30,Q_atoi(CMD_ARGV(2))));
    return true;
}
void OC2_MatchDump()
{
    auto *r=CSGameRules();if(!r)return;
    ALERT(at_console,"oc2match: enabled=%d ct=%d t=%d ct_score=%d t_score=%d half=%d maxrounds=%d winlimit=%d roundtime=%d freeze=%d buytime=%.3f c4=%d terminating=%d gameover=%d rounds=%d\n",
        OC2_MatchEnabled(),Count(CT),Count(TERRORIST),r->m_iNumCTWins,r->m_iNumTerroristWins,halfDone,
        r->m_iMaxRounds,r->m_iMaxRoundsWon,r->m_iRoundTime,r->m_iIntroRoundTime,buytime.value*60,r->m_iC4Timer,r->m_bRoundTerminating,r->m_bGameOver,completedRounds);
}
