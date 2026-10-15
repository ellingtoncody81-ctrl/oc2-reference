#pragma once
namespace match_status
{
void Init();
void Reset();
void Health(int value, int player, float time);
void Armor(int value);
void Money(int value, int delta, float fade, bool blink);
void Ammo(int clip, int reserve, int secondary);
void Dump();
}
