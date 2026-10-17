#include "text_extract.h"
#include "lexer.h"
#include "value.h"
#include "../renderer/text_show.h"
#include "../renderer/sfnt.h"
#include <map>
#include <charconv>
#include <cmath>
#include <algorithm>
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
  }else if(cp>=0xdc00&&cp<=0xdfff){
   // A low surrogate without a preceding high surrogate is invalid UTF-16.
   return false;
  }
  if(!utf8_append(decoded,cp))return false;
 }
 out=std::move(decoded);return true;
}
// TJ amounts are in thousandths of an em. Where an Identity-H CID font
// explicitly maps a two-byte code to U+0020 and supplies that CID's width,
// use the documented space advance instead of a universal half-em guess.
double tj_space_threshold(const PageFont& font){
 if(font.subtype!="Type0"){
  for(const auto& [key,sequence]:font.unicode_sequences){
   if(key.width!=1||sequence!=U" "||key.code>255)continue;
   const auto it=font.simple_widths.find(uint8_t(key.code));
   if(it!=font.simple_widths.end()&&it->second>0&&
      std::isfinite(it->second))
    return std::clamp(it->second,250.0,1000.0);
  }
  // MissingWidth is only a fallback for a code absent from /Widths;
  // an explicit zero-width entry must not be overridden.
  for(const auto& [key,sequence]:font.unicode_sequences){
   if(key.width!=1||sequence!=U" "||key.code>255)continue;
   if(font.simple_widths.count(uint8_t(key.code)))break;
   if(font.missing_width>0&&std::isfinite(font.missing_width))
    return std::clamp(font.missing_width,250.0,1000.0);
  }
 }
 if(font.subtype=="Type0"&&font.encoding=="Identity-H"){
  for(const auto& [key,sequence]:font.unicode_sequences){
   if(key.width!=2||sequence!=U" "||key.code>65535)continue;
   const auto it=font.cid_widths.overrides.find(uint16_t(key.code));
   if(it==font.cid_widths.overrides.end())continue;
   const double width=it->second;
   if(std::isfinite(width)&&width>0)
    return std::clamp(width,250.0,1000.0);
  }
 }
 if(font.subtype=="TrueType"&&!font.embedded_truetype.empty()){
  // Only use a font's physical space advance when its Unicode cmap
  // explicitly resolves U+0020 to a valid glyph. The PDF ToUnicode
  // mapping is not necessarily identical to the SFNT glyph mapping.
  renderer::SfntFont face;
  const auto& bytes=font.embedded_truetype;
  if(renderer::parse_sfnt(bytes.data(),bytes.size(),face)){
   uint16_t gid=0,units=0,advance=0;
   if(renderer::sfnt_glyph_id(face,0x20,gid)&&
      renderer::sfnt_horizontal_metrics(face,gid,units,advance)&&units)
    return std::clamp(1000.0*advance/units,250.0,1000.0);
  }
 }
 return 500;
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
 double font_size=0;
 double character_spacing=0;
 double word_spacing=0;
 double text_leading=0;
 double pending_tj_adjustment=0;
 bool text_run_emitted=false;
 double horizontal_scale=1;
 std::vector<double> operands;
 double text_line_x=0,text_line_y=0;
 double line_axis_x=1,line_axis_y=0;
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
     if(have_pending||!operands.empty())return false;
     const char* begin=amount.value.data();
     const char* end=begin+amount.value.size();
     if(begin!=end&&*begin=='+')++begin;
     double parsed_size=0;
     const auto parsed=std::from_chars(begin,end,parsed_size,
                                      std::chars_format::fixed);
     if(parsed.ec!=std::errc()||parsed.ptr!=end||
        !std::isfinite(parsed_size)||parsed_size<=0||
        parsed_size>10000)return false;
     selected=t.value;font_size=parsed_size;lexer=choose;continue;
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
   if(have_pending||!operands.empty()||
      !renderer::parse_text_array(lexer,runs)||!fonts.count(selected))return false;
   // An array alone is not a text-show operation: require its TJ operator.
   const Token show_op=lexer.next();
   if(show_op.kind!=TokenKind::Keyword||show_op.value!="TJ")return false;
   if(!suppressed){
    bool has_glyphs=false;
    for(const auto& run:runs)if(!run.adjustment&&!run.bytes.empty())has_glyphs=true;
    if(has_glyphs&&new_line_pending&&!output.empty()&&output.back()!='\n'){
     if(output.size()>=4*1024*1024)return false;
     output+='\n';
    }
    if(has_glyphs)new_line_pending=false;
    // Adjacent TJ adjustments combine: a later positive adjustment can
    // cancel an earlier gap. Only infer a space if the net forward offset
    // exceeds a conservative text-space threshold (0.5 em).
    double net_adjustment=pending_tj_adjustment;
    bool emitted_run=text_run_emitted;
    const double space_units=tj_space_threshold(fonts.at(selected));
    // TJ offsets are in 1/1000 em; Tc uses unscaled text units.
    // Horizontal scaling multiplies both advances, so cancels unless 0.
    // Tw only affects the encoded single-byte space (0x20), never CID
    // glyphs in a composite font. Require a known Unicode mapping.
    const auto& active_font=fonts.at(selected);
    const auto mapped_space=active_font.unicode_sequences.find(CMapKey{1,0x20});
    const bool word_space=active_font.subtype!="Type0"&&
       mapped_space!=active_font.unicode_sequences.end()&&
       mapped_space->second==U" ";
    const double spacing=character_spacing+(word_space?word_spacing:0);
    const double gap_threshold=font_size>0?
       space_units+1000.0*spacing/font_size:space_units;
    for(const auto& run:runs){
     if(run.adjustment){
      net_adjustment+=run.displacement;
      if(!std::isfinite(net_adjustment))return false;
      continue;
     }
     if(run.bytes.empty())continue;
     std::string decoded;
     if(!decode_run(run.bytes,fonts.at(selected),decoded))return false;
     if(decoded.empty())continue;
     if(emitted_run&&font_size>0&&horizontal_scale>0&&
        std::isfinite(gap_threshold)&&gap_threshold>0&&
        net_adjustment<=-gap_threshold&&
        !output.empty()&&output.back()!=' '&&output.back()!='\n'&&
        decoded.front()!=' '&&decoded.front()!='\n'){
      if(output.size()>=4*1024*1024)return false;
      output+=' ';
     }
     if(decoded.size()>4*1024*1024-output.size())return false;
     output+=decoded;
     emitted_run=true;
     text_run_emitted=true;
     net_adjustment=0;
    }
    pending_tj_adjustment=net_adjustment;
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
  if(text&&(t.value=="Tc"||t.value=="Tw"||t.value=="Tz"||t.value=="TL")){
   if(operands.size()!=1||have_pending)return false;
   const double v=operands.front();
   if(t.value=="Tc"||t.value=="Tw"||t.value=="TL"){
    if(std::abs(v)>10000)return false;
    if(t.value=="Tc")character_spacing=v;
    else if(t.value=="Tw")word_spacing=v;
    else text_leading=v;
   }else{
    if(v<0||v>1000)return false;
    horizontal_scale=v/100.0;
   }
   operands.clear();continue;
  }
  if(text&&(t.value=="Td"||t.value=="TD"||t.value=="Tm"||t.value=="T*")){
   pending_tj_adjustment=0;
   text_run_emitted=false;
  }
  if(text&&(t.value=="Td"||t.value=="TD")){
   if(operands.size()!=2||have_pending)return false;
   if(operands[1]!=0&&!suppressed)new_line_pending=true;
   if(t.value=="TD")text_leading=-operands[1];
   // Td/TD operate in the current text-line coordinate system.
   const double dx=operands[0],dy=operands[1];
   text_line_x+=line_axis_x*dx-line_axis_y*dy;
   text_line_y+=line_axis_y*dx+line_axis_x*dy;
   if(!std::isfinite(text_line_x)||!std::isfinite(text_line_y))return false;
   has_line_origin=true;
   operands.clear();continue;
  }
  if(text&&t.value=="Tm"){
   if(operands.size()!=6||have_pending)return false;
   const double next_x=operands[4],next_y=operands[5];
   const double old_length=std::hypot(line_axis_x,line_axis_y);
   const double next_length=std::hypot(operands[0],operands[1]);
   if(next_length<=1e-12||!std::isfinite(next_length)||
      old_length<=1e-12||!std::isfinite(old_length))return false;
   // A new Tm can also describe text along a rotated baseline.
   // Compare displacement normal to the prior baseline, not the page Y axis.
   const double displacement_normal=
      -(next_x-text_line_x)*line_axis_y/old_length+
       (next_y-text_line_y)*line_axis_x/old_length;
   if(has_line_origin&&std::abs(displacement_normal)>0.001&&!suppressed)
    new_line_pending=true;
   text_line_x=next_x;text_line_y=next_y;
   line_axis_x=operands[0]/next_length;
   line_axis_y=operands[1]/next_length;
   has_line_origin=true;
   operands.clear();continue;
  }
  if(t.value=="BT"){if(text||have_pending||!operands.empty())return false;
   text=true;operands.clear();has_line_origin=false;
   pending_tj_adjustment=0;text_run_emitted=false;
   text_line_x=0;text_line_y=0;line_axis_x=1;line_axis_y=0;continue;}
  if(t.value=="T*"&&text){
   if(have_pending||!operands.empty())return false;
   text_line_x+=line_axis_y*text_leading;
   text_line_y-=line_axis_x*text_leading;
   if(!std::isfinite(text_line_x)||!std::isfinite(text_line_y))return false;
   has_line_origin=true;
   if(!suppressed)new_line_pending=true;
   continue;
  }
  if(t.value=="ET"){if(!text||have_pending||!operands.empty())return false;
   text=false;pending_tj_adjustment=0;text_run_emitted=false;continue;}
  if(t.value=="EMC"){
   if(actual_stack.empty())return false;
   suppressed-=suppress_stack.back();
   suppress_stack.pop_back();actual_stack.pop_back();
   continue;
  }
  if(text&&(t.value=="Tj"||t.value=="'"||t.value==std::string(1,char(34)))){
   const bool quote=t.value=="'"||t.value==std::string(1,char(34));
   if(!have_pending||!fonts.count(selected)||
      (t.value=="Tj"&&!operands.empty()))return false;
   if(quote){
    if(t.value==std::string(1,char(34))){
     if(operands.size()!=2||std::abs(operands[0])>10000||
        std::abs(operands[1])>10000)return false;
     word_spacing=operands[0];
     character_spacing=operands[1];
     operands.clear();
    }else if(!operands.empty())return false;
    // Quote operators include T*: advance by the current text leading.
    text_line_x+=line_axis_y*text_leading;
    text_line_y-=line_axis_x*text_leading;
    if(!std::isfinite(text_line_x)||!std::isfinite(text_line_y))return false;
    has_line_origin=true;
    pending_tj_adjustment=0;
    text_run_emitted=false;
    if(!suppressed)new_line_pending=true;
   }
   if(!suppressed){
    if(!pending.empty()&&new_line_pending&&!output.empty()&&
       output.back()!='\n'){
     if(output.size()>=4*1024*1024)return false;
     output+='\n';
    }
    if(!pending.empty())new_line_pending=false;
    std::string decoded;
    if(!decode_run(pending,fonts.at(selected),decoded))return false;
    if(!decoded.empty()){
     const auto& active_font=fonts.at(selected);
     const auto mapped_space=active_font.unicode_sequences.find(CMapKey{1,0x20});
     const bool word_space=active_font.subtype!="Type0"&&
        mapped_space!=active_font.unicode_sequences.end()&&
        mapped_space->second==U" ";
     const double threshold=font_size>0?
        tj_space_threshold(active_font)+
        1000.0*(character_spacing+(word_space?word_spacing:0))/font_size:0;
     if(text_run_emitted&&font_size>0&&horizontal_scale>0&&
        std::isfinite(threshold)&&threshold>0&&
        pending_tj_adjustment<=-threshold&&!output.empty()&&
        output.back()!=' '&&output.back()!='\n'&&
        decoded.front()!=' '&&decoded.front()!='\n')output+=' ';
     if(decoded.size()>4*1024*1024-output.size())return false;
     output+=decoded;
     text_run_emitted=true;
     pending_tj_adjustment=0;
    }
   }
   pending.clear();have_pending=false;
  }
 }
 if(text||!actual_stack.empty()||have_pending)return false;
 utf8=std::move(output);return true;
}
}
