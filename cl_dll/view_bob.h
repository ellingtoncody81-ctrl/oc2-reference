/*
view_bob.h - CS:GO style weapon bob

Style 2 of the mobile build's bob. The classic Quake bob already in view.cpp is
untouched; this is the separate path cl_bobstyle 2 selects.

Transcribed from NextClient's view_bob package
(nextclient/packages/gameplay/view/src/view_bob.cpp), which was itself
reconstructed from the CSBTEM binary. The constants are not invented:
bob_scale, the 620 units/s^2 speed follow limit and the -8/-7/+4 clamps all come
from that binary.

The one deliberate deviation from the binary is the x1.25 on bob_cycle. NextClient
says so in their own comment: it was not in their build and is there so CS 1.6's
default cl_bobcycle of 0.8 lands on the right cadence. It is a fudge, not a
recovered constant, and is kept as-is for parity.

Verified bit for bit against the reference by view_bob_check.py before it was
compiled; see md/15-hud-module.md.
*/

#pragma once
#ifndef VIEW_BOB_H
#define VIEW_BOB_H

namespace view_bob
{
	// cl_bobstyle: 0 default Quake bob, 1 old-style sway, 2 this one
	enum
	{
		kStyleClassic = 0,
		kStyleClassicSway = 1,
		kStyleModern = 2
	};

	// units/s per second the bob's idea of the player's speed may change by.
	// Without this the offsets jump on the first frame of a run.
	const float kSpeedAccel = 620.0f;

	struct BobParams
	{
		float bob_cycle;    // cl_bobcycle, seconds per cycle; <= 0 disables the bob
		float bob_up;       // cl_bobup, fraction of the cycle spent rising, (0, 1)
		float amt_vert;     // cl_bobamt_vert, vertical amplitude
		float amt_lat;      // cl_bobamt_lat, lateral amplitude
		float lower_amt;    // cl_bob_lower_amt, speed-proportional lowering
	};

	struct BobState
	{
		float bob_time;
		float last_bob_time;
		float last_speed;
	};

	struct BobOffsets
	{
		float vert;         // raw vertical, world units, clamped to [-8, 4]
		float hor;          // raw lateral, world units, clamped to [-7, 4]
	};

	// time is absolute seconds, speed the horizontal speed in units/s.
	// Fills the state so consecutive calls track speed instead of snapping to it.
	BobOffsets StepBob( BobState &state, const BobParams &params,
	                    float time, float speed, bool onground );

	// The bob reaches the view model as three world-space offsets: how far along
	// the view axis, how far up, and how far to the side. Units are world units.
	struct Placement
	{
		float forward;
		float up;
		float side;
	};

	Placement PlaceBob( const BobOffsets &offsets );
}

#endif // VIEW_BOB_H
