#include "value.h"
#include <charconv>
#include <cmath>
#include <utility>
namespace rinpdf::parser {
static bool integer(const std::string& s,long long& v){
 if(s.empty())return false;
 const char *first=s.data(),*last=first+s.size();
 if(*first=='+')++first;
 if(first==last)return false;
 auto r=std::from_chars(first,last,v);
 return r.ec==std::errc()&&r.ptr==last;
}
static Token peek(Lexer& l){Lexer copy=l;return copy.next();}
static bool parse(Lexer& l,Value& out,unsigned depth,size_t& budget){
 if(budget==0)return false;--budget;
 Token t=l.next();out=Value{};
 switch(t.kind){
 case TokenKind::Integer:{
  long long v=0;if(!integer(t.value,v))return false;
  // PDF indirect references: unsigned-object unsigned-generation R
  if(v>=0){
   Lexer look=l;Token g=look.next(),r=look.next();long long generation=0;
   if(g.kind==TokenKind::Integer&&integer(g.value,generation)&&generation>=0&&generation<=65535&&r.kind==TokenKind::Keyword&&r.value=="R"){
    l=look;out.kind=Value::Kind::Reference;out.integer=v;out.generation=(unsigned)generation;return true;
   }
  }
  out.kind=Value::Kind::Integer;out.integer=v;return true;
 }
 case TokenKind::Real:{
  double number=0;
  const auto parsed=std::from_chars(t.value.data(),
      t.value.data()+t.value.size(),number,std::chars_format::fixed);
  if(parsed.ec!=std::errc()||parsed.ptr!=t.value.data()+t.value.size()||
     !std::isfinite(number))return false;
  out.kind=Value::Kind::Real;out.real=number;return true;
 }
  case TokenKind::Name:out.kind=Value::Kind::Name;out.text=std::move(t.value);return true;
 case TokenKind::String:case TokenKind::HexString:out.kind=Value::Kind::String;out.text=std::move(t.value);return true;
 case TokenKind::Keyword:
  if(t.value=="null"){out.kind=Value::Kind::Null;return true;}
  if(t.value=="true"||t.value=="false"){out.kind=Value::Kind::Boolean;out.boolean=t.value=="true";return true;}
  return false;
 case TokenKind::ArrayOpen:{
  if(depth==0)return false;
  out.kind=Value::Kind::Array;
  while(true){
   Token next=peek(l);
   if(next.kind==TokenKind::ArrayClose){l.next();return true;}
   if(next.kind==TokenKind::End||next.kind==TokenKind::Invalid||out.array.size()>=100000)return false;
   Value child;if(!parse(l,child,depth-1,budget))return false;
   out.array.push_back(std::move(child));
  }
 }
 case TokenKind::DictOpen:{
  if(depth==0)return false;
  out.kind=Value::Kind::Dictionary;
  while(true){
   Token next=peek(l);
   if(next.kind==TokenKind::DictClose){l.next();return true;}
   if(next.kind!=TokenKind::Name||out.dictionary.size()>=100000)return false;
   Token key=l.next();Value child;
   if(!parse(l,child,depth-1,budget))return false;
   if(!out.dictionary.emplace(std::move(key.value),std::move(child)).second)return false;
  }
 }
 default:return false;
 }
}
bool parse_value(Lexer& lexer,Value& result,unsigned max_depth){
 if(max_depth>64)max_depth=64;
 size_t budget=100000;
 return parse(lexer,result,max_depth,budget);
}
}
