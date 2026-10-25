#pragma once
#include "oc2_skin_inventory.h"

// Item appearance belongs to a (weapon, skin) choice. The original two DATs
// remain the equipped projection consumed by the client and server.
namespace oc2_profiles
{
static const char *const kConfigPath="oc2_inventory_profiles.dat";
static const std::size_t kMaximumConfigBytes=4*1024*1024;
static const std::size_t kMaximumProfiles=8192;
struct Profile
{
 oc2_inventory::WeaponState appearance;
 oc2_accessories::WeaponConfig accessories;
};
struct Config { unsigned revision=1;std::vector<Profile> items; };
inline Profile DefaultProfile(int weapon,int skin)
{
 Profile p;const int i=oc2_accessories::Index(weapon);
 if(i>=0)
 {
  p.appearance=oc2_inventory::DefaultConfig().weapons[i];p.appearance.skin=skin;
  p.accessories=oc2_accessories::DefaultConfig().weapons[i];
 }
 return p;
}
inline Profile *Find(Config &c,int weapon,int skin)
{ for(auto &p:c.items)if(p.appearance.weapon==weapon&&p.appearance.skin==skin)return &p;return nullptr; }
inline const Profile *Find(const Config &c,int weapon,int skin)
{ for(const auto &p:c.items)if(p.appearance.weapon==weapon&&p.appearance.skin==skin)return &p;return nullptr; }
inline bool Valid(const Profile &p)
{
 if(!p.appearance.instance||p.appearance.skin<0||p.appearance.weapon!=p.accessories.weapon||
    !oc2_accessories::Valid(p.accessories)||p.appearance.charm< -1)return false;
 for(int sticker:p.appearance.stickers)if(sticker< -1)return false;
 return true;
}
inline bool Put(Config &c,const Profile &p)
{
 if(!Valid(p))return false;
 if(Profile *old=Find(c,p.appearance.weapon,p.appearance.skin)){*old=p;return true;}
 if(c.items.size()>=kMaximumProfiles)return false;
 c.items.push_back(p);return true;
}
inline Profile EquippedProfile(const oc2_inventory::WeaponState &w,const oc2_accessories::WeaponConfig &a)
{
 Profile p;p.appearance=w;p.accessories=a;
 // The old loadout override could force a charm on/off independently of the
 // old attachment switch. Preserve that effective state during migration.
 if(w.charm>0)p.accessories.charm=true;
 else if(w.charm<0)p.accessories.charm=false;
 return p;
}
inline void InheritEquipped(Config &c,const oc2_inventory::Config &loadout,const oc2_accessories::Config &accessories)
{
 // Migration is only for the exact equipped skin. Other skins start with
 // original stickers and disabled attachments, even on the same weapon.
 for(const auto &w:loadout.weapons)if(!Find(c,w.weapon,w.skin))
 {
  Put(c,EquippedProfile(w,*oc2_accessories::Find(accessories,w.weapon)));
 }
}
inline Profile Resolve(const Config &c,const oc2_inventory::Config &loadout,
                       const oc2_accessories::Config &accessories,int weapon,int skin)
{
 if(const Profile *p=Find(c,weapon,skin))return *p;
 Profile p=DefaultProfile(weapon,skin);
 const auto *active=oc2_inventory::Find(loadout,weapon);
 if(active&&active->skin==skin)p=EquippedProfile(*active,*oc2_accessories::Find(accessories,weapon));
 return p;
}
inline bool Validate(const Profile &p,const oc2_inventory::Catalog &catalog,std::string &error)
{
 if(!Valid(p)){error="Invalid skin customization";return false;}
 const auto *skin=oc2_inventory::FindEntry(catalog,p.appearance.skin);
 if(p.appearance.skin&&(!skin||skin->kind!=oc2_inventory::Kind::GunSkin||skin->weapon!=p.appearance.weapon))
  {error="Skin does not match the weapon";return false;}
 for(int sticker:p.appearance.stickers)
 {
  const auto *entry=oc2_inventory::FindEntry(catalog,sticker);
  if(sticker>0&&(!entry||entry->kind!=oc2_inventory::Kind::Sticker)){error="Unknown sticker choice";return false;}
 }
 const auto *charm=oc2_inventory::FindEntry(catalog,p.appearance.charm);
 if(p.appearance.charm>0&&(!charm||charm->kind!=oc2_inventory::Kind::Sticker))
  {error="Unknown charm pattern";return false;}
 error.clear();return true;
}
inline bool Project(const Profile &p,oc2_inventory::Config &loadout,oc2_accessories::Config &accessories)
{
 if(!Valid(p))return false;
 auto *w=oc2_inventory::Find(loadout,p.appearance.weapon);
 auto *a=oc2_accessories::Find(accessories,p.appearance.weapon);
 if(!w||!a)return false;
 // Preserve the existing local weapon instance identity used by snapshots.
 const std::uint64_t instance=w->instance;*w=p.appearance;w->instance=instance;
 w->charm=p.accessories.charm?std::max(0,p.appearance.charm):-1;
 *a=p.accessories;return true;
}
inline unsigned NextRevision(unsigned revision)
{ return revision==std::numeric_limits<unsigned>::max()?1:revision+1; }
inline std::string SerializeConfig(const Config &c)
{
 std::ostringstream s;s<<"OC2_SKIN_PROFILES 1 "<<c.revision<<'\n'<<std::setprecision(9);
 for(const auto &p:c.items)
 {
  const auto &w=p.appearance;const auto &a=p.accessories;
  s<<"profile "<<w.instance<<' '<<w.weapon<<' '<<w.skin;
  for(int sticker:w.stickers)s<<' '<<sticker;
  s<<' '<<w.charm<<' '<<int(a.stattrak)<<' '<<a.statU<<' '<<a.statV<<' '<<int(a.charm)<<' '
   <<a.charmU<<' '<<a.charmV<<' '<<a.sticker<<' '<<a.amplitude<<' '<<a.length<<' '
   <<a.stiffness<<' '<<a.damping<<'\n';
 }
 return s.str();
}
inline bool ParseConfig(const std::string &text,Config &out,std::string &error)
{
 if(text.size()>kMaximumConfigBytes||oc2_inventory::HasNul(text))
  {error="Skin customization exceeds limits or contains NUL bytes";return false;}
 std::istringstream input(text);std::string line,magic;int version=0;std::uint64_t revision=0;
 if(!std::getline(input,line)){error="Empty skin customization";return false;}
 std::istringstream header(line);
 if(!(header>>magic>>version)||magic!="OC2_SKIN_PROFILES"||version!=1||
    !oc2_inventory::ReadNumber(header,std::numeric_limits<unsigned>::max(),revision)||!revision||!oc2_inventory::End(header))
  {error="Unsupported skin customization format";return false;}
 Config c;c.revision=static_cast<unsigned>(revision);
 while(std::getline(input,line))
 {
  if(line.empty()||line=="\r")continue;
  std::istringstream row(line);Profile p;int stat=0,charm=0;std::string type;
  auto &w=p.appearance;auto &a=p.accessories;
  if(!(row>>type)||type!="profile"||!oc2_inventory::ReadNumber(row,std::numeric_limits<std::uint64_t>::max(),w.instance)||
     !oc2_inventory::ReadInt(row,w.weapon)||!oc2_inventory::ReadInt(row,w.skin))
   {error="Invalid skin customization key";return false;}
  for(int &sticker:w.stickers)if(!oc2_inventory::ReadInt(row,sticker,true))
   {error="Invalid profile sticker";return false;}
  if(!oc2_inventory::ReadInt(row,w.charm,true)||!(row>>stat>>a.statU>>a.statV>>charm>>a.charmU>>a.charmV)||
     !oc2_inventory::ReadInt(row,a.sticker)||!(row>>a.amplitude>>a.length>>a.stiffness>>a.damping)||
     stat<0||stat>1||charm<0||charm>1||!oc2_inventory::End(row))
   {error="Invalid profile attachment values";return false;}
  a.weapon=w.weapon;a.stattrak=stat!=0;a.charm=charm!=0;
  if(Find(c,w.weapon,w.skin)||!Put(c,p)){error="Invalid or duplicate skin customization";return false;}
 }
 out=c;error.clear();return true;
}
}
