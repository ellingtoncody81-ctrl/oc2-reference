#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

// Local item customization, shared by mainui (also without a loaded client)
// and the client. Settings and counters are separate so editing a stale menu
// snapshot cannot overwrite kills acquired in the meantime.
namespace oc2_accessories
{
static const char *const kConfigPath="oc2_inventory_accessories.dat";
static const char *const kCountsPath="oc2_inventory_counts.dat";
static const char *const kCatalogPath="gfx/inventory/stickers/catalog.txt";
static const char *const kStickerDirectory="gfx/inventory/stickers/";
static const unsigned kMaximumCount=999999;
static const int kWeaponIds[24]={1,3,5,7,8,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,26,27,28,30};
static const char *const kWeaponNames[24]={"P250","SSG 08","XM1014","MAC-10","AUG","Dual Berettas",
 "Five-SeveN","UMP-45","SCAR-20","Galil AR","FAMAS","USP-S","Glock-18","AWP","MP7","M249",
 "Nova","M4A1-S","MP9","G3SG1","Desert Eagle","SG 553","AK-47","P90"};
inline int Index(int id) { for(int i=0;i<24;++i)if(kWeaponIds[i]==id)return i;return -1; }
inline const char *Name(int id) { int i=Index(id);return i<0?"Unknown":kWeaponNames[i]; }
inline bool Range(float v,float lo,float hi) { return std::isfinite(v)&&v>=lo&&v<=hi; }
struct WeaponConfig
{
 int weapon=0;
 bool stattrak=false;
 float statU=.4f,statV=.55f;
 bool charm=false;
 float charmU=.72f,charmV=.35f;
 int sticker=0;
 float amplitude=15,length=1,stiffness=80,damping=5;
};
struct Config { unsigned revision=1;std::array<WeaponConfig,24> weapons; };
struct Counts { std::array<unsigned,24> values{}; };
struct Sticker
{
 int id;std::string file,name;
 Sticker(int value=0,const std::string &path=std::string(),const std::string &label=std::string())
  :id(value),file(path),name(label){}
};
typedef std::vector<Sticker> Catalog;
inline Config DefaultConfig()
{
 // Inventory 3D gun-side layout, calibrated on the current actual V meshes
 // and complete neutral addons. Pendants favor the muzzle-side fixed body,
 // clear of grips; counters stay readable. New profiles, bots and USE DEFAULTS
 // share these presets; parsing never replaces a player's saved placement.
 static const float placement[24][8]={
  {0.45876798f,0.803028166f,0.119999968f,0.699999988f,10,0.85f,100,10}, // weapon 1
  {0.63294512f,0.561490715f,0.299999982f,0.550000012f,12,1,90,9}, // weapon 3
  {0.589999974f,0.810000002f,0.200083867f,0.87744391f,10,0.9f,110,11}, // weapon 5
  {0.603527069f,0.795102537f,0.239817142f,0.846185803f,12,1,90,9}, // weapon 7
  {0.730000913f,0.660654783f,0.357113779f,0.669115663f,12,1,90,9}, // weapon 8
  {0.730000019f,0.819999993f,0.600000024f,0.699999988f,10,0.85f,100,10}, // weapon 10
  {0.469970673f,0.825829446f,0.13999787f,0.72116822f,10,0.85f,100,10}, // weapon 11
  {0.50999999f,0.819999933f,0.219999999f,0.819999933f,12,1,90,9}, // weapon 12
  {0.61000222f,0.607617855f,0.329021871f,0.576341152f,12,1,90,9}, // weapon 13
  {0.58937937f,0.843597472f,0.272364378f,0.830813766f,12,1,90,9}, // weapon 14
  {0.683741093f,0.64798075f,0.332431287f,0.629398286f,12,1,90,9}, // weapon 15
  {0.738997877f,0.695221663f,0.569999993f,0.730000019f,10,0.85f,100,10}, // weapon 16
  {0.460000008f,0.849999964f,0.13000001f,0.699999988f,10,0.85f,100,10}, // weapon 17
  {0.689999998f,0.550000012f,0.366936594f,0.404346257f,12,1,90,9}, // weapon 18
  {0.569999993f,0.779999912f,0.204622209f,0.85051614f,12,1,90,9}, // weapon 19
  {0.649999976f,0.699999988f,0.319999993f,0.550000012f,10,0.95f,100,10}, // weapon 20
  {0.549303293f,0.732667387f,0.180000007f,0.460000008f,10,0.9f,110,11}, // weapon 21
  {0.620018303f,0.717007935f,0.219999984f,0.689999998f,12,1,90,9}, // weapon 22
  {0.460000008f,0.850000024f,0.207951829f,0.912933052f,12,1,90,9}, // weapon 23
  {0.756972194f,0.610462964f,0.299994379f,0.548136652f,12,1,90,9}, // weapon 24
  {0.45996654f,0.778560817f,0.159912527f,0.685883641f,10,0.85f,100,10}, // weapon 26
  {0.614321828f,0.599907398f,0.25f,0.529999971f,12,1,90,9}, // weapon 27
  {0.579961658f,0.768744409f,0.199972615f,0.751376927f,12,1,90,9}, // weapon 28
  {0.720000029f,0.490000039f,0.209414095f,0.323967278f,12,1,90,9}, // weapon 30
 };
 Config c;
 for(int i=0;i<24;++i)
 {
  WeaponConfig &w=c.weapons[i];w.weapon=kWeaponIds[i];
  w.statU=placement[i][0];w.statV=placement[i][1];
  w.charmU=placement[i][2];w.charmV=placement[i][3];
  w.amplitude=placement[i][4];w.length=placement[i][5];
  w.stiffness=placement[i][6];w.damping=placement[i][7];
 }
 return c;
}
inline Counts DefaultCounts() { return Counts(); }
inline WeaponConfig *Find(Config &c,int id) { int i=Index(id);return i<0?nullptr:&c.weapons[i]; }
inline const WeaponConfig *Find(const Config &c,int id) { int i=Index(id);return i<0?nullptr:&c.weapons[i]; }
inline bool Valid(const WeaponConfig &w)
{
 return Index(w.weapon)>=0&&Range(w.statU,0,1)&&Range(w.statV,0,1)&&Range(w.charmU,0,1)&&Range(w.charmV,0,1)
  &&w.sticker>=0&&Range(w.amplitude,0,25)&&Range(w.length,.5f,1.5f)&&Range(w.stiffness,20,160)&&Range(w.damping,2,30);
}
inline bool End(std::istringstream &s) { std::string extra;return !(s>>extra); }
inline bool UnsignedToken(const std::string &text,unsigned maximum,unsigned &out)
{
 if(text.empty())return false;
 unsigned value=0;
 for(unsigned char ch:text)
 {
  if(ch<'0'||ch>'9')return false;const unsigned digit=ch-'0';
  if(value>maximum/10||(value==maximum/10&&digit>maximum%10))return false;
  value=value*10+digit;
 }
 out=value;return true;
}
inline bool ParseConfig(const char *text,Config &out,std::string &error)
{
 if(!text){error="Missing customization file";return false;}
 std::istringstream input(text);std::string line,magic;int version;Config c=DefaultConfig();
 if(!std::getline(input,line)){error="Empty customization file";return false;}
 std::istringstream header(line);std::string revision;
 if(!(header>>magic>>version>>revision)||magic!="OC2_ACCESSORIES"||version!=1||
  !UnsignedToken(revision,std::numeric_limits<unsigned>::max(),c.revision)||!c.revision||!End(header))
  {error="Unsupported customization format";return false;}
 std::array<bool,24> seen{};int total=0;
 while(std::getline(input,line))
 {
  if(line.empty()||line=="\r")continue;
  std::istringstream s(line);WeaponConfig w;int enabled,attached;
  if(!(s>>w.weapon>>enabled>>w.statU>>w.statV>>attached>>w.charmU>>w.charmV>>w.sticker
      >>w.amplitude>>w.length>>w.stiffness>>w.damping)||!End(s)||enabled<0||enabled>1||attached<0||attached>1||!Valid(w))
   {error="Invalid customization values";return false;}
  int i=Index(w.weapon);if(seen[i]){error="Duplicate weapon customization";return false;}
  w.stattrak=enabled!=0;w.charm=attached!=0;c.weapons[i]=w;seen[i]=true;++total;
 }
 if(total!=24){error="Incomplete weapon customization";return false;}
 out=c;error.clear();return true;
}
inline std::string SerializeConfig(const Config &c)
{
 std::ostringstream s;s<<"OC2_ACCESSORIES 1 "<<(c.revision?c.revision:1)<<'\n'<<std::setprecision(9);
 for(const auto &w:c.weapons)
  s<<w.weapon<<' '<<int(w.stattrak)<<' '<<w.statU<<' '<<w.statV<<' '<<int(w.charm)<<' '<<w.charmU<<' '<<w.charmV
   <<' '<<w.sticker<<' '<<w.amplitude<<' '<<w.length<<' '<<w.stiffness<<' '<<w.damping<<'\n';
 return s.str();
}
inline bool ParseCounts(const char *text,Counts &out,std::string &error)
{
 if(!text){error="Missing counters file";return false;}
 std::istringstream input(text);std::string magic,line;int version;
 if(!std::getline(input,line)){error="Empty counters file";return false;}
 std::istringstream header(line);
 if(!(header>>magic>>version)||magic!="OC2_COUNTS"||version!=1||!End(header)){error="Unsupported counters format";return false;}
 Counts c;std::array<bool,24> seen{};int total=0;
 while(std::getline(input,line))
 {
  if(line.empty()||line=="\r")continue;
  std::istringstream row(line);std::string idText,valueText;unsigned id=0,value=0;
  if(!(row>>idText>>valueText)||!UnsignedToken(idText,30,id)||!UnsignedToken(valueText,kMaximumCount,value)||!End(row))
   {error="Invalid counter";return false;}
  int i=Index(static_cast<int>(id));if(i<0||seen[i]){error="Invalid counter";return false;}
  c.values[i]=value;seen[i]=true;++total;
 }
 if(total!=24){error="Incomplete counters file";return false;}
 out=c;error.clear();return true;
}
inline std::string SerializeCounts(const Counts &c)
{
 std::ostringstream s;s<<"OC2_COUNTS 1\n";
 for(int i=0;i<24;++i)s<<kWeaponIds[i]<<' '<<std::min(c.values[i],kMaximumCount)<<'\n';
 return s.str();
}
inline bool SafeStickerFile(const std::string &file)
{
 if(file.empty()||file.size()>80||file.find("..")!=std::string::npos)return false;
 for(unsigned char c:file)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;
 const std::size_t dot=file.rfind('.');if(dot==std::string::npos)return false;
 std::string ext=file.substr(dot);for(char &c:ext)if(c>='A'&&c<='Z')c+=32;
 return ext==".png"||ext==".tga"||ext==".bmp";
}
inline int StickerId(const std::string &file)
{
 std::uint32_t h=2166136261u;
 for(unsigned char c:file){if(c>='A'&&c<='Z')c+=32;h=(h^c)*16777619u;}
 const int value=static_cast<int>(h&0x7fffffffu);return value?value:1;
}
inline Catalog DefaultCatalog() { return Catalog{Sticker{0,"default.tga","Default sticker"}}; }
inline bool ParseCatalog(const char *text,Catalog &out,std::string &error)
{
 if(!text){error="Missing sticker catalog";return false;}
 std::istringstream input(text);std::string line,magic;int version;
 if(!std::getline(input,line)){error="Empty sticker catalog";return false;}
 std::istringstream h(line);if(!(h>>magic>>version)||magic!="OC2_STICKERS"||version!=1||!End(h))
  {error="Unsupported sticker catalog";return false;}
 Catalog c;
 while(std::getline(input,line))
 {
  if(line.empty()||line=="\r")continue;
  std::istringstream s(line);Sticker item;
  if(!(s>>item.id>>item.file)||item.id<0||!SafeStickerFile(item.file)){error="Invalid sticker entry";return false;}
  std::getline(s,item.name);while(!item.name.empty()&&(item.name.front()==' '||item.name.front()=='\t'))item.name.erase(0,1);
  if(!item.name.empty()&&item.name.back()=='\r')item.name.pop_back();
  if(item.name.empty())item.name=item.file;
  if(item.name.size()>120||c.size()>=512){error="Sticker catalog exceeds limits";return false;}
  for(const auto &old:c)if(old.id==item.id||old.file==item.file){error="Duplicate sticker entry";return false;}
  c.push_back(item);
 }
 if(c.empty()){error="Empty sticker catalog";return false;}
 out=c;error.clear();return true;
}
inline std::string SerializeCatalog(const Catalog &c)
{
 std::ostringstream s;s<<"OC2_STICKERS 1\n";
 for(const auto &v:c)s<<v.id<<' '<<v.file<<' '<<v.name<<'\n';return s.str();
}
}
