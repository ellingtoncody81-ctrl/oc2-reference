#pragma once

// Server snapshots are private to observers. A null result means unavailable,
// stale, or (for the target) that the caller's current target changed.
namespace oc2_spec {
struct Target {
	int playerIndex, health, armor, weaponId, clip, reserve;
	unsigned int weapons; // GoldSrc weapon-ID bit mask, not grenade quantities
	int money, score, deaths, armorType, equipment;
};
enum Equipment { Defuser = 1, Bomb = 2, NightVision = 4, Knife = 8, Taser = 16, Incendiary = 32, Shield = 64 };
struct Radar { int playerIndex, team; float x, y, z; };
struct Round { int completedRounds, maxRounds, aliveT, aliveCT; };
struct PlayerRow {
	int playerIndex, userId, team, alive, health, armor, weaponId, armorType, equipment, money;
	unsigned int weapons;
	int kills, assists, deaths, rounds;
	float damage, adr; // health damage to opponents; rounds joined as T/CT
	int he, flash, smoke, fire;
};

const Target *GetTarget(int currentTargetIndex);
const Radar *GetRadar(int playerIndex);
const Round *GetRound();
const PlayerRow *GetPlayerRow(int playerIndex);
}
