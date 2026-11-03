#include "oc2_texture_stream.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <map>
#include <fstream>
#include <cstdint>
#include <algorithm>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace oc2_texture_stream
{
namespace
{
using Key=std::pair<std::string,int>;
struct Job {Status status=Status::Pending;std::vector<unsigned char> data;bool resized=false,dds=false;int priority=0;unsigned age=0;};
struct State
{
 std::mutex mutex;std::condition_variable changed;std::thread worker;
 std::map<Key,Job> jobs;std::deque<Key> queue;
 std::string directory;Prepare prepare=nullptr;
 unsigned generation=0,clock=0;bool stop=false;std::size_t readyBytes=0;
} state;
constexpr std::size_t readyBudget=(sizeof(void*)>=8?256u:64u)*1024u*1024u;
bool Read(const std::string &path,std::vector<unsigned char> &data)
{
#ifdef _WIN32
 const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,nullptr,0);
 if(!n)return false;std::wstring wide(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,&wide[0],n);
 std::ifstream in(wide.c_str(),std::ios::binary|std::ios::ate);
#else
 std::ifstream in(path,std::ios::binary|std::ios::ate);
#endif
 if(!in)return false;const auto bytes=in.tellg();if(bytes<=0||bytes>16*1024*1024)return false;
 data.resize(std::size_t(bytes));in.seekg(0);return bool(in.read(reinterpret_cast<char *>(data.data()),data.size()));
}
void Run()
{
 for(;;)
 {
  Key key;unsigned generation;std::string directory;Prepare prepare;
  {
   std::unique_lock<std::mutex> lock(state.mutex);
   state.changed.wait(lock,[]{return state.stop||(!state.queue.empty()&&state.readyBytes<readyBudget);});
   if(state.stop)return;
   key=std::move(state.queue.front());state.queue.pop_front();generation=state.generation;directory=state.directory;prepare=state.prepare;
  }
  Job result;
  try
  {
   if(Read(directory+"/"+key.first,result.data))
   {if(prepare)prepare(result.data,key.second,result.resized,result.dds);result.status=Status::Ready;}
   else result.status=Status::Failed;
  }
  catch(...){result.data.clear();result.status=Status::Failed;}
  {
   std::lock_guard<std::mutex> lock(state.mutex);
   if(state.stop)return;
   auto found=state.jobs.find(key);
   if(generation==state.generation&&found!=state.jobs.end())
   {result.age=found->second.age;result.priority=found->second.priority;state.readyBytes+=result.data.size();found->second=std::move(result);}
  }
 }
}
}
void Start(const char *directory,Prepare prepare)
{
 Shutdown();
 {std::lock_guard<std::mutex> lock(state.mutex);state.directory=directory?directory:"";state.prepare=prepare;state.stop=false;}
 state.worker=std::thread(Run);
}
void Reset()
{
 std::lock_guard<std::mutex> lock(state.mutex);++state.generation;state.queue.clear();state.jobs.clear();state.readyBytes=0;state.changed.notify_all();
}
void Shutdown()
{
 {std::lock_guard<std::mutex> lock(state.mutex);state.stop=true;state.changed.notify_all();}
 if(state.worker.joinable())state.worker.join();Reset();
}
void Request(const std::string &path,int limit,int priority)
{
 if(path.empty()||path.size()>=256||path.find("..")!=std::string::npos||path.find(':')!=std::string::npos||path[0]=='/'||path[0]=='\\')return;
 priority=std::max(0,std::min(3,priority));
 std::lock_guard<std::mutex> lock(state.mutex);const Key key(path,limit);
 if(state.stop)return;
 auto existing=state.jobs.find(key);
 const auto enqueue=[&](const Key &queued,int urgent){
  const auto position=urgent?std::find_if(state.queue.begin(),state.queue.end(),[urgent](const Key &other){
   const auto job=state.jobs.find(other);return job==state.jobs.end()||job->second.priority<urgent;}):state.queue.end();
  state.queue.insert(position,queued);
 };
 if(existing!=state.jobs.end())
 {
  existing->second.age=++state.clock;
  // Promote only once; repeated prefetch must not reverse the urgent FIFO.
  if(priority>existing->second.priority)
  {
   existing->second.priority=priority;
   const auto queued=std::find(state.queue.begin(),state.queue.end(),key);
   if(queued!=state.queue.end()){state.queue.erase(queued);enqueue(key,priority);}
  }
  if(existing->second.status!=Status::Pending){state.changed.notify_one();return;}
 }
 // A pending request can already be behind obsolete decoded results. Make
 // room even for existing jobs, while preserving recently requested ready
 // foreground images from speculative eviction.
 while(state.readyBytes>=readyBudget||(existing==state.jobs.end()&&state.jobs.size()>=512))
 {
  auto oldest=state.jobs.end();
  for(auto i=state.jobs.begin();i!=state.jobs.end();++i)
   if(i->first!=key&&i->second.status!=Status::Pending&&
      (!i->second.priority||priority||unsigned(state.clock-i->second.age)>512)&&
      (oldest==state.jobs.end()||i->second.priority<oldest->second.priority||
       (i->second.priority==oldest->second.priority&&i->second.age<oldest->second.age)))oldest=i;
  if(oldest==state.jobs.end()){state.changed.notify_one();return;}
  state.readyBytes-=oldest->second.data.size();state.jobs.erase(oldest);
 }
 if(existing!=state.jobs.end()){state.changed.notify_one();return;}

 Job job;job.age=++state.clock;job.priority=priority;state.jobs.emplace(key,std::move(job));
 enqueue(key,priority);state.changed.notify_one();
}
Status Take(const std::string &path,int limit,std::vector<unsigned char> &data,bool &resized,bool &dds)
{
 std::lock_guard<std::mutex> lock(state.mutex);auto found=state.jobs.find(Key(path,limit));
 if(found==state.jobs.end()||found->second.status==Status::Pending)return Status::Pending;
 const auto status=found->second.status;
 state.readyBytes-=found->second.data.size();data=std::move(found->second.data);resized=found->second.resized;dds=found->second.dds;
 state.jobs.erase(found);state.changed.notify_one();return status;
}
}
