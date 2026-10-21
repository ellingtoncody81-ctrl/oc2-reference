#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

// CS:GO separates CEconItemView's instance ID from its definition, and maps
// (team, loadout slot) to instance IDs. This menu model borrows those concepts;
// providers supply snapshots on the UI thread, without an econ/Steam dependency.
namespace oc2_frontend
{
typedef std::uint64_t InventoryItemId;
typedef std::uint32_t InventoryDefinitionId;
enum class InventoryStatus { Unavailable, Loading, Ready, Error };
enum class InventoryCategory { All, Weapons, Pistols, Rifles, SMGs, Heavy, Knives, Grenades, Equipment, Other };
enum class InventoryTeam { None, Terrorist, CounterTerrorist };
enum class InventorySort { Name, Definition, Instance };

struct InventoryItem
{
    InventoryItemId id=0;
    InventoryDefinitionId definitionId=0;
    std::string name;
    InventoryCategory category=InventoryCategory::Other;
    std::string iconKey;
};
struct InventoryLoadout
{
    InventoryTeam team=InventoryTeam::None;
    int slot=-1;
    InventoryItemId itemId=0;
};
struct InventorySnapshot
{
    InventoryStatus status=InventoryStatus::Unavailable;
    std::uint64_t revision=0;
    std::vector<InventoryItem> items;
    std::vector<InventoryLoadout> loadout;
    std::string message;
};

// A resource preview has no owned-instance ID. Legacy network weapon IDs are
// a separate namespace from econ definitions (e.g. Glock17 versus definition4).
struct InventoryResourcePreview
{
    std::string key;
    InventoryDefinitionId definitionId=0;
    int legacyWeaponId=0;
    std::string name;
    InventoryCategory category=InventoryCategory::Other;
    std::string iconKey;
};
struct InventoryReply
{
    std::uint64_t requestId=0;
    InventorySnapshot snapshot;
};
class IInventoryProvider
{
public:
    virtual ~IInventoryProvider() {}
    virtual void RequestRefresh(std::uint64_t requestId)=0;
    // Implementations marshal asynchronous work into this UI-thread poll.
    // Return each owned reply once; do not retain references to the output.
    virtual bool Poll(InventoryReply &reply)=0;
};
class EmptyInventoryProvider : public IInventoryProvider
{
public:
    void RequestRefresh(std::uint64_t id) override { request=id;pending=true; }
    bool Poll(InventoryReply &reply) override
    {
        if (!pending) return false;
        pending=false;reply=InventoryReply();reply.requestId=request;
        reply.snapshot.message="Inventory provider is not connected";
        return true;
    }
private:
    std::uint64_t request=0;
    bool pending=false;
};
struct InventoryPage
{
    std::size_t total=0, page=0, pageSize=20, pages=0;
    std::vector<InventoryItemId> ids;
};

class InventoryModel
{
public:
    typedef InventoryStatus Status;
    typedef InventoryCategory Category;
    explicit InventoryModel(IInventoryProvider *provider=nullptr)
    {
        SetProvider(provider);Tick();
    }
    InventoryModel(const InventoryModel &)=delete;
    InventoryModel &operator=(const InventoryModel &)=delete;
    // External providers are borrowed and must outlive this model's use of them.
    void SetProvider(IInventoryProvider *provider)
    {
        source=provider?provider:&empty;snapshot=InventorySnapshot();selected=0;page=0;
        RequestRefresh();
    }
    void RequestRefresh()
    {
        ++request;if (!request) ++request;
        awaiting=true;snapshot.status=Status::Loading;snapshot.message.clear();
        source->RequestRefresh(request);
    }
    void Tick()
    {
        InventoryReply reply;
        // A busy or faulty provider cannot hold the menu's frame indefinitely.
        for (int i=0;i<8 && source->Poll(reply);++i)
        {
            if (!awaiting || reply.requestId!=request) continue;
            if (reply.snapshot.status==Status::Loading) continue;
            if (reply.snapshot.status!=Status::Ready && reply.snapshot.status!=Status::Error && reply.snapshot.status!=Status::Unavailable)
            {
                snapshot.status=Status::Error;snapshot.message="Inventory provider status is invalid";awaiting=false;continue;
            }
            if (reply.snapshot.status==Status::Ready)
            {
                if (reply.snapshot.revision && reply.snapshot.revision<snapshot.revision) continue;
                std::string error;
                if (!Validate(reply.snapshot,error))
                {
                    snapshot.status=Status::Error;snapshot.message=error;awaiting=false;continue;
                }
                snapshot=reply.snapshot;
                if (selected && !Find(selected)) selected=0;
            }
            else
            {
                // Failed refreshes keep only the last accepted data, never rows
                // attached to a provider's error or unavailable reply.
                snapshot.status=reply.snapshot.status;snapshot.message=reply.snapshot.message;
            }
            awaiting=false;
        }
    }
    const InventorySnapshot &Snapshot() const { return snapshot; }
    bool IsRefreshing() const { return awaiting; }
    InventoryItemId SelectedId() const { return selected; }
    bool Select(InventoryItemId id)
    {
        if (id && !Find(id)) return false;
        selected=id;return true;
    }
    // Returned views remain valid until the next accepted provider snapshot.
    const InventoryItem *Find(InventoryItemId id) const
    {
        for (const auto &item:snapshot.items) if (item.id==id) return &item;
        return nullptr;
    }
    const InventoryItem *InLoadout(InventoryTeam team,int slot) const
    {
        for (const auto &binding:snapshot.loadout)
            if (binding.team==team && binding.slot==slot) return Find(binding.itemId);
        return nullptr;
    }
    void SetFilter(Category category=Category::All,const std::string &search=std::string())
    {
        filter=category;query=Fold(search);page=0;
    }
    void SetSort(InventorySort value) { sort=value;page=0; }
    void SetPage(std::size_t index,std::size_t size=20)
    {
        page=index;pageSize=size?size:1;if (pageSize>128) pageSize=128;
    }
    InventoryPage Page() const
    {
        std::vector<const InventoryItem *> items;
        for (const auto &item:snapshot.items)
            if (Matches(item.category) && (query.empty() || Fold(item.name).find(query)!=std::string::npos))
                items.push_back(&item);
        std::sort(items.begin(),items.end(),[this](const InventoryItem *a,const InventoryItem *b)
        {
            if (sort==InventorySort::Name)
            {
                const std::string left=Fold(a->name),right=Fold(b->name);
                if (left!=right) return left<right;
            }
            if (sort!=InventorySort::Instance && a->definitionId!=b->definitionId)
                return a->definitionId<b->definitionId;
            return a->id<b->id;
        });
        InventoryPage result;result.total=items.size();result.pageSize=pageSize;
        result.pages=items.empty()?0:1+(items.size()-1)/pageSize;
        result.page=result.pages && page>=result.pages?result.pages-1:page;
        if (!result.pages) result.page=0;
        const std::size_t begin=result.page*pageSize;
        for (std::size_t i=begin;i<items.size() && result.ids.size()<pageSize;++i)
            result.ids.push_back(items[i]->id);
        return result;
    }
    void SetResourceCatalog(const std::vector<InventoryResourcePreview> &items) { catalog=items; }
    const std::vector<InventoryResourcePreview> &ResourceCatalog() const { return catalog; }
private:
    static std::string Fold(std::string value)
    {
        // ASCII search folding leaves UTF-8 display-name bytes unchanged.
        for (char &c:value) if (c>='A' && c<='Z') c=static_cast<char>(c+('a'-'A'));
        return value;
    }
    bool Matches(Category value) const
    {
        return filter==Category::All || value==filter || (filter==Category::Weapons &&
            value!=Category::All && value!=Category::Equipment && value!=Category::Other);
    }
    static bool Validate(const InventorySnapshot &value,std::string &error)
    {
        if (!value.revision) { error="Inventory revision is missing";return false; }
        std::set<InventoryItemId> ids;
        for (const auto &item:value.items)
        {
            if (!item.id || !item.definitionId)
                { error="Inventory item identity is missing";return false; }
            if (!ids.insert(item.id).second)
                { error="Duplicate inventory item identity";return false; }
        }
        std::set<std::pair<int,int> > slots;
        for (const auto &slot:value.loadout)
        {
            if (!ids.count(slot.itemId) || slot.slot<0 || (slot.team!=InventoryTeam::Terrorist && slot.team!=InventoryTeam::CounterTerrorist))
                { error="Inventory loadout reference is invalid";return false; }
            if (!slots.insert(std::make_pair(static_cast<int>(slot.team),slot.slot)).second)
                { error="Duplicate inventory loadout slot";return false; }
        }
        return true;
    }
    EmptyInventoryProvider empty;
    IInventoryProvider *source=nullptr;
    InventorySnapshot snapshot;
    std::vector<InventoryResourcePreview> catalog;
    std::uint64_t request=0;
    bool awaiting=false;
    InventoryItemId selected=0;
    Category filter=Category::All;
    InventorySort sort=InventorySort::Name;
    std::string query;
    std::size_t page=0,pageSize=20;
};
}
