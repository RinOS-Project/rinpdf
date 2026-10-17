#include "text_show.h"
#include <charconv>
#include <cmath>
namespace rinpdf::renderer {
bool parse_text_array(parser::Lexer& lexer,std::vector<TextRun>& runs){
 std::vector<TextRun> parsed;
 size_t bytes=0;
 for(size_t n=0;n<4096;++n){
  auto token=lexer.next();
  if(token.kind==parser::TokenKind::ArrayClose){
   runs=std::move(parsed);return true;
  }
  if(token.kind==parser::TokenKind::String||
     token.kind==parser::TokenKind::HexString){
   if(token.value.size()>1048576-bytes)return false;
   bytes+=token.value.size();
   parsed.push_back({false,std::move(token.value),0});
   continue;
  }
  if(token.kind!=parser::TokenKind::Integer&&
     token.kind!=parser::TokenKind::Keyword)return false;
  double value=0;
  const auto result=std::from_chars(token.value.data(),
       token.value.data()+token.value.size(),value);
  if(result.ec!=std::errc{}||result.ptr!=token.value.data()+token.value.size()||
     !std::isfinite(value)||std::abs(value)>10000000)return false;
  parsed.push_back({true,{},value});
 }
 return false;
}
}
