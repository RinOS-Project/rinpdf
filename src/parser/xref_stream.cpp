#include "xref_stream.h"
#include "stream.h"
#include "stream_object.h"
#include <cstring>
#include <set>
#include <map>
#include <limits>
#include <utility>
namespace rinpdf::parser {
bool decode_xref_stream_entries(const uint8_t* data,size_t size,
 const std::vector<unsigned>& widths,const std::vector<uint64_t>& index,
 std::vector<XrefStreamEntry>& result){
 if((!data&&size)||widths.size()!=3||index.empty()||index.size()%2!=0||
    index.size()>2048)return false;
 size_t stride=0;
 for(unsigned w:widths){
  if(w>8||stride>24-w)return false;
  stride+=w;
 }
 if(stride==0)return false;
 uint64_t total=0;
 uint64_t previous_end=0;
 for(size_t i=0;i<index.size();i+=2){
  const uint64_t first=index[i],count=index[i+1];
  if(count==0||count>1000000||first>1000000||
     count>1000000-first||total>1000000-count)return false;
  // Reject overlapping or non-monotonic ranges; the caller may sort
  // entries only after successful decoding.
  if(i&&first<previous_end)return false;
  previous_end=first+count;
  total+=count;
 }
 if(total>static_cast<uint64_t>(std::numeric_limits<size_t>::max()/stride)||
    size!=static_cast<size_t>(total)*stride)return false;
 std::vector<XrefStreamEntry> decoded;
 decoded.reserve(static_cast<size_t>(total));
 size_t pos=0;
 auto field=[&](unsigned width)->uint64_t{
  uint64_t value=0;
  for(unsigned j=0;j<width;++j)value=(value<<8)|data[pos++];
  return value;
 };
 for(size_t i=0;i<index.size();i+=2){
  for(uint64_t n=0;n<index[i+1];++n){
   uint64_t type=widths[0]?field(widths[0]):1;
   uint64_t first=field(widths[1]),second=field(widths[2]);
   if(type>2)return false;
   // Type 0 (free), 1 (offset,generation), 2 (object stream,index).
   // A compressed object has an implicit generation number of zero.
   if(type==1&&second>65535)return false;
   decoded.push_back({static_cast<size_t>(index[i]+n),
                      static_cast<uint8_t>(type),first,second});
  }
 }
 result=std::move(decoded);
 return true;
}
static bool xref_space(uint8_t c){
 return c==0||c==9||c==10||c==12||c==13||c==32;
}
static bool xref_unsigned(const uint8_t* d,size_t n,size_t& p,uint64_t& out){
 while(p<n&&xref_space(d[p]))++p;
 if(p==n||d[p]<'0'||d[p]>'9')return false;
 uint64_t v=0;
 while(p<n&&d[p]>='0'&&d[p]<='9'){
  const unsigned digit=d[p++]-'0';
  if(v>(UINT64_MAX-digit)/10)return false;
  v=v*10+digit;
 }
 if(p<n&&!xref_space(d[p]))return false;
 out=v;return true;
}
static bool xref_integer(const Value& v,uint64_t& out){
 if(v.kind!=Value::Kind::Integer||v.integer<0)return false;
 out=static_cast<uint64_t>(v.integer);return true;
}
bool read_xref_stream_at(const uint8_t* data,size_t size,size_t offset,
 std::vector<XrefStreamEntry>& entries,Value& trailer){
 if(!data||offset>=size)return false;
 size_t p=offset;uint64_t object=0,generation=0;
 if(!xref_unsigned(data,size,p,object)||!xref_unsigned(data,size,p,generation)||
    object==0||generation!=0||object>1000000)return false;
 while(p<size&&xref_space(data[p]))++p;
 if(size-p<3||std::memcmp(data+p,"obj",3)!=0||
    (p+3<size&&!xref_space(data[p+3])))return false;
 p+=3;
 StreamRange stream;
 if(!locate_indirect_stream(data,size,
     XrefEntry{static_cast<size_t>(object),offset,0,true},{},stream))return false;
 const auto& dict=stream.dictionary.dictionary;
 auto type=dict.find("Type"),width=dict.find("W"),size_key=dict.find("Size");
 if(type==dict.end()||type->second.kind!=Value::Kind::Name||
    type->second.text!="XRef"||width==dict.end()||
    width->second.kind!=Value::Kind::Array||width->second.array.size()!=3||
    size_key==dict.end())return false;
 uint64_t declared_size=0;
 if(!xref_integer(size_key->second,declared_size)||declared_size==0||
    declared_size>1000000)return false;
 std::vector<unsigned> widths;
 for(const auto& item:width->second.array){
  uint64_t w=0;
  if(!xref_integer(item,w)||w>8)return false;
  widths.push_back(static_cast<unsigned>(w));
 }
 std::vector<uint64_t> index{0,declared_size};
 auto idx=dict.find("Index");
 if(idx!=dict.end()){
  if(idx->second.kind!=Value::Kind::Array||idx->second.array.empty()||
     idx->second.array.size()%2!=0)return false;
  index.clear();
  for(const auto& item:idx->second.array){
   uint64_t v=0;
   if(!xref_integer(item,v))return false;
   index.push_back(v);
  }
  for(size_t i=0;i<index.size();i+=2)
   if(index[i]>declared_size||index[i+1]>declared_size-index[i])return false;
 }
 std::vector<uint8_t> decoded;
 if(!decode_stream(data,size,stream,decoded,24*1000000))return false;
 std::vector<XrefStreamEntry> result;
 if(!decode_xref_stream_entries(decoded.data(),decoded.size(),widths,index,result))return false;
 entries=std::move(result);
 trailer=stream.dictionary;
 return true;
}

bool read_xref_section(const uint8_t* data,size_t size,
 XrefFormat& format,std::vector<XrefEntry>& classic_entries,
 std::vector<XrefStreamEntry>& stream_entries,Value& trailer){
 size_t offset=0;
 if(!find_startxref(data,size,offset))return false;
 std::vector<XrefEntry> classic;
 size_t trailer_offset=0;
 if(read_classic_xref(data,size,classic,&trailer_offset)){
  Lexer lexer(data+trailer_offset,size-trailer_offset);
  Value dict;
  if(!parse_value(lexer,dict)||dict.kind!=Value::Kind::Dictionary)return false;
  format=XrefFormat::Classic;
  classic_entries=std::move(classic);
  stream_entries.clear();
  trailer=std::move(dict);
  return true;
 }
 std::vector<XrefStreamEntry> stream;
 Value dict;
 if(!read_xref_stream_at(data,size,offset,stream,dict))return false;
 format=XrefFormat::Stream;
 stream_entries=std::move(stream);
 classic_entries.clear();
 trailer=std::move(dict);
 return true;
}

bool read_uncompressed_xref(const uint8_t* data,size_t size,
 std::vector<XrefEntry>& entries,Value& trailer){
 XrefFormat format=XrefFormat::Classic;
 std::vector<XrefEntry> classic;
 std::vector<XrefStreamEntry> stream;
 Value dictionary;
 if(!read_xref_section(data,size,format,classic,stream,dictionary))return false;
 if(dictionary.kind!=Value::Kind::Dictionary)return false;
 const auto& fields=dictionary.dictionary;
 if(fields.count("Prev")||fields.count("XRefStm")||fields.count("Encrypt"))return false;
 if(format==XrefFormat::Classic){
  entries=std::move(classic);
  trailer=std::move(dictionary);
  return true;
 }
 std::vector<XrefEntry> resolved;
 resolved.reserve(stream.size());
 std::set<size_t> seen;
 for(const auto& item:stream){
  if(!seen.insert(item.object).second)return false;
  if(item.type==2)return false; // ObjStm resolution is not implemented.
  if(item.type==0)continue;
  if(item.type!=1||item.field1>=size||item.field2>65535)return false;
  resolved.push_back({item.object,static_cast<size_t>(item.field1),
                      static_cast<unsigned>(item.field2),true});
 }
 if(resolved.empty())return false;
 entries=std::move(resolved);
 trailer=std::move(dictionary);
 return true;
}

bool read_object_xref(const uint8_t* data,size_t size,
 std::vector<XrefEntry>& ordinary,std::vector<XrefStreamEntry>& compressed,
 Value& trailer){
 size_t current=0;
 if(!find_startxref(data,size,current))return false;
 std::set<size_t> visited;
 std::map<size_t,XrefStreamEntry> newest;
 Value latest;
 for(unsigned depth=0;depth<32;++depth){
  if(!visited.insert(current).second)return false;
  std::vector<XrefStreamEntry> section;
  Value dict;
  size_t trailer_pos=0;
  std::vector<XrefEntry> classic;
  if(read_classic_xref_at(data,size,current,classic,&trailer_pos)){
   Lexer lexer(data+trailer_pos,size-trailer_pos);
   if(!parse_value(lexer,dict)||dict.kind!=Value::Kind::Dictionary)return false;
   std::set<size_t> local;
   for(const auto& item:classic){
    if(item.object>1000000||!local.insert(item.object).second)return false;
    section.push_back({item.object,static_cast<uint8_t>(item.in_use?1:0),
                       static_cast<uint64_t>(item.offset),item.generation});
   }
   // /XRefStm supplements this revision's Classic table. Existing
   // Classic entries win when the two tables name the same object.
   auto hybrid=dict.dictionary.find("XRefStm");
   if(hybrid!=dict.dictionary.end()){
    if(hybrid->second.kind!=Value::Kind::Integer||
       hybrid->second.integer<0||
       static_cast<uint64_t>(hybrid->second.integer)>=current)return false;
    const size_t supplementary_offset=
      static_cast<size_t>(hybrid->second.integer);
    if(!visited.insert(supplementary_offset).second)return false;
    std::vector<XrefStreamEntry> supplement;
    Value supplement_dict;
    if(!read_xref_stream_at(data,size,supplementary_offset,
                            supplement,supplement_dict))return false;
    // A supplementary stream must not introduce a second update chain.
    if(supplement_dict.dictionary.count("Prev")||
       supplement_dict.dictionary.count("XRefStm")||
       supplement_dict.dictionary.count("Encrypt"))return false;
    std::set<size_t> supplement_seen;
    for(const auto& item:supplement){
     if(!supplement_seen.insert(item.object).second)return false;
     if(local.insert(item.object).second)section.push_back(item);
    }
   }
  }else if(!read_xref_stream_at(data,size,current,section,dict))return false;
  if(dict.dictionary.count("Encrypt"))return false;
  if(classic.empty()&&dict.dictionary.count("XRefStm"))return false;
  if(depth==0)latest=dict;
  else for(const auto* key:{"Root","Info","ID"}){
   if(!latest.dictionary.count(key)){
    auto found=dict.dictionary.find(key);
    if(found!=dict.dictionary.end())latest.dictionary.emplace(key,found->second);
   }
  }
  std::set<size_t> local;
  for(const auto& item:section){
   if(!local.insert(item.object).second)return false;
   if(item.type>2)return false;
   if(item.type==1&&(item.field1>=size||item.field2>65535))return false;
   if(item.type==2&&(item.field1==0||item.field1>1000000||
                      item.field2>100000))return false;
   // Newer free/type-1/type-2 entries all override older revisions.
   newest.emplace(item.object,item);
  }
  if(newest.size()>1000000)return false;
  auto prev=dict.dictionary.find("Prev");
  if(prev==dict.dictionary.end()){
   std::vector<XrefEntry> normals;
   std::vector<XrefStreamEntry> all;
   for(const auto& [id,item]:newest){
    all.push_back(item);
    if(item.type==1)normals.push_back({
     id,static_cast<size_t>(item.field1),
     static_cast<unsigned>(item.field2),true});
    else if(item.type==0)normals.push_back({id,0,0,false});
   }
   if(normals.empty())return false;
   ordinary=std::move(normals);
   compressed=std::move(all);
   trailer=std::move(latest);
   return true;
  }
  if(prev->second.kind!=Value::Kind::Integer||prev->second.integer<0||
     static_cast<uint64_t>(prev->second.integer)>=current)return false;
  current=static_cast<size_t>(prev->second.integer);
 }
 return false;
}

}
