// Standalone regression: compile this file with a C++11 compiler and run it.
#include "../dlls/oc2_spec_stats.h"
#include <cassert>

int main()
{
	oc2_stats::Accumulator s;
	s.Participate(1); s.Participate(1); s.Participate(2);
	s.Damage(1, 4, 30); s.Damage(1, 4, 10); s.Damage(2, 4, 60);
	s.Death(2, 4, 1u << 0);
	assert(s.Get(1).rounds == 1 && s.Get(1).assists == 1 && s.Get(1).damage == 40);
	assert(s.Get(2).kills == 1 && s.Get(2).damage == 60 && s.Get(4).deaths == 1);
	s.Death(0, 4, 0); // world/suicide: death but no enemy kill or assist
	assert(s.Get(4).deaths == 2 && s.Get(1).assists == 1);
	s.Damage(1, 4, 45); s.Damage(3, 4, 55);
	s.Death(2, 4, (1u << 0) | (1u << 2));
	assert(s.Get(3).assists == 1 && s.Get(1).assists == 1);
	s.NextRound(false); s.Participate(1); s.Damage(1, 4, 39); s.Death(2, 4, 1u << 0);
	assert(s.Get(1).rounds == 2 && s.Get(1).assists == 1 && s.Get(1).damage == 124);
	s.Damage(1, 4, 100); s.ClearPlayer(1); s.Death(2, 4, 1u << 0);
	assert(s.Get(1).kills == 0 && s.Get(1).damage == 0 && s.Get(1).assists == 0);
	s.NextRound(true);
	assert(s.Get(2).kills == 0 && s.Get(2).rounds == 0 && s.Get(4).deaths == 0);
	return 0;
}
