#include "stream_object.h"
#include "object.h"
#include <algorithm>
#include <cstring>
#include <limits>
namespace rinpdf::parser {
static bool space(unsigned char c){return c==0||c==9||c==10||c==12||c==13||c==32;}
static bool number(const uint8_t *d,size_t n,size_t& p,size_t& value){
 while(p<n&&space(d[p]))++p;
 if(p==n||d[p]<'0'||d[p]>'9')return false;
 value=0;
 while(p<n&&d[p]>='0'&&d[p]<='9'){
  unsigned digit=d[p++]-'0';
  if(value>(std::numeric_limits<size_t>::max()-digit)/10)return false;
  value=value*10+digit;
 }
 return p==n||space(d[p]);
}
static bool header(const uint8_t *d,size_t n,const XrefEntry& entry,size_t& start){
 if(!entry.in_use||entry.offset>=n)return false;
 size_t p=entry.offset,id=0,gen=0;
 if(!number(d,n,p,id)||!number(d,n,p,gen)||id!=entry.object||gen!=entry.generation)return false;
 while(p<n&&space(d[p]))++p;
 if(n-p<3||std::memcmp(d+p,"obj",3)!=0||(p+3<n&&!space(d[p+3])))return false;
 start=p+3;return true;
}
bool locate_indirect_stream(const uint8_t *data,size_t size,const XrefEntry& entry,const std::vector<XrefEntry>& xref,StreamRange& result){
 result=StreamRange{};if(!data)return false;
 size_t begin=0;if(!header(data,size,entry,begin))return false;
 Lexer lexer(data+begin,size-begin);Value dict;
 if(!parse_value(lexer,dict)||dict.kind!=Value::Kind::Dictionary)return false;
 auto it=dict.dictionary.find("Length");
 if(it==dict.dictionary.end())return false;
 size_t length=0;
 if(it->second.kind==Value::Kind::Integer){
  if(it->second.integer<0)return false;
  length=(size_t)it->second.integer;
 }else if(it->second.kind==Value::Kind::Reference){
  const Value& ref=it->second;
  if(ref.integer<=0)return false;
  auto found=std::find_if(xref.begin(),xref.end(),[&](const XrefEntry& e){return e.in_use&&e.object==(size_t)ref.integer&&e.generation==ref.generation;});
  if(found==xref.end()||found->object==entry.object)return false;
  size_t a=0,b=0;if(!resolve_object(data,size,*found,a,b))return false;
  Lexer value_lexer(data+a,b-a);Value value;
  if(!parse_value(value_lexer,value)||value_lexer.next().kind!=TokenKind::End||value.kind!=Value::Kind::Integer||value.integer<0)return false;
  length=(size_t)value.integer;
 }else return false;
 size_t p=begin+lexer.position();
 while(p<size&&space(data[p]))++p;
 if(size-p<6||std::memcmp(data+p,"stream",6)!=0)return false;
 p+=6;if(p==size)return false;
 if(data[p]=='\r'){++p;if(p<size&&data[p]=='\n')++p;}
 else if(data[p]=='\n')++p;
 else return false;
 if(length>size-p)return false;
 size_t end=p+length,tail=end;
 if(tail<size&&data[tail]=='\r'){++tail;if(tail<size&&data[tail]=='\n')++tail;}
 else if(tail<size&&data[tail]=='\n')++tail;
 if(size-tail<9||std::memcmp(data+tail,"endstream",9)!=0)return false;
 tail+=9;while(tail<size&&space(data[tail]))++tail;
 if(size-tail<6||std::memcmp(data+tail,"endobj",6)!=0)return false;
 result.dictionary=std::move(dict);result.offset=p;result.length=length;
 return true;
}
}
