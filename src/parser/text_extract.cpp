#include "text_extract.h"
#include "lexer.h"
#include "value.h"
#include "../renderer/text_show.h"
#include <map>
#include <charconv>
#include <cmath>
#include <vector>
#include <utility>
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
 if(font.unicode_sequences.empty())return false;
 const unsigned max_width=font.subtype=="Type0"?4:1;
 for(size_t i=0;i<bytes.size();){
  uint32_t code=0;
  size_t selected_width=0;
  const std::u32string* sequence=nullptr;
  for(unsigned width=1;width<=max_width&&i+width<=bytes.size();++width){
   code=(code<<8)|uint8_t(bytes[i+width-1]);
   bool is_code=false;
   if(font.unicode_codespaces.empty()){
    is_code=font.unicode_sequences.count(CMapKey{uint8_t(width),code})!=0;
   }else{
    for(const auto& range:font.unicode_codespaces){
     if(range.first.width==width&&code>=range.first.code&&
        code<=range.second.code){is_code=true;break;}
    }
   }
   if(is_code){
    // Codespaces determine boundaries, not whether a Unicode mapping exists.
    // Do not reinterpret an unmapped code using a longer mapping.
    selected_width=width;
    const auto it=font.unicode_sequences.find(CMapKey{uint8_t(width),code});
    sequence=it==font.unicode_sequences.end()?nullptr:&it->second;
    break;
   }
  }
  if(!sequence)return false;
  for(char32_t cp:*sequence)if(!utf8_append(out,uint32_t(cp)))return false;
  i+=selected_width;
 }
 return true;
}
}
bool extract_page_text(const uint8_t* pdf,size_t size,size_t page,
 std::string& utf8){
 std::vector<uint8_t> contents;
 std::map<std::string,PageFont> fonts;
 PageProperties properties;
 if(!read_page_contents(pdf,size,page,contents)||
    !read_page_fonts(pdf,size,page,fonts)||
    !read_page_properties(pdf,size,page,properties))return false;
 Lexer lexer(contents.data(),contents.size());
 std::string selected,output;
 bool text=false;
 std::vector<std::string> actual_stack;
 std::vector<bool> suppress_stack;
 std::string pending;
 bool have_pending=false;
 bool new_line_pending=false;
 std::vector<double> operands;
 double text_line_y=0;
 bool has_line_origin=false;
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
       if(new_line_pending&&!output.empty()&&output.back()!='\n')output+='\n';
       new_line_pending=false;
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
     auto property=properties.find(next.value);
     if(property==properties.end())return false;
     const bool replace=property->second.has_actual_text;
     if(replace){
      std::string replacement;
      if(!actual_text(property->second.actual_text,replacement))return false;
      if(!suppressed){
       if(new_line_pending&&!output.empty()&&output.back()!='\n')output+='\n';
       new_line_pending=false;
       if(output.size()+replacement.size()>4*1024*1024)return false;
       output+=replacement;
      }
     }
     actual_stack.push_back({});
     suppress_stack.push_back(replace);
     suppressed+=replace;
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
   // A second literal before Tj means the first string was never consumed.
   // Do not silently lose it and claim successful text extraction.
   if(!text)continue;
   if(have_pending)return false;
   pending=t.value;have_pending=true;continue;
  }
  if(t.kind==TokenKind::ArrayOpen&&text){
   std::vector<renderer::TextRun> runs;
   if(!renderer::parse_text_array(lexer,runs)||!fonts.count(selected))return false;
   if(!suppressed){
    bool has_glyphs=false;
    for(const auto& run:runs)if(!run.adjustment&&!run.bytes.empty())has_glyphs=true;
    if(has_glyphs&&new_line_pending&&!output.empty()&&output.back()!='\n')
     output+='\n';
    if(has_glyphs)new_line_pending=false;
    for(const auto& run:runs)if(!run.adjustment&&
       !decode_run(run.bytes,fonts.at(selected),output))return false;
   }
   continue;
  }
  if(text&&(t.kind==TokenKind::Integer||t.kind==TokenKind::Real)){
   double number=0;
   const char* begin=t.value.data();
   const char* end=begin+t.value.size();
   if(begin!=end&&*begin=='+')++begin;
   auto parsed=std::from_chars(begin,end,number,std::chars_format::fixed);
   if(parsed.ec!=std::errc()||parsed.ptr!=end||
      !std::isfinite(number)||operands.size()>=16)return false;
   operands.push_back(number);continue;
  }
  if(t.kind!=TokenKind::Keyword)continue;
  if(text&&(t.value=="Td"||t.value=="TD")){
   if(operands.size()!=2||have_pending)return false;
   if(operands[1]!=0&&!suppressed)new_line_pending=true;
   text_line_y+=operands[1];
   if(!std::isfinite(text_line_y))return false;
   has_line_origin=true;
   operands.clear();continue;
  }
  if(text&&t.value=="Tm"){
   if(operands.size()!=6||have_pending)return false;
   const double next_y=operands[5];
   if(has_line_origin&&std::abs(next_y-text_line_y)>0.001&&!suppressed)
    new_line_pending=true;
   text_line_y=next_y;
   has_line_origin=true;
   operands.clear();continue;
  }
  if(t.value=="BT"){text=true;operands.clear();has_line_origin=false;continue;}
  if(t.value=="T*"&&text){
   if(!suppressed)new_line_pending=true;
   continue;
  }
  if(t.value=="ET"){if(!operands.empty())return false;text=false;continue;}
  if(t.value=="EMC"){
   if(actual_stack.empty())return false;
   suppressed-=suppress_stack.back();
   suppress_stack.pop_back();actual_stack.pop_back();
   continue;
  }
  if(t.value=="Tj"&&text){
   if(!have_pending||!fonts.count(selected))return false;
   if(!suppressed){
    if(!pending.empty()&&new_line_pending&&!output.empty()&&
       output.back()!='\n')output+='\n';
    if(!pending.empty())new_line_pending=false;
    if(!decode_run(pending,fonts.at(selected),output))return false;
   }
   pending.clear();have_pending=false;
  }
 }
 if(text||!actual_stack.empty()||have_pending)return false;
 utf8=std::move(output);return true;
}
}
