#pragma once

// The legacy messages remain unchanged. OC2Hostage has a fixed 17-byte
// payload: version, kind, epoch, index, state, carrier, userid, x/y/z.
namespace oc2_hostage
{
enum Status { Idle = 0, Following = 1, Carried = 2, Dead = 3, Rescued = 4, LegacyTerminal = 5 };
struct State
{
    bool valid = false;
    bool extended = false;
    bool hasPosition = false;
    int status = Idle;
    int carrier = 0;
    int userid = 0;
    Vector origin = Vector(0, 0, 0);
    float terminalAt = 0;
    float expires = 0;
};
void Init();
void MapReset();
void RoundReset();
bool LegacyPosition(int index, const Vector &origin, float time);
bool LegacyEnd(int index, float time);
const State &Get(int index);
void BeginRadarFrame(float now);
void MarkShown(int index);
bool ViewPose(float now, int &sequence, float &frame);
void ViewDrawn(float now, int sequence, float frame);
}
