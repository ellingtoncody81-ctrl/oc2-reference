#pragma once
#include <string>
#include <vector>
namespace oc2_texture_stream
{
using Prepare=void(*)(std::vector<unsigned char>&,int,bool&,bool&);
enum class Status {Pending,Ready,Failed};
void Start(const char *directory,Prepare prepare);
void Reset();
void Shutdown();
void Request(const std::string &path,int limit,bool priority=false);
Status Take(const std::string &path,int limit,std::vector<unsigned char> &data,bool &resized,bool &dds);
}
