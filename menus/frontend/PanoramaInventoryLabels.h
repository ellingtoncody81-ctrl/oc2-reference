#pragma once
#include <string>
#include <cctype>
#include "../../../../common/oc2_display_names.h"
#include "../../../../common/oc2_skin_inventory.h"
#include "../../../../common/oc2_glove_inventory.h"

namespace oc2_inventory_labels
{
inline bool Chinese(const std::string &text)
{
    for(std::size_t i=0;i+2<text.size();++i)
    {
        const unsigned char a=text[i],b=text[i+1],c=text[i+2];
        if((a&0xf0)!=0xe0||(b&0xc0)!=0x80||(c&0xc0)!=0x80)continue;
        const unsigned code=((a&15)<<12)|((b&63)<<6)|(c&63);
        if(code>=0x3400&&code<=0x9fff)return true;
    }
    return false;
}
inline std::string NameKey(std::string name)
{
    const std::size_t first=name.find_first_not_of(" \t\r\n"),last=name.find_last_not_of(" \t\r\n");
    if(first==std::string::npos)return std::string();
    name=name.substr(first,last-first+1);
    for(char &c:name)if(c>='A'&&c<='Z')c=char(c+'a'-'A');
    return name;
}
inline bool EncodedName(const std::string &name)
{
    const std::string key=NameKey(name);if(key.empty())return true;
    bool numeric=true,hex=key.size()>=12;
    for(unsigned char c:key)
    {
        if(!std::isdigit(c)&&c!='_'&&c!='-'&&c!='.'&&c!=' ')numeric=false;
        if(!std::isxdigit(c))hex=false;
    }
    if(numeric||hex)return true;
    static const char *prefixes[]={"snip_","shot_","pist_","rif_","smg_","mg_"};
    for(const char *prefix:prefixes)if(key.compare(0,std::char_traits<char>::length(prefix),prefix)==0)
    {
        int numbers=0;
        for(std::size_t i=0;i+1<key.size();++i)if(key[i]=='_'&&std::isdigit(static_cast<unsigned char>(key[i+1])))
        {
            std::size_t end=i+1;while(end<key.size()&&std::isdigit(static_cast<unsigned char>(key[end])))++end;
            if(end==key.size()||key[end]=='_')++numbers;
        }
        if(numbers>=2)return true;
    }
    static const char *generic[]={"skin","texture","image","pattern","sticker","untitled","unnamed","unknown","custom","t","p"};
    for(const char *prefix:generic)
    {
        const std::size_t size=std::char_traits<char>::length(prefix);
        if(key.compare(0,size,prefix)!=0)continue;
        if(key.size()==size){if(size>1)return true;continue;}
        if(key[size]!='_'&&key[size]!='-'&&key[size]!=' ')continue;
        bool code=true,hasDigit=false;
        for(std::size_t i=size+1;i<key.size();++i)
        {
            const unsigned char c=key[i];if(std::isdigit(c))hasDigit=true;
            if(!std::isdigit(c)&&c!='_'&&c!='-'&&c!='.'&&c!=' ')code=false;
        }
        if(code&&hasDigit)return true;
    }
    return false;
}
inline const char *KnifeName(int definition)
{
    return oc2_display_names::Knife(definition);
}
inline const char *GloveFamilyKey(int index)
{
    static const char *names[]={"Bloodhound","Fingerless","Full Finger","Hand Wraps","Hard Knuckle","Motorcycle","Driver","Specialist","Sport"};
    return index>=0&&index<9?names[index]:"Team Default";
}
inline const char *GloveFamilyLabel(int index)
{
    static const char *names[]={u8"血猎手套",u8"半指手套",u8"全指手套",u8"裹手",u8"硬壳手套",u8"摩托手套",u8"驾驶手套",u8"专业手套",u8"运动手套"};
    return index>=0&&index<9?names[index]:u8"阵营默认";
}
inline const char *GloveCollection(const std::string &collection)
{
    for(int i=0;i<9;++i)if(collection==GloveFamilyKey(i))return GloveFamilyLabel(i);
    return Chinese(collection)?collection.c_str():u8"手套";
}
// Ordinals use the complete catalog group, so filtering and pagination never
// rename an item. Display names and their ordinal aliases remain searchable.
inline int PatternOrdinal(const oc2_inventory::Catalog &catalog,const oc2_inventory::Entry &selected)
{
    int ordinal=0;
    for(const auto &entry:catalog)
    {
        if(entry.kind!=selected.kind||entry.weapon!=selected.weapon||entry.definition!=selected.definition)continue;
        if(entry.kind==oc2_inventory::Kind::Sticker&&oc2_inventory::IsTransparent(entry))continue;
        ++ordinal;if(entry.id==selected.id)return ordinal;
    }
    return 0;
}
inline const char *FinishTranslation(const std::string &name);
inline std::string Finish(const oc2_inventory::Catalog &catalog,int id)
{
    if(!id)return u8"原版";
    const auto *entry=oc2_inventory::FindEntry(catalog,id);
    if(!entry)return u8"无法读取";
    if(const char *translation=FinishTranslation(entry->name))return translation;
    if(!EncodedName(entry->name))return entry->name;
    return std::string(u8"自定义涂装 ")+std::to_string(PatternOrdinal(catalog,*entry));
}
inline std::string Sticker(const oc2_inventory::Catalog &catalog,int id)
{
    if(id<0)return u8"未安装";
    if(!id)return u8"原贴纸";
    const auto *entry=oc2_inventory::FindEntry(catalog,id);
    if(!entry)return u8"无法读取";
    if(!EncodedName(entry->name))return entry->name;
    return std::string(u8"图案 ")+std::to_string(PatternOrdinal(catalog,*entry));
}
inline std::string SearchText(const oc2_inventory::Catalog &catalog,const oc2_inventory::Entry &entry)
{
    const std::string ordinal=std::to_string(PatternOrdinal(catalog,entry));
    const bool sticker=entry.kind==oc2_inventory::Kind::Sticker;
    return entry.name+" "+entry.collection+" "+(sticker?Sticker(catalog,entry.id):Finish(catalog,entry.id))+" "+
        (sticker?u8"图案 ":u8"自定义涂装 ")+ordinal+" "+(sticker?u8"图案":u8"自定义涂装")+ordinal+u8" 序号 "+ordinal+u8" 序号"+ordinal;
}
inline const char *FinishTranslation(const std::string &name)
{
    struct Variant {const char *key,*label;};
    static const Variant variants[]={
        {"Hard Knuckle Olive",u8"橄榄绿"},{"Hard Knuckle Black",u8"黑色"},{"Hard Knuckle Blue",u8"蓝色"},
        {"Blue Steel",u8"蓝钢"},{"Case Hardened",u8"表面淬火"},{"Crimson Web",u8"深红之网"},
        {"Damascus Steel",u8"大马士革钢"},{"Doppler",u8"多普勒"},{"Fade",u8"渐变之色"},
        {"Gamma Doppler",u8"伽玛多普勒"},{"Tiger Tooth",u8"虎牙"},
        {"Purple&White",u8"紫白配色"},{"Purple and White",u8"紫白配色"}
    };
    for(const auto &variant:variants)if(NameKey(name)==NameKey(variant.key))return variant.label;
    return nullptr;
}
inline const char *GloveVariant(const std::string &name){return FinishTranslation(name);}
inline std::string GloveFinish(const oc2_gloves::Catalog &catalog,int id)
{
    if(!id)return u8"阵营默认";
    const auto *selected=oc2_gloves::FindEntry(catalog,id);if(!selected)return u8"无法读取";
    if(Chinese(selected->name))return selected->name;
    if(selected->name==selected->collection)return std::string(GloveCollection(selected->collection))+u8"·原色";
    if(const char *variant=GloveVariant(selected->name))return variant;
    if(!EncodedName(selected->name))return selected->name;
    int ordinal=0;
    for(const auto &entry:catalog)if(entry.collection==selected->collection){++ordinal;if(entry.id==id)break;}
    return std::string(u8"自定义配色 ")+std::to_string(ordinal);
}
inline std::string GloveSearchText(const oc2_gloves::Catalog &catalog,const oc2_gloves::Entry &selected)
{
    int ordinal=0;for(const auto &entry:catalog)if(entry.collection==selected.collection){++ordinal;if(entry.id==selected.id)break;}
    const std::string number=std::to_string(ordinal);
    return selected.name+" "+selected.collection+" "+GloveCollection(selected.collection)+" "+GloveFinish(catalog,selected.id)+
        u8" 自定义配色 "+number+u8" 自定义配色"+number+u8" 序号 "+number+u8" 序号"+number;
}
}
