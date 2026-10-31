#pragma once

// Standalone accumulator; player slots are 1..32. Damage is health lost.
namespace oc2_stats {
struct Player { int kills, assists, deaths, rounds; float damage; bool counted; };
class Accumulator {
public:
	Accumulator() { Reset(); }
	void Reset() { for (int i = 0; i <= 32; ++i) ClearPlayer(i); }
	void ClearPlayer(int i) {
		if (i < 0 || i > 32) return;
		players[i] = {}; for (int j = 0; j <= 32; ++j) hits[i][j] = hits[j][i] = 0;
	}
	void NextRound(bool full) {
		if (full) { Reset(); return; }
		for (int i = 0; i <= 32; ++i) { players[i].counted = false; for (int j = 0; j <= 32; ++j) hits[i][j] = 0; }
	}
	void Participate(int i) { if (valid(i) && !players[i].counted) { ++players[i].rounds; players[i].counted = true; } }
	void Damage(int attacker, int victim, float healthLoss) {
		if (!valid(attacker) || !valid(victim) || attacker == victim || healthLoss <= 0) return;
		Participate(attacker); Participate(victim);
		players[attacker].damage += healthLoss; hits[attacker][victim] += healthLoss;
	}
	// eligibleMask contains current teammates of the killer, excluding the killer.
	void Death(int killer, int victim, unsigned int eligibleMask) {
		if (!valid(victim)) return;
		Participate(victim); ++players[victim].deaths;
		if (valid(killer) && killer != victim) {
			Participate(killer); ++players[killer].kills;
			int best = 0; float highest = 39.9999f;
			for (int i = 1; i <= 32; ++i) {
				if (i != killer && (eligibleMask & (1u << (i - 1))) && hits[i][victim] >= 40.0f && hits[i][victim] > highest) {
					best = i; highest = hits[i][victim];
				}
			}
			if (best) ++players[best].assists;
		}
		for (int i = 1; i <= 32; ++i) hits[i][victim] = 0;
	}
	const Player &Get(int i) const { return players[valid(i) ? i : 0]; }
private:
	static bool valid(int i) { return i >= 1 && i <= 32; }
	Player players[33]; float hits[33][33];
};
}

class CBasePlayer;
class CBaseEntity;
void OC2_SpecStatsReset();
void OC2_SpecStatsClear(int slot);
void OC2_SpecStatsRound(bool full);
void OC2_SpecStatsParticipate(CBasePlayer *player);
void OC2_SpecStatsDamage(CBaseEntity *attacker, CBasePlayer *victim, float damage);
void OC2_SpecStatsDeath(CBasePlayer *victim, CBasePlayer *killer);
