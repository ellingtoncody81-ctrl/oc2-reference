#pragma once
#include "oc2_accessories.h"
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// Local definitions, item instances and equipped choices are separate, as in
// CEconItemView/CCSPlayerInventory. Resource IDs are local, not Steam paint kits.
// The menu can use this contract before the client DLL has been loaded.
namespace oc2_inventory
{
static const char *const kCatalogPath="gfx/inventory/skins/catalog.dat";
static const char *const kConfigPath="oc2_inventory_loadout.dat";
static const char *const kTransparentPath="gfx/inventory/skins/textures/transparent_sticker.tga";
static const std::size_t kMaximumCatalogBytes=4*1024*1024;
static const std::size_t kMaximumConfigBytes=32768;
enum class Kind { GunSkin=1, Knife=2, KnifeSkin=3, Sticker=4 };
struct Entry
{
 int id=0;Kind kind=Kind::GunSkin;int weapon=0,definition=0;
 std::string file,model,material,preview,name,collection;
};
typedef std::vector<Entry> Catalog;
struct WeaponState
{
 std::uint64_t instance=0;int weapon=0,skin=0;
 std::array<int,4> stickers{};
 // 0 inherits the existing accessory setting; -1 removes the whole charm;
 // a positive sticker resource equips the charm without editing the old DAT.
 int charm=0;
};
struct KnifeState { std::uint64_t instance=0;int definition=0,skin=0; };
struct Config
{
 unsigned revision=1;std::array<WeaponState,24> weapons;
 std::vector<KnifeState> knives;int knife=0;
};
inline int Definition(int weapon)
{
 static const int definitions[24]={36,40,25,17,8,2,3,24,38,13,10,61,4,9,33,14,35,60,34,11,1,39,7,19};
 const int index=oc2_accessories::Index(weapon);return index<0?0:definitions[index];
}
inline Config DefaultConfig()
{
 Config c;
 for(int i=0;i<24;++i)
 {
  c.weapons[i].weapon=oc2_accessories::kWeaponIds[i];
  c.weapons[i].instance=(std::uint64_t(1)<<32)+c.weapons[i].weapon;
 }
 return c;
}
inline WeaponState *Find(Config &c,int weapon)
{ const int i=oc2_accessories::Index(weapon);return i<0?nullptr:&c.weapons[i]; }
inline const WeaponState *Find(const Config &c,int weapon)
{ const int i=oc2_accessories::Index(weapon);return i<0?nullptr:&c.weapons[i]; }
inline KnifeState *FindKnife(Config &c,int definition)
{ for(auto &v:c.knives)if(v.definition==definition)return &v;return nullptr; }
inline const KnifeState *FindKnife(const Config &c,int definition)
{ for(const auto &v:c.knives)if(v.definition==definition)return &v;return nullptr; }
inline KnifeState *EnsureKnife(Config &c,int definition)
{
 if(definition<=0||definition>65535)return nullptr;
 if(auto *v=FindKnife(c,definition))return v;
 if(c.knives.size()>=64)return nullptr;
 KnifeState v;v.definition=definition;v.instance=(std::uint64_t(2)<<32)+definition;
 c.knives.push_back(v);return &c.knives.back();
}
inline bool EquipKnife(Config &c,int definition)
{
 if(definition!=0&&!EnsureKnife(c,definition))return false;
 c.knife=definition;return true;
}
inline const Entry *FindEntry(const Catalog &catalog,int id)
{ if(id<=0)return nullptr;for(const auto &v:catalog)if(v.id==id)return &v;return nullptr; }
inline const Entry *FindKnifeEntry(const Catalog &catalog,int definition)
{ if(definition<=0)return nullptr;for(const auto &v:catalog)if(v.kind==Kind::Knife&&v.definition==definition)return &v;return nullptr; }
inline std::string Lower(std::string text)
{ for(char &c:text)if(c>='A'&&c<='Z')c+=32;return text; }
inline bool IsTransparent(const Entry &v)
{
 const std::size_t slash=v.file.find_last_of("/\\");
 const std::string name=Lower(v.file.substr(slash==std::string::npos?0:slash+1));
 return v.kind==Kind::Sticker&&(name=="transparent_sticker.bmp"||name=="transparent_sticker.tga"||name=="transparent_sticker.png");
}
inline bool ApplySticker(WeaponState &w,int slot,const Entry &entry)
{
 if(slot<0||slot>=4||entry.kind!=Kind::Sticker||entry.id<=0)return false;
 w.stickers[slot]=IsTransparent(entry)?-1:entry.id;return true;
}
inline bool ApplyCharm(WeaponState &w,const Entry &entry)
{
 if(entry.kind!=Kind::Sticker||entry.id<=0)return false;
 w.charm=IsTransparent(entry)?-1:entry.id;return true;
}
inline bool SafePath(const std::string &path,bool empty=false)
{
 if(path.empty())return empty;
 if(path.size()>240||path.front()=='/'||path.find("..")!=std::string::npos)return false;
 for(unsigned char c:path)
  if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c=='/'||c==' '))return false;
 return true;
}
inline bool TextField(const std::string &text,std::size_t maximum)
{ if(text.size()>maximum)return false;for(unsigned char c:text)if(c<32||c==127)return false;return true; }
inline bool End(std::istringstream &stream) { std::string extra;return !(stream>>extra); }
inline bool Number(const std::string &text,std::uint64_t maximum,std::uint64_t &out)
{
 if(text.empty())return false;std::uint64_t value=0;
 for(unsigned char c:text)
 {
  if(c<'0'||c>'9')return false;const unsigned digit=c-'0';
  if(value>maximum/10||(value==maximum/10&&digit>maximum%10))return false;
  value=value*10+digit;
 }
 out=value;return true;
}
inline bool ReadNumber(std::istringstream &s,std::uint64_t maximum,std::uint64_t &out)
{ std::string text;return bool(s>>text)&&Number(text,maximum,out); }
inline bool ReadInt(std::istringstream &s,int &out,bool removal=false)
{
 std::string text;std::uint64_t value=0;
 if(!(s>>text))return false;
 if(removal&&text=="-1"){out=-1;return true;}
 if(!Number(text,std::numeric_limits<int>::max(),value))return false;
 out=static_cast<int>(value);return true;
}
inline bool HasNul(const std::string &text) { return text.find('\0')!=std::string::npos; }
inline bool ParseCatalog(const char *text,Catalog &out,std::string &error)
{
 if(!text){error="Missing skin catalog";return false;}
 const std::string contents(text);
 if(contents.size()>kMaximumCatalogBytes){error="Skin catalog exceeds limits";return false;}
 std::istringstream input(contents);std::string line;
 if(!std::getline(input,line)){error="Empty skin catalog";return false;}
 if(!line.empty()&&line.back()=='\r')line.pop_back();
 if(line!="OC2_SKIN_CATALOG 1"){error="Unsupported skin catalog";return false;}
 Catalog result;std::set<int> ids,knives;
 while(std::getline(input,line))
 {
  if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
  std::vector<std::string> fields;std::size_t start=0;
  for(;;){const std::size_t end=line.find('\t',start);fields.push_back(line.substr(start,end==std::string::npos?end:end-start));if(end==std::string::npos)break;start=end+1;}
  if(fields.size()!=10||result.size()>=8192){error="Invalid skin catalog row";return false;}
  Entry e;std::uint64_t id=0,kind=0,weapon=0,definition=0;
  if(!Number(fields[0],std::numeric_limits<int>::max(),id)||!id||!Number(fields[1],4,kind)||!kind||
     !Number(fields[2],65535,weapon)||!Number(fields[3],65535,definition))
   {error="Invalid skin catalog identifiers";return false;}
  e.id=int(id);e.kind=static_cast<Kind>(kind);e.weapon=int(weapon);e.definition=int(definition);
  e.file=fields[4];e.model=fields[5];e.material=fields[6];e.preview=fields[7];e.name=fields[8];e.collection=fields[9];
  if(!ids.insert(e.id).second||!SafePath(e.file,e.kind==Kind::Knife)||!SafePath(e.model,true)||!SafePath(e.preview,true)||
     !TextField(e.material,512)||!TextField(e.name,240)||e.name.empty()||!TextField(e.collection,240))
   {error="Invalid or duplicate skin catalog entry";return false;}
  if((e.kind==Kind::GunSkin&&(Definition(e.weapon)!=e.definition||!e.definition||e.material.empty()))||
     (e.kind==Kind::Knife&&(e.weapon!=29||!e.definition||e.model.size()<11||e.model.compare(0,7,"models/")||
      Lower(e.model.substr(e.model.size()-4))!=".mdl"||!knives.insert(e.definition).second))||
     (e.kind==Kind::KnifeSkin&&(e.weapon!=29||!e.definition||e.material.empty()))||
     (e.kind==Kind::Sticker&&(e.weapon||e.definition)))
   {error="Skin catalog definition mismatch";return false;}
  result.push_back(e);
 }
 if(result.empty()){error="Empty skin catalog";return false;}
 for(const auto &e:result)if(e.kind==Kind::KnifeSkin&&!FindKnifeEntry(result,e.definition))
  {error="Skin catalog references an unknown knife";return false;}
 out=result;error.clear();return true;
}
inline bool ParseCatalog(const std::string &text,Catalog &out,std::string &error)
{
 if(HasNul(text)){error="Skin catalog contains NUL bytes";return false;}
 return ParseCatalog(text.c_str(),out,error);
}
inline std::string SerializeCatalog(const Catalog &catalog)
{
 std::ostringstream s;s<<"OC2_SKIN_CATALOG 1\n";
 for(const auto &e:catalog)s<<e.id<<'\t'<<int(e.kind)<<'\t'<<e.weapon<<'\t'<<e.definition<<'\t'<<e.file<<'\t'
  <<e.model<<'\t'<<e.material<<'\t'<<e.preview<<'\t'<<e.name<<'\t'<<e.collection<<'\n';
 return s.str();
}
inline bool ParseConfig(const char *text,Config &out,std::string &error)
{
 if(!text){error="Missing loadout file";return false;}
 const std::string contents(text);if(contents.size()>kMaximumConfigBytes){error="Loadout exceeds limits";return false;}
 std::istringstream input(contents);std::string line,magic;int version=0;Config c=DefaultConfig();
 if(!std::getline(input,line)){error="Empty loadout file";return false;}
 std::istringstream header(line);std::uint64_t revision=0;
 if(!(header>>magic>>version)||magic!="OC2_LOADOUT"||version!=1||!ReadNumber(header,std::numeric_limits<unsigned>::max(),revision)||!revision||!End(header))
  {error="Unsupported loadout format";return false;}
 c.revision=static_cast<unsigned>(revision);std::array<bool,24> seen{};std::set<std::uint64_t> instances;std::set<int> knives;bool equipped=false;int total=0;
 while(std::getline(input,line))
 {
  if(line.empty()||line=="\r")continue;std::istringstream row(line);std::string type;if(!(row>>type)){error="Invalid loadout row";return false;}
  if(type=="gun")
  {
   WeaponState w;
   if(!ReadNumber(row,std::numeric_limits<std::uint64_t>::max(),w.instance)||!w.instance||!ReadInt(row,w.weapon)||!ReadInt(row,w.skin))
    {error="Invalid weapon instance";return false;}
   for(int &sticker:w.stickers)if(!ReadInt(row,sticker,true)){error="Invalid sticker choice";return false;}
   const int i=oc2_accessories::Index(w.weapon);
   if(!ReadInt(row,w.charm,true)||!End(row)||i<0||seen[i]||!instances.insert(w.instance).second)
    {error="Invalid or duplicate weapon loadout";return false;}
   seen[i]=true;c.weapons[i]=w;++total;
  }
  else if(type=="knife")
  {
   KnifeState v;
   if(!ReadNumber(row,std::numeric_limits<std::uint64_t>::max(),v.instance)||!v.instance||!ReadInt(row,v.definition)||v.definition<=0||v.definition>65535||
      !ReadInt(row,v.skin)||!End(row)||!instances.insert(v.instance).second||!knives.insert(v.definition).second||c.knives.size()>=64)
    {error="Invalid or duplicate knife instance";return false;}
   c.knives.push_back(v);
  }
  else if(type=="equip_knife")
  {
   if(equipped||!ReadInt(row,c.knife)||c.knife>65535||!End(row)){error="Invalid equipped knife";return false;}
   equipped=true;
  }
  else {error="Unknown loadout row";return false;}
 }
 if(total!=24||!equipped||(c.knife&&!FindKnife(c,c.knife))){error="Incomplete loadout";return false;}
 out=c;error.clear();return true;
}
inline bool ParseConfig(const std::string &text,Config &out,std::string &error)
{
 if(HasNul(text)){error="Loadout contains NUL bytes";return false;}
 return ParseConfig(text.c_str(),out,error);
}
inline std::string SerializeConfig(const Config &c)
{
 std::ostringstream s;s<<"OC2_LOADOUT 1 "<<c.revision<<'\n';
 for(const auto &w:c.weapons)
 {s<<"gun "<<w.instance<<' '<<w.weapon<<' '<<w.skin;for(int sticker:w.stickers)s<<' '<<sticker;s<<' '<<w.charm<<'\n';}
 for(const auto &v:c.knives)s<<"knife "<<v.instance<<' '<<v.definition<<' '<<v.skin<<'\n';
 s<<"equip_knife "<<c.knife<<'\n';return s.str();
}
inline bool ValidateConfig(const Config &c,const Catalog &catalog,std::string &error)
{
 Config parsed;if(!ParseConfig(SerializeConfig(c).c_str(),parsed,error))return false;
 for(const auto &w:c.weapons)
 {
  const Entry *skin=FindEntry(catalog,w.skin);
  if(w.skin&&(!skin||skin->kind!=Kind::GunSkin||skin->weapon!=w.weapon)){error="Skin does not match the weapon";return false;}
  for(int sticker:w.stickers)
  {const Entry *e=FindEntry(catalog,sticker);if(sticker>0&&(!e||e->kind!=Kind::Sticker)){error="Unknown sticker choice";return false;}}
  const Entry *charm=FindEntry(catalog,w.charm);
  if(w.charm>0&&(!charm||charm->kind!=Kind::Sticker)){error="Unknown charm pattern";return false;}
 }
 for(const auto &v:c.knives)
 {
  const Entry *e=FindEntry(catalog,v.skin);
  if(!FindKnifeEntry(catalog,v.definition)||(v.skin&&(!e||e->kind!=Kind::KnifeSkin||e->definition!=v.definition)))
   {error="Skin does not match the knife";return false;}
 }
 error.clear();return true;
}
}
