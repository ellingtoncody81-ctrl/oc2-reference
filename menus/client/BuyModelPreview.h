#pragma once
#include <cstdint>

struct cl_entity_s;

// CS1.6 team: 1=T, 2=CT. The viewport uses physical screen pixels.
// Returns false when this preview cannot be drawn, so the menu can fall back.
bool OC2_DrawBuyPreview(int team, int weaponId, int x, int y, int width, int height);
bool OC2_DrawLobbyPreview(int team, int weaponId, int x, int y, int width, int height);
// Team cards loop the same lobby idle, with an independent clock for each team.
bool OC2_DrawTeamPreview(int team, int weaponId, int x, int y, int width, int height);
void OC2_RestartTeamPreviews();
void OC2_DumpTeamPreviews();
// Call once when the home scene becomes visible. Selection changes also restart.
void OC2_RestartLobbyPreview();
// Effective inventory state; implemented beside the shared appearance cache.
std::uint64_t OC2_PreviewAppearanceSignature(int weaponId);
int OC2_LastPresentationSwaps();
void OC2_DumpPresentation(const char *tag);

// The front end and shop share the equipped catalog and private texture cache.
// Material indices are restored before either model can be used by the game.
int OC2_PreviewEquippedGloveBody(cl_entity_s &glove);
class OC2_PreviewAppearance
{
public:
    OC2_PreviewAppearance(cl_entity_s &weapon,cl_entity_s *glove,int weaponId);
    ~OC2_PreviewAppearance();
    int Swaps() const;
private:
    void *m_state;
    OC2_PreviewAppearance(const OC2_PreviewAppearance &)=delete;
    OC2_PreviewAppearance &operator=(const OC2_PreviewAppearance &)=delete;
};
