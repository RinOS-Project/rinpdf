#include "tounicode.h"
#include "lexer.h"
#include <utility>
#include <vector>
namespace rinpdf::parser {
namespace {
bool unicode_sequence(const std::string& bytes,std::u32string& result){
 if(bytes.empty()||(bytes.size()&1)||bytes.size()>512)return false;
 std::u32string decoded;
 for(size_t i=0;i<bytes.size();i+=2){
  uint32_t cp=(uint16_t(uint8_t(bytes[i]))<<8)|uint8_t(bytes[i+1]);
  if(cp>=0xd800&&cp<=0xdbff){
   if(i+3>=bytes.size())return false;
   const uint32_t lo=(uint16_t(uint8_t(bytes[i+2]))<<8)|uint8_t(bytes[i+3]);
   if(lo<0xdc00||lo>0xdfff)return false;
   cp=0x10000+((cp-0xd800)<<10)+(lo-0xdc00);i+=2;
  }else if(cp>=0xdc00&&cp<=0xdfff)return false;
  decoded.push_back(static_cast<char32_t>(cp));
 }
 result=std::move(decoded);return true;
}
bool code(const Token& t,CMapKey& key){
 if(t.kind!=TokenKind::HexString||t.value.empty()||t.value.size()>4)
  return false;
 key.width=static_cast<uint8_t>(t.value.size());
 key.code=0;
 for(unsigned char ch:t.value)key.code=(key.code<<8)|ch;
 return true;
}
}
bool parse_tounicode_sequences(const uint8_t* data,size_t size,
 std::map<CMapKey,std::u32string>& result,
 std::vector<std::pair<CMapKey,CMapKey>>* output_codespaces){
 if(!data||!size||size>1024*1024)return false;
 Lexer lexer(data,size);
 std::map<CMapKey,std::u32string> mapped;
 std::vector<std::pair<CMapKey,CMapKey>> codespaces;
 size_t total=0;
 while(true){
  Token t=lexer.next();
  if(t.kind==TokenKind::End)break;
  if(t.kind==TokenKind::Invalid)return false;
  // An inherited CMap cannot be silently ignored: it changes both
  // code boundaries and Unicode mappings.
  if(t.kind==TokenKind::Keyword&&t.value=="usecmap")return false;
  if(t.kind!=TokenKind::Integer)continue;
  unsigned count=0;
  if(t.value.empty()||t.value.size()>4)return false;
  for(char c:t.value){
   if(c<'0'||c>'9')return false;
   count=count*10+unsigned(c-'0');
  }
  Token op=lexer.next();
  if(op.kind!=TokenKind::Keyword)return false;
  if(op.value=="begincodespacerange"){
   if(!count||count>32)return false;
   for(unsigned i=0;i<count;++i){
    CMapKey lo,hi;
    if(!code(lexer.next(),lo)||!code(lexer.next(),hi)||
       lo.width!=hi.width||lo.code>hi.code)return false;
    if(codespaces.size()>=32)return false;
    for(const auto& range:codespaces){
     if(lo.width==range.first.width&&
        !(hi.code<range.first.code||lo.code>range.second.code))return false;
    }
    codespaces.emplace_back(lo,hi);
   }
   const Token end=lexer.next();
   if(end.kind!=TokenKind::Keyword||end.value!="endcodespacerange")return false;
   continue;
  }
  const bool chars=op.value=="beginbfchar";
  const bool ranges=op.value=="beginbfrange";
  if(!chars&&!ranges)continue;
  if(!count||count>4096||total+count>4096)return false;
  for(unsigned i=0;i<count;++i){
   CMapKey first,last;
   if(!code(lexer.next(),first))return false;
   if(ranges){
    if(!code(lexer.next(),last)||first.width!=last.width||
       first.code>last.code)return false;
   }else last=first;
   const size_t span=size_t(last.code)-first.code+1;
   if(total+span>4096)return false;
   Token dest=lexer.next();
   if(ranges&&dest.kind==TokenKind::ArrayOpen){
    for(size_t j=0;j<span;++j){
     Token entry=lexer.next();std::u32string sequence;
     if(entry.kind!=TokenKind::HexString||
        !unicode_sequence(entry.value,sequence)||
        !mapped.emplace(CMapKey{first.width,uint32_t(first.code+j)},
                        std::move(sequence)).second)return false;
    }
    if(lexer.next().kind!=TokenKind::ArrayClose)return false;
   }else{
    if(dest.kind!=TokenKind::HexString)return false;
    for(size_t j=0;j<span;++j){
     std::string bytes=dest.value;
     if(ranges){
      // Per PDF, increment the entire big-endian UTF-16BE destination.
      uint32_t carry=static_cast<uint32_t>(j);
      for(size_t k=bytes.size();k>0&&carry;--k){
       const uint32_t n=uint8_t(bytes[k-1])+(carry&255);
       bytes[k-1]=char(n&255);
       carry=(carry>>8)+(n>>8);
      }
      if(carry)return false;
     }
     std::u32string sequence;
     if(!unicode_sequence(bytes,sequence)||
        !mapped.emplace(CMapKey{first.width,uint32_t(first.code+j)},
                        std::move(sequence)).second)return false;
    }
   }
   total+=span;
  }
  Token end=lexer.next();
  if(end.kind!=TokenKind::Keyword||
     end.value!=(chars?"endbfchar":"endbfrange"))return false;
 }
 // Codespaces with different byte lengths must also be prefix-free.
 // Otherwise greedy decoding could split a code at the wrong boundary.
 for(size_t i=0;i<codespaces.size();++i)
  for(size_t j=i+1;j<codespaces.size();++j){
   const auto& short_range=codespaces[i].first.width<
     codespaces[j].first.width?codespaces[i]:codespaces[j];
   const auto& long_range=codespaces[i].first.width<
     codespaces[j].first.width?codespaces[j]:codespaces[i];
   if(short_range.first.width==long_range.first.width)continue;
   const unsigned shift=8*(long_range.first.width-short_range.first.width);
   if((long_range.first.code>>shift)<=short_range.second.code&&
      (long_range.second.code>>shift)>=short_range.first.code)return false;
  }
 if(mapped.empty())return false;
 if(!codespaces.empty())for(const auto& entry:mapped){
  bool covered=false;
  for(const auto& range:codespaces)
   if(entry.first.width==range.first.width&&
      entry.first.code>=range.first.code&&entry.first.code<=range.second.code){
    covered=true;break;
   }
  if(!covered)return false;
 }
 result=std::move(mapped);
 if(output_codespaces)*output_codespaces=std::move(codespaces);
 return true;
}
}
