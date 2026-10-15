#pragma once

// Shared state for top-bar and Tab avatars; no engine or renderer types.
// CS:GO reference (D:/RLRLJYRSBJQRS/cstrike19_src/src/game/client/cstrike15/):
// panorama/hud/csgo_hudteamcounter.cpp:2329-2340,2434-2465 (index-order roster),
// 2400-2403 (living counts),2626-2652 (clear unused slots).
// panorama/csgo_avatarimage.cpp:104-131 (clear reused identities).
// Team numbers and the 64-slot capacity below are CS1.6/Xash adapters.
namespace avatar_state
{
	const int kMaxPlayers = 64;
	enum Team { kUnassigned = 0, kTerrorist = 1, kCounterTerrorist = 2, kSpectator = 3 };
	enum AvatarKind { kAvatarNone = 0, kAvatarT = 1, kAvatarCT = 2 };

	struct PlayerInput
	{
		bool connected = false;
		int team = kUnassigned;
		bool dead = false, local = false;
		unsigned long long identity = 0; // caller token; never treated as a SteamID
	};
	struct Snapshot
	{
		PlayerInput players[kMaxPlayers + 1]; // index 0 reserved, slots 1..64
	};
	struct PlayerState
	{
		bool connected = false;
		int team = kUnassigned;
		bool dead = false, local = false;
		unsigned long long identity = 0;
		AvatarKind avatar_kind = kAvatarNone;
		unsigned int identity_revision = 0;
		bool identity_reset = false; // one-update flag: new, replaced or disconnected
	};
	struct Roster
	{
		int slots[kMaxPlayers] = {};
		int count = 0, alive = 0;
	};
	struct State
	{
		PlayerState players[kMaxPlayers + 1];
		Roster terrorists, counter_terrorists;
	};

	AvatarKind DefaultAvatar( int team );
	unsigned long long MakeIdentity( int user_id, const char *name );
	// Returns whether semantic state changed. identity_reset is cleared at the
	// next update even if Update returns false; disconnected fields never linger.
	bool Update( State &state, const Snapshot &snapshot );
	void Clear( State &state );
	const PlayerState *GetPlayer( const State &state, int slot );
}
