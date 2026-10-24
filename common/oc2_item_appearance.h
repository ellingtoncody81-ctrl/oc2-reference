#pragma once

// Server-issued weapon snapshots. Serial is a match-local identity, independent
// of local inventory IDs and player slots; transfers retain the same snapshot.
namespace oc2_item
{
static const int kVersion = 1;
static const char *const kMessage = "OC2Item";
static const int kResetBytes = 2, kRemoveBytes = 6, kUpsertBytes = 39;
enum Operation { Reset = 0, Upsert = 1, Remove = 2 };
enum Flags { Active = 1, BotOrigin = 2, OriginOwner = 4 };
struct Appearance
{
 int serial = 0, entity = 0, owner = 0, box = 0, weapon = 0;
 int definition = 0, skin = 0, stickers[4] = {};
 int originUserid = 0, flags = 0;
};
inline bool Equal(const Appearance &a, const Appearance &b)
{
 if(a.serial!=b.serial||a.entity!=b.entity||a.owner!=b.owner||a.box!=b.box||
    a.weapon!=b.weapon||a.definition!=b.definition||a.skin!=b.skin||
    a.originUserid!=b.originUserid||a.flags!=b.flags)return false;
 for(int i=0;i<4;++i)if(a.stickers[i]!=b.stickers[i])return false;
 return true;
}
// OC2Item: BYTE version, BYTE operation; Remove: LONG serial.
// Upsert: LONG serial, SHORT entity, BYTE owner, SHORT box, BYTE weapon,
// SHORT definition, LONG skin, LONG stickers[4], LONG originUserid, BYTE flags.
// Loadout transaction (all IDs checked against the server catalog):
// oc2_loadout begin <revision>; then each SerializeConfig row prefixed by
// oc2_loadout (gun/knife/equip_knife); oc2_loadout end. resync requests a snapshot.
}
