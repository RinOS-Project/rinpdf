#include "object_stream.h"
#include "xref_stream.h"
#include "stream_object.h"
#include "stream.h"
#include <set>
#include <limits>
#include <utility>
namespace rinpdf::parser {
static bool space(uint8_t c){return c==0||c==9||c==10||c==12||c==13||c==32;}
static bool natural(const uint8_t* data,size_t limit,size_t& pos,size_t& out){
 while(pos<limit&&space(data[pos]))++pos;
 if(pos==limit||data[pos]<'0'||data[pos]>'9')return false;
 size_t v=0;
 while(pos<limit&&data[pos]>='0'&&data[pos]<='9'){
  const unsigned digit=data[pos++]-'0';
  if(v>(std::numeric_limits<size_t>::max()-digit)/10)return false;
  v=v*10+digit;
 }
 if(pos<limit&&!space(data[pos]))return false;
 out=v;return true;
}
bool decode_object_stream(const uint8_t* payload,size_t length,
 size_t count,size_t first,std::vector<ObjectStreamItem>& output){
 if(!payload||count==0||count>100000||first>=length||
    length>64*1024*1024||count>first/4+1)return false;
 std::vector<size_t> ids,offsets;
 ids.reserve(count);offsets.reserve(count);
 std::set<size_t> unique;
 size_t pos=0;
 for(size_t i=0;i<count;++i){
  size_t id=0,off=0;
  if(!natural(payload,first,pos,id)||!natural(payload,first,pos,off)||
     id==0||id>1000000||!unique.insert(id).second||
     off>=length-first||(i&&off<=offsets.back()))return false;
  ids.push_back(id);offsets.push_back(off);
 }
 while(pos<first){if(!space(payload[pos++]))return false;}
 std::vector<ObjectStreamItem> result;
 result.reserve(count);
 for(size_t i=0;i<count;++i){
  const size_t begin=first+offsets[i];
  const size_t end=i+1<count?first+offsets[i+1]:length;
  if(begin>=end)return false;
  Lexer lexer(payload+begin,end-begin);
  Value value;
  if(!parse_value(lexer,value)||value.kind==Value::Kind::Invalid)return false;
  const Token trailing=lexer.next();
  if(trailing.kind!=TokenKind::End)return false;
  result.push_back({ids[i],std::move(value)});
 }
 output=std::move(result);
 return true;
}
bool resolve_compressed_object(const uint8_t* data,size_t size,
 const std::vector<XrefStreamEntry>& xref,size_t object,Value& output){
 if(!data||object==0)return false;
 const XrefStreamEntry* target=nullptr;
 for(const auto& item:xref)
  if(item.object==object){if(target)return false;target=&item;}
 if(!target||target->type!=2||target->field1==0||
    target->field1>1000000||target->field2>100000)return false;
 const XrefStreamEntry* container=nullptr;
 for(const auto& item:xref)
  if(item.object==target->field1){if(container)return false;container=&item;}
 if(!container||container->type!=1||container->field2!=0||
    container->field1>=size)return false;
 StreamRange range;
 const XrefEntry entry{container->object,
                      static_cast<size_t>(container->field1),0,true};
 if(!locate_indirect_stream(data,size,entry,{},range))return false;
 const auto& dict=range.dictionary.dictionary;
 auto type=dict.find("Type"),n=dict.find("N"),first=dict.find("First");
 if(type==dict.end()||type->second.kind!=Value::Kind::Name||
    type->second.text!="ObjStm"||n==dict.end()||first==dict.end()||
    n->second.kind!=Value::Kind::Integer||
    first->second.kind!=Value::Kind::Integer||
    n->second.integer<=0||n->second.integer>100000||
    first->second.integer<0)return false;
 std::vector<uint8_t> decoded;
 if(!decode_stream(data,size,range,decoded,64*1024*1024))return false;
 std::vector<ObjectStreamItem> objects;
 if(!decode_object_stream(decoded.data(),decoded.size(),
       static_cast<size_t>(n->second.integer),
       static_cast<size_t>(first->second.integer),objects))return false;
 const size_t index=static_cast<size_t>(target->field2);
 if(index>=objects.size()||objects[index].object!=object)return false;
 output=std::move(objects[index].value);
 return true;
}

}
