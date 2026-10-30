#include "hud.h"
#include "cl_util.h"
#include "cl_dll.h"
#include "parsemsg.h"
#include "cl_entity.h"
#include "com_model.h"
#include "studio.h"
#include "r_studioint.h"
#include "oc2_hostage.h"
#include <cstdio>
#include <cstring>

extern engine_studio_api_t IEngineStudio;

namespace oc2_hostage
{
namespace
{
struct WorldSample { int entity = 0; char model[128] = {}; Vector origin; float at = -1000; };
WorldSample worldSamples[64];
}
void WorldDrawn(int entity, const char *model, const Vector &origin, float now)
{
    if (entity <= 0 || !model || (!strstr(model, "hostage") && !strstr(model, "scientist"))) return;
    for (auto &s : worldSamples)
        if (!s.entity || s.entity == entity)
        {
            s.entity = entity; std::snprintf(s.model, sizeof(s.model), "%s", model);
            s.origin = origin; s.at = now; return;
        }
}
namespace
{
State states[MAX_HOSTAGES + 1];
State staged[MAX_HOSTAGES + 1];
bool snapshot = false, epochKnown = false, waitingRound = false;
unsigned short epoch = 0, stagingEpoch = 0;
int lastVersion = 0;
int viewIndex = 0, viewUserid = 0;
float viewStart = 0, drawnAt = -1, drawnFrame = 0;
int drawnSequence = -1;
bool armDrawn = false;
bool radarShown[MAX_HOSTAGES + 1] = {};
float radarDrawTime = -1;

bool Newer(unsigned short a, unsigned short b)
{
    return a != b && static_cast<unsigned short>(a - b) < 0x8000;
}
float Duration(int state)
{
    return state == Dead ? 10.0f : state == Rescued ? 2.5f : state == LegacyTerminal ? 1.5f : 0.0f;
}
void ClearView()
{
    viewIndex = viewUserid = 0;
    viewStart = 0;
}
void ClearLegacy()
{
    for (int i = 0; i <= MAX_HOSTAGES; ++i) g_HostageInfo[i] = hostage_info_t{};
}
void SetState(State &dst, const State &previous, int status, int carrier, int userid, const Vector &origin, float now)
{
    dst.valid = true;
    dst.extended = true;
    dst.hasPosition = status != Hidden;
    dst.status = status;
    dst.carrier = carrier;
    dst.userid = userid;
    dst.origin = origin;
    const float duration = Duration(status);
    const bool sameTerminal = duration > 0 && previous.valid && previous.extended && previous.status == status && previous.expires > 0;
    dst.terminalAt = duration > 0 ? (sameTerminal ? previous.terminalAt : now) : 0;
    dst.expires = duration > 0 ? (sameTerminal ? previous.expires : now + duration) : 0;
}
void SyncLegacy(int index, const State &previous)
{
    const State &s = states[index];
    hostage_info_t &legacy = g_HostageInfo[index];
    if (!s.valid || !s.hasPosition) { legacy = hostage_info_t{}; return; }
    legacy.origin = s.origin;
    const bool terminal = s.status == Dead || s.status == Rescued;
    if (!previous.valid || previous.status != s.status)
    {
        legacy.radarflashtime = gHUD.m_flTime;
        legacy.radarflashtimedelta = terminal ? 0.1f : 0.5f;
        legacy.radarflashes = terminal ? 15 : 99999;
        legacy.nextflash = true;
    }
    legacy.dead = terminal;
}
int Message(const char *, int size, void *buffer)
{
    if (!buffer || size != 17) return 1;
    BufferReader r("OC2Hostage", buffer, size);
    const int version = r.ReadByte(), kind = r.ReadByte();
    const unsigned short incoming = static_cast<unsigned short>(r.ReadShort());
    const int index = r.ReadByte(), status = r.ReadByte(), carrier = r.ReadByte();
    const int userid = r.ReadLong();
    const float x = r.ReadCoord(), y = r.ReadCoord(), z = r.ReadCoord();
    const Vector origin(x, y, z);
    if (version != 1 || kind > 2) return 1;
    if (kind != 1 && (index || status || carrier || userid || origin != Vector(0, 0, 0))) return 1;
    if (kind == 1 && (index < 1 || index > MAX_HOSTAGES || (status > Rescued && status != Hidden) || carrier > 32 || userid < 0 ||
        ((carrier == 0) != (userid == 0)) || (status == Carried && carrier == 0) ||
        (status == Hidden && (carrier || userid || origin != Vector(0, 0, 0))))) return 1;
    lastVersion = version;
    if (kind == 0)
    {
        if (epochKnown && ((waitingRound && !Newer(incoming, epoch)) ||
            (!waitingRound && incoming != epoch && !Newer(incoming, epoch)))) return 1;
        if (snapshot && incoming != stagingEpoch && !Newer(incoming, stagingEpoch)) return 1;
        for (int i = 0; i <= MAX_HOSTAGES; ++i) staged[i] = State{};
        stagingEpoch = incoming;
        snapshot = true;
        return 1;
    }
    if (kind == 2)
    {
        if (!snapshot || incoming != stagingEpoch) return 1;
        for (int i = 1; i <= MAX_HOSTAGES; ++i)
        {
            const State previous = epochKnown && incoming == epoch ? states[i] : State{};
            states[i] = staged[i];
            SyncLegacy(i, previous);
        }
        epoch = incoming; epochKnown = true; waitingRound = false; snapshot = false;
        return 1;
    }
    if (snapshot)
    {
        if (incoming != stagingEpoch) return 1;
        const State &previous = staged[index].valid ? staged[index] :
            (epochKnown && incoming == epoch ? states[index] : State{});
        SetState(staged[index], previous, status, carrier, userid, origin, gEngfuncs.GetClientTime());
        return 1;
    }
    if (waitingRound || !epochKnown || incoming != epoch) return 1;
    State previous = states[index];
    SetState(states[index], previous, status, carrier, userid, origin, gEngfuncs.GetClientTime());
    SyncLegacy(index, previous);
    return 1;
}
void Dump()
{
    const float now = gEngfuncs.GetClientTime();
    for (const auto &s : worldSamples)
        if (s.entity && now >= s.at && now - s.at < 5.0f)
            gEngfuncs.Con_Printf("hostage_world: entity=%d model=%s drawn_at=%.3f age=%.3f origin=%.1f,%.1f,%.1f\n",
                s.entity, s.model, s.at, now-s.at, s.origin.x, s.origin.y, s.origin.z);
    const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
    const int slot = local ? local->index : 0;
    const player_info_t *info = slot > 0 && slot <= 32 && IEngineStudio.PlayerInfo ? IEngineStudio.PlayerInfo(slot - 1) : nullptr;
    const int userid = info ? info->userid : 0;
    int carried = 0;
    for (int i = 1; i <= MAX_HOSTAGES; ++i)
        if (states[i].valid && states[i].extended && states[i].status == Carried &&
            states[i].carrier == slot && userid > 0 && states[i].userid == userid) { carried = i; break; }
    gEngfuncs.Con_Printf("hostage: time=%.3f epoch=%u extended=%d local=%d userid=%d carried=%d arm_drawn=%d arm_model=%s draw_time=%.3f arm_sequence=%d arm_frame=%.3f version=%d epoch_known=%d snapshot=%d staging_epoch=%u waiting_round=%d radar_draw_time=%.3f\n",
        now, unsigned(epoch), int(epochKnown), slot, userid, carried, int(armDrawn),
        armDrawn ? "models/hostage/v_hostage_arm.mdl" : "none", drawnAt, drawnSequence, drawnFrame,
        lastVersion, int(epochKnown), int(snapshot), unsigned(stagingEpoch), int(waitingRound), radarDrawTime);
    for (int i = 1; i <= MAX_HOSTAGES; ++i)
    {
        const State &s = states[i];
        if (!s.valid) continue;
        char status[12];
        if (s.status == LegacyTerminal) std::strcpy(status, "unknown");
        else std::snprintf(status, sizeof(status), "%d", s.status);
        gEngfuncs.Con_Printf("hostage_state: id=%d state=%s carrier=%d userid=%d origin=%.1f,%.1f,%.1f terminal_at=%.3f hide_at=%.3f shown=%d extended=%d has_position=%d\n",
            i, status, s.carrier, s.userid, s.origin.x, s.origin.y, s.origin.z, s.terminalAt, s.expires,
            int(radarShown[i]), int(s.extended), int(s.hasPosition));
    }
}
}

void Init()
{
    MapReset();
    gEngfuncs.pfnHookUserMsg("OC2Hostage", Message);
    gEngfuncs.pfnAddCommand("hostage_dump", Dump);
}
void MapReset()
{
    for (auto &sample : worldSamples) sample = WorldSample{};
    for (int i = 0; i <= MAX_HOSTAGES; ++i) { states[i] = State{}; staged[i] = State{}; }
    snapshot = epochKnown = waitingRound = false;
    epoch = stagingEpoch = 0; lastVersion = 0;
    armDrawn = false; drawnAt = -1; drawnSequence = -1; drawnFrame = 0;
    std::memset(radarShown, 0, sizeof(radarShown)); radarDrawTime = -1;
    ClearLegacy(); ClearView();
}
void RoundReset()
{
    for (int i = 0; i <= MAX_HOSTAGES; ++i) { states[i] = State{}; staged[i] = State{}; }
    snapshot = false; waitingRound = epochKnown;
    armDrawn = false;
    std::memset(radarShown, 0, sizeof(radarShown)); radarDrawTime = -1;
    ClearLegacy(); ClearView();
}
bool LegacyPosition(int index, const Vector &origin, float)
{
    if (index < 1 || index > MAX_HOSTAGES || states[index].extended ||
        (states[index].valid && states[index].status == LegacyTerminal)) return false;
    State &s = states[index];
    s.valid = s.hasPosition = true; s.status = Idle; s.origin = origin; s.terminalAt = s.expires = 0;
    return true;
}
bool LegacyEnd(int index, float now)
{
    if (index < 1 || index > MAX_HOSTAGES || states[index].extended) return false;
    State &s = states[index];
    if (!s.valid || s.status != LegacyTerminal) { s.terminalAt = now; s.expires = now + Duration(LegacyTerminal); }
    s.valid = true; s.status = LegacyTerminal;
    return true;
}
const State &Get(int index)
{
    return states[index >= 1 && index <= MAX_HOSTAGES ? index : 0];
}
void BeginRadarFrame(float now)
{
    std::memset(radarShown, 0, sizeof(radarShown)); radarDrawTime = now;
}
void MarkShown(int index)
{
    if (index > 0 && index <= MAX_HOSTAGES) radarShown[index] = true;
}
bool ViewPose(float now, int &sequence, float &frame)
{
    armDrawn = false;
    const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
    const int slot = local ? local->index : 0;
    int team = slot > 0 && slot <= 32 ? g_PlayerExtraInfo[slot].teamnumber : 0;
    if (team <= 0 && slot > 0 && slot <= 32) team = g_PlayerInfoList[slot].team;
    const player_info_t *info = slot > 0 && slot <= 32 && IEngineStudio.PlayerInfo ? IEngineStudio.PlayerInfo(slot - 1) : nullptr;
    int found = 0;
    if (team == TEAM_CT && !gHUD.m_fPlayerDead && !g_iUser1 && !gEngfuncs.IsSpectateOnly() && !CL_IsThirdPerson() && info && info->userid > 0)
        for (int i = 1; i <= MAX_HOSTAGES; ++i)
            if (states[i].valid && states[i].extended && states[i].status == Carried &&
                states[i].carrier == slot && states[i].userid == info->userid) { found = i; break; }
    if (!found) { ClearView(); return false; }
    if (viewIndex != found || viewUserid != info->userid || now < viewStart)
    { viewIndex = found; viewUserid = info->userid; viewStart = now; }
    const float elapsed = now - viewStart;
    sequence = elapsed < 28.0f / 30.0f ? 1 : 0;
    frame = sequence ? elapsed * 30.0f : 0.0f;
    return true;
}
void ViewDrawn(float now, int sequence, float frame)
{
    armDrawn = true; drawnAt = now; drawnSequence = sequence; drawnFrame = frame;
}
}
