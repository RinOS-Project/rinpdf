#include "tounicode.h"
#include "lexer.h"
#include <string>
#include <vector>
namespace rinpdf::parser {
static bool scalar(const std::string& s,uint32_t& unicode){
 if(s.size()!=2&&s.size()!=4)return false;
 auto unit=[&](size_t i){
  return (uint16_t(uint8_t(s[i]))<<8)|uint8_t(s[i+1]);
 };
 uint16_t first=unit(0);
 if(s.size()==2){
  if(first>=0xd800&&first<=0xdfff)return false;
  unicode=first;return true;
 }
 uint16_t second=unit(2);
 if(first<0xd800||first>0xdbff||second<0xdc00||second>0xdfff)return false;
 unicode=0x10000+((first-0xd800)<<10)+(second-0xdc00);
 return true;
}
static bool source(const Token& t,CMapKey& key){
 if(t.kind!=TokenKind::HexString||(t.value.size()!=1&&t.value.size()!=2))
  return false;
 key.width=static_cast<uint8_t>(t.value.size());
 key.code=t.value.size()==1?uint8_t(t.value[0]):
   (uint16_t(uint8_t(t.value[0]))<<8)|uint8_t(t.value[1]);
 return true;
}
bool parse_tounicode_cmap(const uint8_t* data,size_t size,
 std::map<CMapKey,uint32_t>& output){
 if(!data||size==0||size>1024*1024)return false;
 Lexer lexer(data,size);
 std::map<CMapKey,uint32_t> decoded;
 size_t operations=0;
 while(true){
  Token t=lexer.next();
  if(t.kind==TokenKind::End)break;
  if(t.kind==TokenKind::Invalid)return false;
  if(t.kind!=TokenKind::Integer)continue;
  unsigned count=0;
  if(t.value.empty()||t.value.size()>4)return false;
  for(char c:t.value){
   if(c<'0'||c>'9')return false;
   count=count*10+unsigned(c-'0');
  }
  Token op=lexer.next();
  if(op.kind!=TokenKind::Keyword)return false;
  const bool chars=op.value=="beginbfchar";
  const bool ranges=op.value=="beginbfrange";
  if(!chars&&!ranges)continue;
  if(count==0||count>4096||operations+count>4096)return false;
  operations+=count;
  for(unsigned i=0;i<count;++i){
   CMapKey low,high;
   if(!source(lexer.next(),low))return false;
   if(ranges){
    if(!source(lexer.next(),high)||high.width!=low.width||
       high.code<low.code||high.code-low.code>4095||
       operations+(high.code-low.code)>4096)return false;
   }else high=low;
   Token destination=lexer.next();
   if(destination.kind!=TokenKind::HexString)return false;
   uint32_t unicode=0;
   if(!scalar(destination.value,unicode))return false;
   // Contiguous ranges of BMP codepoints only; arrays and overflow fail closed.
   if(ranges&&(destination.value.size()!=2||
       uint64_t(unicode)+high.code-low.code>0xd7ff))return false;
   for(uint32_t code=low.code;code<=high.code;++code){
    if(!decoded.emplace(CMapKey{low.width,static_cast<uint16_t>(code)},
       unicode+code-low.code).second)return false;
   }
  }
  Token end=lexer.next();
  if(end.kind!=TokenKind::Keyword||
     end.value!=(chars?"endbfchar":"endbfrange"))return false;
 }
 if(decoded.empty())return false;
 output=std::move(decoded);return true;
}
}
