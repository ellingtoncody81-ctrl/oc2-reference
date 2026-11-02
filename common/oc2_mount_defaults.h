#pragma once
#include "oc2_accessories.h"

// User-curated per-weapon layout. Skin, art, counts and enable probabilities
// deliberately remain in their existing inventories and bot rules.
namespace oc2_mount_defaults
{
static const char *const kPath="oc2_inventory_mount_defaults.dat";
struct Config
{
 unsigned revision=1;
 std::array<bool,24> present{};
 std::array<oc2_accessories::WeaponConfig,24> weapons=oc2_accessories::DefaultConfig().weapons;
};
inline void CopyLayout(oc2_accessories::WeaponConfig& to,const oc2_accessories::WeaponConfig& from)
{to.statU=from.statU;to.statV=from.statV;to.charmU=from.charmU;to.charmV=from.charmV;to.length=from.length;}
inline bool Put(Config& config,const oc2_accessories::WeaponConfig& weapon)
{
 const int i=oc2_accessories::Index(weapon.weapon);if(i<0||!oc2_accessories::Valid(weapon))return false;
 CopyLayout(config.weapons[i],weapon);config.present[i]=true;return true;
}
inline oc2_accessories::Config Apply(const Config& config)
{
 auto result=oc2_accessories::DefaultConfig();
 for(int i=0;i<24;++i)if(config.present[i])CopyLayout(result.weapons[i],config.weapons[i]);
 return result;
}
inline bool Parse(const std::string& text,Config& out,std::string& error)
{
 if(text.size()>8192||text.find('\0')!=std::string::npos){error="Invalid mount defaults size";return false;}
 std::istringstream input(text);std::string line,magic,revision;int version=0;Config c;
 if(!std::getline(input,line)){error="Empty mount defaults";return false;}
 std::istringstream header(line);
 if(!(header>>magic>>version>>revision)||magic!="OC2_MOUNT_DEFAULTS"||version!=1||
    !oc2_accessories::UnsignedToken(revision,std::numeric_limits<unsigned>::max(),c.revision)||!c.revision||!oc2_accessories::End(header))
 {error="Unsupported mount defaults";return false;}
 while(std::getline(input,line))
 {
  if(line.empty()||line=="\r")continue;
  std::istringstream row(line);oc2_accessories::WeaponConfig w;
  if(!(row>>w.weapon>>w.statU>>w.statV>>w.charmU>>w.charmV>>w.length)||!oc2_accessories::End(row))
  {error="Invalid mount layout";return false;}
  const int i=oc2_accessories::Index(w.weapon);
  if(i<0||c.present[i]||!Put(c,w)){error="Invalid or duplicate mount weapon";return false;}
 }
 out=c;error.clear();return true;
}
inline std::string Serialize(const Config& c)
{
 std::ostringstream out;out<<"OC2_MOUNT_DEFAULTS 1 "<<c.revision<<'\n'<<std::setprecision(9);
 for(int i=0;i<24;++i)if(c.present[i]){const auto&w=c.weapons[i];out<<w.weapon<<' '<<w.statU<<' '<<w.statV<<' '<<w.charmU<<' '<<w.charmV<<' '<<w.length<<'\n';}
 return out.str();
}
}
