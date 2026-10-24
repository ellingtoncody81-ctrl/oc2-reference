#pragma once
#include <cstdint>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// Hands are a player wearable (CS:GO CLOTHING_HANDS), separate from the
// appearance stored on a weapon instance. Both teams share this local slot.
namespace oc2_gloves
{
static const char *const kCatalogPath="gfx/inventory/gloves/catalog.dat";
static const char *const kConfigPath="oc2_inventory_gloves.dat";
static const char *const kMessage="OC2Glove";
static const unsigned char kVersion=1;
enum Operation { Reset=0, Upsert=1 };
static const int kResetBytes=2,kUpsertBytes=7;
static const std::size_t kMaximumCatalogBytes=2*1024*1024;
static const std::size_t kMaximumConfigBytes=1024;
struct Entry
{
 int id=0,definition=0,body=0;
 std::string model,leftMaterial,rightMaterial,leftTexture,rightTexture,preview,name,collection;
};
typedef std::vector<Entry> Catalog;
struct Config { unsigned revision=1;int resource=0;std::uint64_t instance=0; };
inline Config DefaultConfig() { return Config(); }
inline const Entry *FindEntry(const Catalog &catalog,int resource)
{ if(resource<=0)return nullptr;for(const auto &entry:catalog)if(entry.id==resource)return &entry;return nullptr; }
inline std::uint64_t Instance(int resource)
{ return resource>0?(std::uint64_t(3)<<32)+static_cast<unsigned>(resource):0; }
inline bool Equip(Config &config,int resource)
{ if(resource<0)return false;config.resource=resource;config.instance=Instance(resource);return true; }
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
inline bool ReadNumber(std::istringstream &stream,std::uint64_t maximum,std::uint64_t &out)
{ std::string text;return bool(stream>>text)&&Number(text,maximum,out); }
inline bool End(std::istringstream &stream) { std::string extra;return !(stream>>extra); }
inline bool TextField(const std::string &text,std::size_t maximum)
{ if(text.size()>maximum)return false;for(unsigned char c:text)if(c<32||c==127)return false;return true; }
inline bool SafePath(const std::string &path)
{
 if(path.empty()||path.size()>240||path.front()=='/'||path.find("..")!=std::string::npos)return false;
 for(unsigned char c:path)
  if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c=='/'||c==' '))return false;
 return true;
}
inline bool Extension(const std::string &path,const char *extension)
{ const std::string ext(extension);return path.size()>=ext.size()&&path.compare(path.size()-ext.size(),ext.size(),ext)==0; }
inline bool MaterialField(const std::string &text)
{
 if(!TextField(text,512))return false;
 if(text.empty())return true;
 std::size_t start=0;
 for(;;)
 {
  const std::size_t end=text.find(',',start);const std::string name=text.substr(start,end==std::string::npos?end:end-start);
  if(name.empty()||name.size()>63||name.find("..")!=std::string::npos)return false;
  for(unsigned char c:name)if(c==':'||c=='|'||c=='\t'||c=='\r'||c=='\n')return false;
  if(end==std::string::npos)return true;start=end+1;
 }
}
inline bool ParseCatalog(const std::string &contents,Catalog &out,std::string &error)
{
 if(contents.size()>kMaximumCatalogBytes||contents.find('\0')!=std::string::npos)
  {error="Glove catalog exceeds limits";return false;}
 std::istringstream input(contents);std::string line;
 if(!std::getline(input,line)){error="Empty glove catalog";return false;}
 if(!line.empty()&&line.back()=='\r')line.pop_back();
 if(line!="OC2_GLOVE_CATALOG 1"){error="Unsupported glove catalog";return false;}
 Catalog candidate;std::set<int> ids;
 while(std::getline(input,line))
 {
  if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
  std::vector<std::string> fields;std::size_t start=0;
  for(;;){const std::size_t end=line.find('\t',start);fields.push_back(line.substr(start,end==std::string::npos?end:end-start));if(end==std::string::npos)break;start=end+1;}
  if(fields.size()!=11||candidate.size()>=4096){error="Invalid glove catalog row";return false;}
  std::uint64_t id=0,definition=0,body=0;
  if(!Number(fields[0],std::numeric_limits<int>::max(),id)||!id||!Number(fields[1],65535,definition)||
     !Number(fields[10],65535,body)||!ids.insert(int(id)).second)
   {error="Invalid glove identifiers";return false;}
  Entry entry;entry.id=int(id);entry.definition=int(definition);entry.body=int(body);
  entry.model=fields[2];entry.leftMaterial=fields[3];entry.rightMaterial=fields[4];
  entry.leftTexture=fields[5];entry.rightTexture=fields[6];entry.preview=fields[7];entry.name=fields[8];entry.collection=fields[9];
  if(!SafePath(entry.model)||entry.model.compare(0,12,"models/arms/")||!Extension(entry.model,".mdl")||
     !MaterialField(entry.leftMaterial)||!MaterialField(entry.rightMaterial)||
     !SafePath(entry.preview)||entry.preview.compare(0,21,"gfx/inventory/gloves/")||
     entry.name.empty()||!TextField(entry.name,160)||!TextField(entry.collection,160))
   {error="Invalid glove resource fields";return false;}
  const bool original=entry.leftTexture.empty()&&entry.rightTexture.empty();
  if(!original&&(entry.leftMaterial.empty()||entry.rightMaterial.empty()||
     !SafePath(entry.leftTexture)||!SafePath(entry.rightTexture)||
     entry.leftTexture.compare(0,21,"gfx/inventory/gloves/")||entry.rightTexture.compare(0,21,"gfx/inventory/gloves/")||
     !Extension(entry.leftTexture,".tga")||!Extension(entry.rightTexture,".tga")))
   {error="Invalid glove texture pair";return false;}
  candidate.push_back(entry);
 }
 out.swap(candidate);error.clear();return true;
}
inline bool ParseConfig(const std::string &contents,Config &out,std::string &error)
{
 if(contents.size()>kMaximumConfigBytes||contents.find('\0')!=std::string::npos)
  {error="Glove loadout exceeds limits";return false;}
 std::istringstream input(contents);std::string line;
 if(!std::getline(input,line)){error="Empty glove loadout";return false;}
 std::istringstream header(line);std::string magic;std::uint64_t version=0,revision=0;
 if(!(header>>magic)||magic!="OC2_GLOVE_LOADOUT"||!ReadNumber(header,1,version)||version!=1||
    !ReadNumber(header,std::numeric_limits<unsigned>::max(),revision)||!revision||!End(header))
  {error="Invalid glove loadout header";return false;}
 if(!std::getline(input,line)){error="Missing glove equipment";return false;}
 std::istringstream row(line);std::string command;std::uint64_t resource=0,instance=0;
 if(!(row>>command)||command!="equip"||!ReadNumber(row,std::numeric_limits<int>::max(),resource)||
    !ReadNumber(row,std::numeric_limits<std::uint64_t>::max(),instance)||!End(row)||
    instance!=Instance(int(resource)))
  {error="Invalid glove equipment";return false;}
 while(std::getline(input,line))if(line.find_first_not_of(" \t\r")==std::string::npos)continue;
  else {error="Unexpected glove loadout data";return false;}
 Config candidate;candidate.revision=static_cast<unsigned>(revision);candidate.resource=int(resource);candidate.instance=instance;
 out=candidate;error.clear();return true;
}
inline std::string SerializeConfig(const Config &config)
{
 std::ostringstream output;output<<"OC2_GLOVE_LOADOUT 1 "<<config.revision<<"\nequip "<<config.resource<<' '<<config.instance<<'\n';return output.str();
}
inline bool ValidateConfig(const Config &config,const Catalog &catalog,std::string &error)
{
 Config candidate;if(!ParseConfig(SerializeConfig(config),candidate,error))return false;
 if(config.resource&&!FindEntry(catalog,config.resource)){error="Unknown glove choice";return false;}
 error.clear();return true;
}
}
