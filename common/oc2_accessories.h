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
 // Full gun-side coordinates, independently chosen for each model. These
 // presets initialize new profiles and the explicit USE DEFAULTS action;
 // parsing an existing profile never replaces the player's saved placement.
 static const float placement[24][8]={
  {0.493370384f,0.913342357f,0.100000031f,0.720000088f,10,0.85f,100,10}, // weapon 1
  {0.640817046f,0.551753283f,0.850000024f,0.639999986f,12,1,90,9}, // weapon 3
  {0.61005187f,0.868574023f,0.689326108f,0.823671579f,10,0.9f,110,11}, // weapon 5
  {0.620000184f,0.874438822f,0.339914113f,0.885496318f,12,1,90,9}, // weapon 7
  {0.750885844f,0.664079309f,0.899999976f,0.670000017f,12,1,90,9}, // weapon 8
  {0.53342402f,0.840063632f,0.220000029f,0.560000002f,10,0.85f,100,10}, // weapon 10
  {0.469900399f,0.82583046f,0.140017852f,0.770205379f,10,0.85f,100,10}, // weapon 11
  {0.520009696f,0.83513248f,0.694125772f,0.815667629f,12,1,90,9}, // weapon 12
  {0.620000005f,0.600000024f,0.7791152f,0.623179555f,12,1,90,9}, // weapon 13
  {0.599992752f,0.845825672f,0.767767429f,0.775463879f,12,1,90,9}, // weapon 14
  {0.620000005f,0.649999976f,0.85004425f,0.680638433f,12,1,90,9}, // weapon 15
  {0.729984045f,0.796723187f,0.569999993f,0.779999971f,10,0.85f,100,10}, // weapon 16
  {0.439999998f,0.939999938f,0.11999999f,0.75f,10,0.85f,100,10}, // weapon 17
  {0.700000048f,0.560000002f,0.870000064f,0.50999999f,12,1,90,9}, // weapon 18
  {0.569999993f,0.779999912f,0.800000012f,0.819999993f,12,1,90,9}, // weapon 19
  {0.650001824f,0.682794869f,0.757509172f,0.692948878f,10,0.95f,100,10}, // weapon 20
  {0.549303293f,0.732667387f,0.5f,0.700000107f,10,0.9f,110,11}, // weapon 21
  {0.618996263f,0.730559647f,0.730000019f,0.730000019f,12,1,90,9}, // weapon 22
  {0.460000008f,0.879999995f,0.270150244f,0.911379457f,12,1,90,9}, // weapon 23
  {0.75999999f,0.610000014f,0.899999976f,0.579999983f,12,1,90,9}, // weapon 24
  {0.429879755f,0.778863251f,0.150000006f,0.770000041f,10,0.85f,100,10}, // weapon 26
  {0.615073919f,0.601145744f,0.720000029f,0.670000017f,12,1,90,9}, // weapon 27
  {0.579999983f,0.800000072f,0.707003176f,0.815474987f,12,1,90,9}, // weapon 28
  {0.724483848f,0.524870574f,0.899999917f,0.520000041f,12,1,90,9}, // weapon 30
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
