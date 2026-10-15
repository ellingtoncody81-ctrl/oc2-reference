#include "kill_feedback.h"
#include <cmath>
#include <limits>

namespace kill_feedback
{
	namespace
	{
		bool Player( int slot ) { return slot >= 1 && slot <= kMaxPlayers; }
		bool Team( int team ) { return team == 1 || team == 2; }
	}
	bool OnDeath( State &state, const Event &event )
	{
		if( !Player( event.local ) || !std::isfinite( event.time ) ) return false;
		if( event.victim == event.local )
		{
			state.local_dead = true;
			state.card_visible = false;
		}
		if( event.killer != event.local || !Player( event.victim ) || event.killer == event.victim ||
			!Team( event.killer_team ) || !Team( event.victim_team ) || event.killer_team == event.victim_team )
			return false;
		if( state.round_kills < ( std::numeric_limits<int>::max )() ) ++state.round_kills;
		if( event.headshot && state.round_headshots < state.round_kills ) ++state.round_headshots;
		state.last_kill_time = event.time;
		state.card_visible = !state.local_dead;
		return true;
	}
	int CardFrame( const State &state, float now )
	{
		if( !state.card_visible || !std::isfinite( now ) || now < state.last_kill_time ) return -1;
		const float elapsed = now - state.last_kill_time;
		return elapsed >= 0.3f ? 6 : static_cast<int>( elapsed * 20.0f );
	}
	bool WeaponResourceName( const char *name )
	{
		if( !name || !*name ) return false;
		int length = 0;
		for( const unsigned char *p = reinterpret_cast<const unsigned char *>( name ); *p; ++p )
		{
			if( ++length > 31 || !( ( *p >= 'a' && *p <= 'z' ) || ( *p >= 'A' && *p <= 'Z' ) ||
				( *p >= '0' && *p <= '9' ) || *p == '_' ) ) return false;
		}
		return true;
	}
	float NoticeOpacity( float now, float expires )
	{
		if( !std::isfinite( now ) || !std::isfinite( expires ) ) return 0.0f;
		const float remaining = expires-now;
		return remaining <= 0.0f ? 0.0f : ( remaining >= 1.0f ? 1.0f : remaining );
	}
	void ResetRound( State &state ) { state = State(); }
	void ClearMap( State &state ) { state = State(); }
}
