#include "text_extract.h"
#include "lexer.h"
#include "value.h"
#include "../renderer/text_show.h"
#include <map>
#include <vector>
namespace rinpdf::parser {
namespace {
bool utf8_append(std::string& out,uint32_t cp){
 if(cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
 if(cp<0x80)out.push_back(char(cp));
 else if(cp<0x800){
  out.push_back(char(0xc0|(cp>>6)));out.push_back(char(0x80|(cp&63)));
 }else if(cp<0x10000){
  out.push_back(char(0xe0|(cp>>12)));out.push_back(char(0x80|((cp>>6)&63)));
  out.push_back(char(0x80|(cp&63)));
 }else{
  out.push_back(char(0xf0|(cp>>18)));out.push_back(char(0x80|((cp>>12)&63)));
  out.push_back(char(0x80|((cp>>6)&63)));out.push_back(char(0x80|(cp&63)));
 }
 return out.size()<=4*1024*1024;
}
bool actual_text(const std::string& raw,std::string& out){
 if(raw.size()<2||uint8_t(raw[0])!=0xfe||uint8_t(raw[1])!=0xff||
    (raw.size()&1))return false;
 std::string decoded;
 for(size_t i=2;i<raw.size();i+=2){
  uint32_t cp=(uint16_t(uint8_t(raw[i]))<<8)|uint8_t(raw[i+1]);
  if(cp>=0xd800&&cp<=0xdbff){
   if(i+3>=raw.size())return false;
   uint32_t tail=(uint16_t(uint8_t(raw[i+2]))<<8)|uint8_t(raw[i+3]);
   if(tail<0xdc00||tail>0xdfff)return false;
   cp=0x10000+((cp-0xd800)<<10)+(tail-0xdc00);i+=2;
  }
  if(!utf8_append(decoded,cp))return false;
 }
 out=std::move(decoded);return true;
}
bool decode_run(const std::string& bytes,const PageFont& font,std::string& out){
 if(font.to_unicode.empty())return false;
 const bool wide=font.subtype=="Type0";
 const unsigned width=wide?2:1;
 if(bytes.size()%width)return false;
 for(size_t i=0;i<bytes.size();i+=width){
  uint16_t code=uint8_t(bytes[i]);
  if(wide)code=uint16_t((code<<8)|uint8_t(bytes[i+1]));
  const auto it=font.to_unicode.find(CMapKey{uint8_t(width),code});
  if(it==font.to_unicode.end()||!utf8_append(out,it->second))return false;
 }
 return true;
}
}
bool extract_page_text(const uint8_t* pdf,size_t size,size_t page,
 std::string& utf8){
 std::vector<uint8_t> contents;
 std::map<std::string,PageFont> fonts;
 if(!read_page_contents(pdf,size,page,contents)||
    !read_page_fonts(pdf,size,page,fonts))return false;
 Lexer lexer(contents.data(),contents.size());
 std::string selected,output;
 bool text=false;
 std::vector<std::string> actual_stack;
 std::vector<bool> suppress_stack;
 std::string pending;
 bool have_pending=false;
 unsigned suppressed=0;
 size_t tokens=0;
 while(true){
  if(++tokens>1000000)return false;
  const Token t=lexer.next();
  if(t.kind==TokenKind::End)break;
  if(t.kind==TokenKind::Invalid)return false;
  if(t.kind==TokenKind::Name){
   Lexer look=lexer;
   Token next=look.next();
   if(next.kind==TokenKind::DictOpen){
    Value dict;
    Lexer value_lexer=lexer;
    if(!parse_value(value_lexer,dict,8)||dict.kind!=Value::Kind::Dictionary)
     return false;
    Token op=value_lexer.next();
    if(op.kind==TokenKind::Keyword&&op.value=="BDC"){
     bool replace=false;
     const auto it=dict.dictionary.find("ActualText");
     if(it!=dict.dictionary.end()){
      if(it->second.kind!=Value::Kind::String)return false;
      std::string replacement;
      if(!actual_text(it->second.text,replacement))return false;
      if(!suppressed){
       if(output.size()+replacement.size()>4*1024*1024)return false;
       output+=replacement;
      }
      replace=true;
     }
     if(actual_stack.size()>=64)return false;
     actual_stack.push_back({});
     suppress_stack.push_back(replace);
     suppressed+=replace;
     lexer=value_lexer;continue;
    }
   }
   if(next.kind==TokenKind::Keyword&&next.value=="BMC"){
    if(actual_stack.size()>=64)return false;
    actual_stack.push_back({});
    suppress_stack.push_back(false);
    lexer=look;continue;
   }
   if(next.kind==TokenKind::Name){
    Token op=look.next();
    if(op.kind==TokenKind::Keyword&&op.value=="BDC"){
     if(actual_stack.size()>=64)return false;
     actual_stack.push_back({});
     suppress_stack.push_back(false);
     lexer=look;continue;
    }
   }
   if(text){
    Lexer choose=lexer;
    Token amount=choose.next();
    Token tf=choose.next();
    if((amount.kind==TokenKind::Integer||amount.kind==TokenKind::Real||
        amount.kind==TokenKind::Keyword)&&tf.kind==TokenKind::Keyword&&
        tf.value=="Tf"&&fonts.count(t.value)){
     selected=t.value;lexer=choose;continue;
    }
   }
   continue;
  }
  if(t.kind==TokenKind::String||t.kind==TokenKind::HexString){
   pending=t.value;have_pending=true;continue;
  }
  if(t.kind==TokenKind::ArrayOpen&&text){
   std::vector<renderer::TextRun> runs;
   if(!renderer::parse_text_array(lexer,runs)||!fonts.count(selected))return false;
   if(!suppressed)for(const auto& run:runs)if(!run.adjustment&&
      !decode_run(run.bytes,fonts.at(selected),output))return false;
   continue;
  }
  if(t.kind!=TokenKind::Keyword)continue;
  if(t.value=="BT"){text=true;continue;}
  if(t.value=="ET"){text=false;continue;}
  if(t.value=="EMC"){
   if(actual_stack.empty())return false;
   suppressed-=suppress_stack.back();
   suppress_stack.pop_back();actual_stack.pop_back();
   continue;
  }
  if(t.value=="Tj"&&text){
   if(!have_pending||!fonts.count(selected))return false;
   if(!suppressed&&!decode_run(pending,fonts.at(selected),output))return false;
   pending.clear();have_pending=false;
  }
 }
 if(text||!actual_stack.empty()||have_pending)return false;
 utf8=std::move(output);return true;
}
}
