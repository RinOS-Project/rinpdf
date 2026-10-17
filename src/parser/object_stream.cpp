#include "object_stream.h"
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
}
