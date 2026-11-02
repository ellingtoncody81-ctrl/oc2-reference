#pragma once
#include "SkinInventory.h"
#include "../../../../common/oc2_skin_profiles.h"
#include "../../../../common/oc2_mount_defaults.h"
#include <chrono>
#include <fstream>
#include <iterator>
#ifdef _WIN32
#include "miniutl/winlite.h"
#endif

namespace oc2_local_profiles
{
struct SavedFile { bool exists=false;std::string text; };
struct State
{
 oc2_profiles::Config profiles;
 oc2_inventory::Config loadout=oc2_inventory::DefaultConfig();
 oc2_accessories::Config accessories=oc2_accessories::DefaultConfig();
 oc2_mount_defaults::Config mountDefaults;
 oc2_accessories::Config defaultAccessories=oc2_accessories::DefaultConfig();
 SavedFile profileFile,loadoutFile,accessoryFile,mountFile;
};
inline bool ReadSaved(const char *path,SavedFile &file,std::string &error)
{
 file.exists=EngFuncs::FileExists(path,true)!=0;file.text.clear();
 if(file.exists&&!oc2_local_inventory::Read(path,file.text))
  {error="Saved inventory could not be read.";return false;}
 return true;
}
inline bool Load(State &out,std::string &error)
{
 State state;
 if(!ReadSaved(oc2_inventory::kConfigPath,state.loadoutFile,error)||
    !ReadSaved(oc2_accessories::kConfigPath,state.accessoryFile,error)||
    !ReadSaved(oc2_profiles::kConfigPath,state.profileFile,error)||
    !ReadSaved(oc2_mount_defaults::kPath,state.mountFile,error))return false;
 if(state.mountFile.exists&&!oc2_mount_defaults::Parse(state.mountFile.text,state.mountDefaults,error))return false;
 state.defaultAccessories=oc2_mount_defaults::Apply(state.mountDefaults);state.accessories=state.defaultAccessories;
 if(state.loadoutFile.exists&&!oc2_inventory::ParseConfig(state.loadoutFile.text,state.loadout,error))return false;
 if(state.accessoryFile.exists&&!oc2_accessories::ParseConfig(state.accessoryFile.text.c_str(),state.accessories,error))return false;
 if(state.profileFile.exists&&!oc2_profiles::ParseConfig(state.profileFile.text,state.profiles,error))return false;
 oc2_profiles::InheritEquipped(state.profiles,state.loadout,state.accessories);
 out=state;error.clear();return true;
}
inline oc2_profiles::Profile Resolve(const State &state,int weapon,int skin)
{ return oc2_profiles::Resolve(state.profiles,state.loadout,state.accessories,weapon,skin,&state.defaultAccessories); }
inline bool IsEquipped(const State &state,int weapon,int skin)
{ const auto *w=oc2_inventory::Find(state.loadout,weapon);return w&&w->skin==skin; }

// COM_SaveFile writes through the engine's selected game directory. Verify its
// temporary file on disk before replacing a DAT, so an unusual write directory
// fails safely instead of writing to an inferred location.
inline std::string DiskPath(const std::string &file)
{
 char directory[1024]={};EngFuncs::GetGameDir(directory);
 std::string path=directory;if(!path.empty()&&path.back()!='/'&&path.back()!='\\')path+='/';
 return path+file;
}
inline bool DiskMatches(const std::string &path,const std::string &text)
{
 std::ifstream file(path.c_str(),std::ios::binary|std::ios::ate);
 if(!file||file.tellg()!=std::streamoff(text.size()))return false;
 file.seekg(0);const std::string actual((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
 return actual==text;
}
inline bool Replace(const std::string &source,const std::string &target)
{
#ifdef _WIN32
 return MoveFileExA(source.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
 return std::rename(source.c_str(),target.c_str())==0;
#endif
}
inline std::string TemporaryName(const char *path)
{
 static unsigned serial=0;std::ostringstream suffix;
 suffix<<path<<".pending."<<std::chrono::steady_clock::now().time_since_epoch().count()<<'.'<<++serial;
 return suffix.str();
}
inline bool Prepare(const char *path,const std::string &text,std::string &temporary)
{
 temporary=TemporaryName(path);
 if(!EngFuncs::COM_SaveFile(temporary.c_str(),text.data(),int(text.size())))return false;
 std::string saved;
 return oc2_local_inventory::Read(temporary.c_str(),saved)&&saved==text&&DiskMatches(DiskPath(temporary),text);
}
inline bool SameSaved(const char *path,const SavedFile &before)
{
 if((EngFuncs::FileExists(path,true)!=0)!=before.exists)return false;
 if(!before.exists)return true;
 std::string current;return oc2_local_inventory::Read(path,current)&&current==before.text;
}
struct Change
{
 const char *path;SavedFile before;std::string after,temporary;bool committed=false;
 Change(const char *name,const SavedFile &old,const std::string &text):path(name),before(old),after(text){}
};
inline bool Restore(Change &change)
{
 // A failed transaction must not overwrite a subsequent external save.
 std::string current;
 if(!oc2_local_inventory::Read(change.path,current)||current!=change.after)return false;
 if(!change.before.exists)
  return Replace(DiskPath(change.path),DiskPath(TemporaryName(change.path)+".rolled-back"));
 std::string temporary;
 return Prepare(change.path,change.before.text,temporary)&&Replace(DiskPath(temporary),DiskPath(change.path));
}
inline bool SaveChanges(std::vector<Change> &changes,std::string &error)
{
 // Prepare all outputs before touching an existing DAT. A conflicting save is
 // rejected rather than replacing other edits made after this snapshot.
 for(auto &change:changes)
  if(!SameSaved(change.path,change.before)||!Prepare(change.path,change.after,change.temporary))
   {error="Inventory changed or could not be prepared. Reopen and try again.";return false;}
 bool failed=false;
 for(auto &change:changes)
 {
  if(!SameSaved(change.path,change.before)||!Replace(DiskPath(change.temporary),DiskPath(change.path)))
   {failed=true;break;}
  change.committed=true;
  std::string saved;
  if(!oc2_local_inventory::Read(change.path,saved)||saved!=change.after){failed=true;break;}
 }
 if(!failed){error.clear();return true;}
 bool restored=true;
 for(auto it=changes.rbegin();it!=changes.rend();++it)if(it->committed&&!Restore(*it))restored=false;
 error=restored?"Inventory save failed. Previous settings were restored.":
  "Inventory save was interrupted; reopen the inventory before editing again.";
 return false;
}
inline bool Persist(State &state,bool project,std::string &error,const oc2_accessories::WeaponConfig *defaults=nullptr)
{
 state.profiles.revision=oc2_profiles::NextRevision(state.profiles.revision);
 std::vector<Change> changes;
 changes.emplace_back(oc2_profiles::kConfigPath,state.profileFile,oc2_profiles::SerializeConfig(state.profiles));
 if(defaults)
 {
  if(!oc2_mount_defaults::Put(state.mountDefaults,*defaults)){error="Invalid shared mount layout";return false;}
  state.mountDefaults.revision=oc2_profiles::NextRevision(state.mountDefaults.revision);
  changes.emplace_back(oc2_mount_defaults::kPath,state.mountFile,oc2_mount_defaults::Serialize(state.mountDefaults));
 }
 if(project)
 {
  state.loadout.revision=oc2_profiles::NextRevision(state.loadout.revision);
  state.accessories.revision=oc2_profiles::NextRevision(state.accessories.revision);
  changes.emplace_back(oc2_inventory::kConfigPath,state.loadoutFile,oc2_inventory::SerializeConfig(state.loadout));
  changes.emplace_back(oc2_accessories::kConfigPath,state.accessoryFile,oc2_accessories::SerializeConfig(state.accessories));
 }
 return SaveChanges(changes,error);
}
inline bool SaveProfile(const oc2_profiles::Profile &profile,const oc2_inventory::Catalog &catalog,std::string &error,bool shareDefaults=false)
{
 if(!oc2_profiles::Validate(profile,catalog,error))return false;
 State state;if(!Load(state,error))return false;
 if(!oc2_profiles::Put(state.profiles,profile)){error="Skin customization limit reached.";return false;}
 const bool active=IsEquipped(state,profile.appearance.weapon,profile.appearance.skin);
 if(active&&!oc2_profiles::Project(profile,state.loadout,state.accessories))
  {error="Skin customization could not be equipped.";return false;}
 return Persist(state,active,error,shareDefaults?&profile.accessories:nullptr);
}
inline bool EquipProfile(int weapon,int skin,const oc2_inventory::Catalog &catalog,std::string &error)
{
 State state;if(!Load(state,error))return false;
 const auto profile=Resolve(state,weapon,skin);
 if(!oc2_profiles::Validate(profile,catalog,error))return false;
 if(!oc2_profiles::Put(state.profiles,profile)||!oc2_profiles::Project(profile,state.loadout,state.accessories))
  {error="Skin customization could not be equipped.";return false;}
 return Persist(state,true,error);
}
}
