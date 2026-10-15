#pragma once

// CS:GO rules: server/cstrike15/cs_player.cpp:18442-18451,8363-8364;
// client/cstrike15/panorama/hud/csgo_hudweaponpanel.cpp:164-175,427-489.
// GoldSrc has DeathMsg rather than the CS:GO replicated round-kill fields.
// This state adapts authoritative DeathMsg events. CS2-style cards are a
// presentation choice; this does not claim a native CS:GO card mechanism.
namespace kill_feedback
{
	const int kMaxPlayers = 64;
	struct Event
	{
		float time = 0.0f;
		int local = 0, killer = 0, victim = 0;
		int killer_team = 0, victim_team = 0; // GoldSrc: T=1, CT=2
		bool headshot = false;
	};
	struct State
	{
		int round_kills = 0, round_headshots = 0;
		float last_kill_time = -1.0f;
		bool card_visible = false, local_dead = false;
	};
	bool OnDeath( State &state, const Event &event );
	// OC2 resource animation: 20fps, seven frames, final frame held. A card
	// remains until local death or reset; it has no invented timeout.
	int CardFrame( const State &state, float now );
	bool WeaponResourceName( const char *name );
	float NoticeOpacity( float now, float expires ); // final one-second fade
	void ResetRound( State &state );
	void ClearMap( State &state );
}

// Rendering adapter exported by death.cpp; pure-state tests need not link it.
float HUD_LocalKillFlash( float time );
void HUD_KillFeedbackDump();
void HUD_DrawKillCard(float time);
