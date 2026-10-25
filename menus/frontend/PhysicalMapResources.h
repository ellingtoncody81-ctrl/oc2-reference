#pragma once
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>
#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#endif

namespace oc2_physical_maps
{
// The engine's gamedironly VFS also searches archives, downloaded/addon
// directories and the read-only game directory. Installed-map eligibility is
// deliberately checked against the current game's two loose-file directories.
inline std::string Path(const std::string &gameDirectory,const std::string &relative)
{
    if(gameDirectory.empty())return {};
    const char last=gameDirectory.back();
    return gameDirectory+(last=='/'||last=='\\'?"":"/")+relative;
}

inline bool FileExists(const std::string &gameDirectory,const std::string &relative)
{
    const std::string path=Path(gameDirectory,relative);
    if(path.empty())return false;
    FILE *file=std::fopen(path.c_str(),"rb");
    if(!file)return false;
    const bool exists=std::fseek(file,0,SEEK_END)==0&&std::ftell(file)>0;
    std::fclose(file);return exists;
}

inline bool ReadDefinition(const std::string &gameDirectory,const std::string &relative,std::string &text)
{
    text.clear();
    const std::string path=Path(gameDirectory,relative);
    if(path.empty())return false;
    FILE *file=std::fopen(path.c_str(),"rb");
    if(!file)return false;
    if(std::fseek(file,0,SEEK_END)!=0){std::fclose(file);return false;}
    const long size=std::ftell(file);
    if(size<=0||size>65536||std::fseek(file,0,SEEK_SET)!=0){std::fclose(file);return false;}
    text.resize(static_cast<size_t>(size));
    const size_t bytes=std::fread(&text[0],1,text.size(),file);
    std::fclose(file);
    if(bytes!=text.size()||text.find('\0')!=std::string::npos){text.clear();return false;}
    return true;
}

inline void AddMapName(const char *filename,std::vector<std::string> &names)
{
    std::string name=filename;
    if(name.size()<=4)return;
    std::string extension=name.substr(name.size()-4);
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(extension!=".bsp")return;
    name.resize(name.size()-4);
    if(name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)return;
    names.push_back(name);
}

inline std::vector<std::string> Names(const std::string &gameDirectory)
{
    std::vector<std::string> names;
    if(gameDirectory.empty())return names;
#ifdef _WIN32
    struct _finddata_t entry;
    const intptr_t handle=_findfirst(Path(gameDirectory,"maps/*").c_str(),&entry);
    if(handle==-1)return names;
    do
    {
        if(!(entry.attrib&_A_SUBDIR))AddMapName(entry.name,names);
    }while(_findnext(handle,&entry)==0);
    _findclose(handle);
#else
    DIR *directory=opendir(Path(gameDirectory,"maps").c_str());
    if(!directory)return names;
    while(const dirent *entry=readdir(directory))AddMapName(entry->d_name,names);
    closedir(directory);
#endif
    std::sort(names.begin(),names.end());
    names.erase(std::unique(names.begin(),names.end()),names.end());
    return names;
}
}
