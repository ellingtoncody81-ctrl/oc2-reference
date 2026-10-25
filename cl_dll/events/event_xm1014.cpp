/*
*
*    This program is free software; you can redistribute it and/or modify it
*    under the terms of the GNU General Public License as published by the
*    Free Software Foundation; either version 2 of the License, or (at
*    your option) any later version.
*
*    This program is distributed in the hope that it will be useful, but
*    WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*    General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*    In addition, as a special exception, the author gives permission to
*    link the code of this program with the Half-Life Game Engine ("HL
*    Engine") and Modified Game Libraries ("MODs") developed by Valve,
*    L.L.C ("Valve").  You must obey the GNU General Public License in all
*    respects for all of the code used other than the HL Engine and MODs
*    from Valve.  If you modify this file, you may extend this exception
*    to your version of the file, but you are not obligated to do so.  If
*    you do not wish to do so, delete this exception statement from your
*    version.
*
*/
#include "events.h"
#include "wpn_shared.h"
#include "../../dlls/wpn_shared/oc2_weapon_params.h"

static const char *SOUNDS_NAME = "weapons/xm1014-1.wav";

void EV_FireXM1014(event_args_t *args)
{
	const int idx = args->entindex;
	Vector origin(args->origin);
	// XM1014 sends complete shot angles; iparam1/2 carry seed/pattern index.
	Vector angles(args->angles);
	Vector velocity(args->velocity);
	Vector forward, right, up;
	AngleVectors(angles, forward, right, up);
	Vector shellVelocity, shellOrigin;
	if (EV_IsLocal(idx))
	{
		++g_iShotsFired;
		EV_MuzzleFlash();
		gEngfuncs.pEventAPI->EV_WeaponAnimation(Com_RandomLong(XM1014_FIRE1, XM1014_FIRE2), 2);
		const float side = gHUD.cl_righthand && gHUD.cl_righthand->value == 0.0f ? -10.0f : 10.0f;
		EV_GetDefaultShellInfo(args, origin, velocity, shellVelocity, shellOrigin,
			forward, right, up, 20.0f, -8.0f, side, false);
	}
	else
		EV_GetDefaultShellInfo(args, origin, velocity, shellVelocity, shellOrigin,
			forward, right, up, 20.0f, -12.0f, 4.0f, false);
	EV_EjectBrass(shellOrigin, shellVelocity, angles[YAW], g_iShotgunShell, TE_BOUNCE_SHOTSHELL);
	PLAY_EVENT_SOUND(SOUNDS_NAME);
	Vector source;
	EV_GetGunPosition(args, source, origin);
	const oc2_weapon::Params &params = oc2_weapon::Get(oc2_weapon::XM1014);
	oc2_weapon::Offset offsets[9];
	oc2_weapon::SpreadOffsets(params, 0, unsigned(args->iparam1) & 255u,
		args->fparam1 / 1024, float(args->iparam2), offsets);
	for (int i = 0; i < params.pellets; ++i)
	{
		Vector aim = (forward + offsets[i].x * right + offsets[i].y * up).Normalize();
		Vector zeroSpread(0, 0, 0);
		// Buckshot effects with zero additional Gaussian spread. Each visual
		// ray now follows the same seeded XM1014 pattern as the server pellet.
		EV_HLDM_FireBullets(idx, forward, right, up, 1, source, aim,
			zeroSpread, params.range, BULLET_PLAYER_BUCKSHOT, 1);
	}
}
