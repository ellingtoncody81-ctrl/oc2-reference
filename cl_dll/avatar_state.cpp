#include "avatar_state.h"

namespace avatar_state
{
	namespace
	{
		unsigned int NextRevision( unsigned int revision )
		{
			++revision;
			return revision == 0 ? 1 : revision;
		}
		bool SamePlayer( const PlayerState &a, const PlayerState &b )
		{
			return a.connected == b.connected && a.team == b.team && a.dead == b.dead &&
				a.local == b.local && a.identity == b.identity && a.avatar_kind == b.avatar_kind &&
				a.identity_revision == b.identity_revision;
		}
	}

	AvatarKind DefaultAvatar( int team )
	{
		if( team == kTerrorist ) return kAvatarT;
		if( team == kCounterTerrorist ) return kAvatarCT;
		return kAvatarNone;
	}

	unsigned long long MakeIdentity( int user_id, const char *name )
	{
		if( user_id == 0 && ( !name || !*name ) ) return 0;
		// FNV-1a identifies the caller's current userID/name pair. It is a local
		// change token only: it does not authenticate users or imply Steam support.
		unsigned long long hash = 14695981039346656037ULL;
		const unsigned int id = static_cast<unsigned int>( user_id );
		for( int byte = 0; byte < 4; ++byte )
		{
			hash ^= ( id >> ( byte * 8 ) ) & 0xffu;
			hash *= 1099511628211ULL;
		}
		hash ^= 0xffu;
		hash *= 1099511628211ULL;
		if( name )
		{
			for( const unsigned char *p = reinterpret_cast<const unsigned char *>( name ); *p; ++p )
			{
				hash ^= *p;
				hash *= 1099511628211ULL;
			}
		}
		return hash == 0 ? 1 : hash;
	}

	bool Update( State &state, const Snapshot &snapshot )
	{
		bool changed = false;
		state.players[0] = PlayerState();
		state.terrorists = Roster();
		state.counter_terrorists = Roster();
		for( int slot = 1; slot <= kMaxPlayers; ++slot )
		{
			const PlayerInput &input = snapshot.players[slot];
			const PlayerState &old = state.players[slot];
			PlayerState next;
			next.identity_revision = old.identity_revision;
			if( input.connected )
			{
				next.connected = true;
				next.team = input.team;
				next.dead = input.dead;
				next.local = input.local;
				next.identity = input.identity;
				next.avatar_kind = DefaultAvatar( input.team );
				next.identity_reset = !old.connected || old.identity != input.identity;
			}
			else next.identity_reset = old.connected;
			if( next.identity_reset ) next.identity_revision = NextRevision( old.identity_revision );
			if( !SamePlayer( old, next ) ) changed = true;
			state.players[slot] = next;

			// Ordinary CS:GO mini-scoreboard scans indices, retaining dead players
			// in their slots. Kills/deaths never enter this roster ordering.
			Roster *roster = next.avatar_kind == kAvatarT ? &state.terrorists :
				( next.avatar_kind == kAvatarCT ? &state.counter_terrorists : nullptr );
			if( next.connected && roster )
			{
				roster->slots[roster->count++] = slot;
				if( !next.dead ) ++roster->alive;
			}
		}
		return changed;
	}

	void Clear( State &state ) { state = State(); }
	const PlayerState *GetPlayer( const State &state, int slot )
	{
		return slot >= 1 && slot <= kMaxPlayers ? &state.players[slot] : nullptr;
	}
}
