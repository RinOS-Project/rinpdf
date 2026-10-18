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
// Identity-H uses two-byte CID codes and PDF descendant font widths.
// Do not infer advances for other Type0 encodings or odd-length runs.
bool cid_text_advance(const std::string& bytes,const PageFont& font,
 double font_size,double tc,double hz,double& advance){
 if(font.subtype!="Type0"||font.encoding!="Identity-H"||
    bytes.size()%2||font_size<=0||!std::isfinite(font_size)||
    !std::isfinite(tc)||!std::isfinite(hz)||hz<0)return false;
 double units=0;
 for(size_t i=0;i<bytes.size();i+=2){
  const uint16_t cid=(uint16_t(uint8_t(bytes[i]))<<8)|
                     uint8_t(bytes[i+1]);
  double width=0;
  if(!cid_width(font.cid_widths,cid,width))return false;
  // PDF Tw applies to single-byte 0x20, never two-byte Identity-H codes.
  units+=width*font_size/1000.0+tc;
  if(!std::isfinite(units)||std::abs(units)>1e12)return false;
 }
 advance=units*hz;
 return std::isfinite(advance)&&std::abs(advance)<=1e12;
}
// Identity-V advances along the vertical writing axis using W2/DW2.
// Return a positive downward distance; v1x/v1y locate the glyph origin
// and must not be mistaken for an additional text advance.
bool cid_vertical_advance(const std::string& bytes,const PageFont& font,
 double font_size,double tc,double& advance){
 if(font.subtype!="Type0"||font.encoding!="Identity-V"||
    bytes.size()%2||font_size<=0||!std::isfinite(font_size)||
    !std::isfinite(tc))return false;
 double total=0;
 for(size_t i=0;i<bytes.size();i+=2){
  const uint16_t cid=(uint16_t(uint8_t(bytes[i]))<<8)|uint8_t(bytes[i+1]);
  CIDVerticalMetric metrics;
  if(!cid_vertical_metric(font.vertical_metrics,font.cid_widths,cid,metrics)||
     !std::isfinite(metrics.w1y)||!std::isfinite(metrics.v1x)||
     !std::isfinite(metrics.v1y))return false;
  total+=(-metrics.w1y)*font_size/1000.0+tc;
  if(!std::isfinite(total)||std::abs(total)>1e12)return false;
 }
 advance=total;
 return true;
}
bool text_advance(const std::string& bytes,const PageFont& font,
 double font_size,double tc,double tw,double hz,double& advance);
// Calculate advances only when explicit PDF widths are available.
// The Unicode replacement string may have a different length than glyph codes.
bool simple_text_advance(const std::string& bytes,const PageFont& font,
 double font_size,double tc,double tw,double hz,double& advance){
 if(font.subtype=="Type0"||font_size<=0||!std::isfinite(font_size))return false;
 double units=0;
 for(unsigned char code:bytes){
  const auto it=font.simple_widths.find(code);
  const double width=it!=font.simple_widths.end()?it->second:font.missing_width;
  if(!std::isfinite(width)||width<0||
     (it==font.simple_widths.end()&&width<=0))return false;
  units+=width*font_size/1000.0+tc+(code==0x20?tw:0);
  if(!std::isfinite(units)||std::abs(units)>1e12)return false;
 }
 advance=units*hz;
 return std::isfinite(advance)&&std::abs(advance)<=1e12;
}
bool text_advance(const std::string& bytes,const PageFont& font,
 double font_size,double tc,double tw,double hz,double& advance){
 if(font.subtype=="Type0")
  return cid_text_advance(bytes,font,font_size,tc,hz,advance);
 return simple_text_advance(bytes,font,font_size,tc,tw,hz,advance);
}
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
   // /DW applies when /W does not override the mapped space CID.
   double width=0;
   // Use the actual PDF CID space width. Narrow spaces must not be
   // inflated to 250 units, nor wide ones truncated to 1000 units.
   if(cid_width(font.cid_widths,uint16_t(key.code),width)&&
      std::isfinite(width)&&width>0)
    return width;
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
// Shared spacing threshold for both Tj and TJ text-show paths.
double text_gap_threshold(const PageFont& font,double font_size,
 double character_spacing,double word_spacing){
 const auto mapped=font.unicode_sequences.find(CMapKey{1,0x20});
 const bool single_byte_space=font.subtype!="Type0"&&
   mapped!=font.unicode_sequences.end()&&mapped->second==U" ";
 const double spacing=character_spacing+
   (single_byte_space?word_spacing:0);
 return font_size>0?
   tj_space_threshold(font)+1000.0*spacing/font_size:
   tj_space_threshold(font);
}
// Shared geometric spacing check for Td/TD and Tm. The previous glyph
// advance and both text origins are expressed in the same text matrix.
bool has_position_gap(double old_x,double old_y,double next_x,double next_y,
 double a,double b,double cursor,double threshold_units,
 double font_size,double horizontal_scale){
 const double length=std::hypot(a,b);
 if(!std::isfinite(length)||length<=1e-12||
    !std::isfinite(cursor)||!std::isfinite(threshold_units)||
    threshold_units<=0||font_size<=0||horizontal_scale<=0)return false;
 const double dx=next_x-old_x-a*cursor;
 const double dy=next_y-old_y-b*cursor;
 const double along=(dx*a+dy*b)/length;
 const double across=(dx*b-dy*a)/length;
 const double threshold=font_size*horizontal_scale*length*
                        threshold_units/1000.0;
 return std::isfinite(along)&&std::isfinite(across)&&
        std::isfinite(threshold)&&threshold>0&&
        std::abs(across)<=0.001&&along>=threshold;
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
 double text_cursor=0;
 bool text_cursor_known=true;
 double vertical_cursor=0;
 bool vertical_cursor_known=true;
 bool text_run_emitted=false;
 bool object_glyph_emitted=false;
 double horizontal_scale=1;
 std::vector<double> operands;
 double text_line_x=0,text_line_y=0;
 double line_axis_x=1,line_axis_y=0;
 double line_a=1,line_b=0,line_c=0,line_d=1;
 bool has_line_origin=false;
 // Preserve the last rendered endpoint across BT/ET boundaries.
 double previous_end_x=0,previous_end_y=0;
 double previous_a=1,previous_b=0;
 bool previous_end_valid=false;
 bool cross_object_pending=false;
 unsigned suppressed=0;
 size_t tokens=0;
 // Keep UTF-8 decoding and glyph advance updates identical for Tj and TJ.
 // Centralize output bounds and avoid publishing a partial result.
 // ActualText replacements obey the same 4 MiB output budget,
 // including a newline inferred before the replacement.
 const auto append_actual_text=[&](const std::string& replacement)->bool{
  const bool add_newline=new_line_pending&&!output.empty()&&
      output.back()!='\n';
  const size_t remaining=4*1024*1024-output.size();
  if(replacement.size()>remaining||
      (add_newline&&replacement.size()==remaining))return false;
  if(add_newline)output+='\n';
  new_line_pending=false;
  output+=replacement;
  return true;
 };
 const auto append_extracted=[&](const std::string& decoded,
     bool infer_space)->bool{
  const bool add_space=infer_space&&!output.empty()&&
    output.back()!=' '&&output.back()!='\n'&&!decoded.empty()&&
    decoded.front()!=' '&&decoded.front()!='\n';
  const size_t remaining=4*1024*1024-output.size();
  if(decoded.size()>remaining|| (add_space&&decoded.size()==remaining))
   return false;
  if(add_space)output+=' ';
  output+=decoded;
  return true;
 };
 const auto decode_and_advance=[&](const std::string& bytes,
     const PageFont& font,std::string& decoded,bool& was_known){
  was_known=text_cursor_known;
  if(!decode_run(bytes,font,decoded))return false;
  if(decoded.empty())return true;
  if(font.subtype=="Type0"&&font.encoding=="Identity-V"){
   double vertical_advance=0;
   if(vertical_cursor_known&&cid_vertical_advance(bytes,font,font_size,
       character_spacing,vertical_advance)){
    vertical_cursor+=vertical_advance;
    if(!std::isfinite(vertical_cursor)||
       std::abs(vertical_cursor)>1e12)return false;
   }else vertical_cursor_known=false;
   // Vertical glyph placement is not a horizontal advance.
   text_cursor_known=false;
   return true;
  }
  double advance=0;
  if(text_cursor_known&&text_advance(bytes,font,font_size,
      character_spacing,word_spacing,horizontal_scale,advance)){
   text_cursor+=advance;
   if(!std::isfinite(text_cursor)||std::abs(text_cursor)>1e12)return false;
  }else text_cursor_known=false;
  return true;
 };
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
       if(!append_actual_text(replacement))return false;
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
       if(!append_actual_text(replacement))return false;
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
        tf.value=="Tf"){
     // An unresolved font must not silently reuse the preceding Tf state.
     if(!fonts.count(t.value)||have_pending||!operands.empty())return false;
     const char* begin=amount.value.data();
     const char* end=begin+amount.value.size();
     if(begin!=end&&*begin=='+')++begin;
     double parsed_size=0;
     const auto parsed=std::from_chars(begin,end,parsed_size,
                                      std::chars_format::fixed);
     if(parsed.ec!=std::errc()||parsed.ptr!=end||
        !std::isfinite(parsed_size)||parsed_size<=0||
        parsed_size>10000)return false;
     // A trailing TJ displacement is measured using the text state at
     // the adjustment, not the font size selected for the next show.
     // Convert its 1/1000-em representation before changing Tf.
     if(pending_tj_adjustment!=0&&font_size>0){
      const double converted=pending_tj_adjustment*font_size/parsed_size;
      if(!std::isfinite(converted)||std::abs(converted)>1e12)return false;
      pending_tj_adjustment=converted;
     }
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
    const auto& active_font=fonts.at(selected);
    const double gap_threshold=text_gap_threshold(active_font,font_size,
                                                  character_spacing,word_spacing);
    // Compare physical advances when a font width and text cursor are
    // available; retain the PDF-unit fallback for unsupported metrics.
    const double physical_space=font_size*horizontal_scale*
                               gap_threshold/1000.0;
    for(const auto& run:runs){
     if(run.adjustment){
      net_adjustment+=run.displacement;
      if(active_font.subtype=="Type0"&&active_font.encoding=="Identity-V"&&
         vertical_cursor_known){
       // TJ changes the vertical text position, not the horizontal cursor.
       vertical_cursor-=run.displacement*font_size/1000.0;
       if(!std::isfinite(vertical_cursor)||
          std::abs(vertical_cursor)>1e12)return false;
      }
      if(text_cursor_known){
       text_cursor-=run.displacement*font_size*horizontal_scale/1000.0;
       if(!std::isfinite(text_cursor)||std::abs(text_cursor)>1e12)return false;
      }
      if(!std::isfinite(net_adjustment))return false;
      continue;
     }
     if(run.bytes.empty())continue;
     std::string decoded;
     bool gap_cursor_known=false;
     if(!decode_and_advance(run.bytes,active_font,decoded,
                            gap_cursor_known))return false;
     if(decoded.empty())continue;
     const double physical_gap=-net_adjustment*font_size*
                               horizontal_scale/1000.0;
     const bool gap_is_space=gap_cursor_known?
       (std::isfinite(physical_space)&&physical_space>0&&
        std::isfinite(physical_gap)&&physical_gap>=physical_space):
       (std::isfinite(gap_threshold)&&gap_threshold>0&&
        net_adjustment<=-gap_threshold);
     if(!append_extracted(decoded,
          !(active_font.subtype=="Type0"&&active_font.encoding=="Identity-V")&&
          emitted_run&&font_size>0&&
                            horizontal_scale>0&&gap_is_space))return false;
     emitted_run=true;
     text_run_emitted=true;
     object_glyph_emitted=true;
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
  // TJ is consumed together with its preceding array, while Tf is consumed
  // with a font name and size. A bare operator must never be ignored.
  if(text&&(t.value=="TJ"||t.value=="Tf"))return false;
  // Rendering mode and rise affect glyph painting/placement, not the
  // Unicode sequence itself. Validate their operands during extraction.
  if(text&&(t.value=="Tr"||t.value=="Ts")){
   if(have_pending||operands.size()!=1)return false;
   const double v=operands.front();
   if(t.value=="Tr"){
    if(v<0||v>7||std::trunc(v)!=v)return false;
   }else if(std::abs(v)>10000)return false;
   operands.clear();continue;
  }
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
    const double next_scale=v/100.0;
    // A trailing TJ gap was applied at the previous horizontal scale.
    // Keep its physical displacement when changing Tz. Zero scaling
    // cannot represent a finite gap in em units, so discard inference.
    if(pending_tj_adjustment!=0){
     if(next_scale==0||horizontal_scale==0)
      pending_tj_adjustment=0;
     else{
      const double adjusted=pending_tj_adjustment*
                            horizontal_scale/next_scale;
      if(!std::isfinite(adjusted)||std::abs(adjusted)>1e12)return false;
      pending_tj_adjustment=adjusted;
     }
    }
    horizontal_scale=next_scale;
   }
   operands.clear();continue;
  }
  if(text&&(t.value=="Td"||t.value=="TD"||t.value=="Tm"||t.value=="T*")){
   pending_tj_adjustment=0;
  }
  if(text&&(t.value=="Td"||t.value=="TD")){
   if(operands.size()!=2||have_pending)return false;
   // Compare the new line origin against the previous glyph endpoint.
   if(!suppressed&&operands[1]==0&&has_line_origin&&
      text_run_emitted&&text_cursor_known&&!new_line_pending&&
      !output.empty()&&output.back()!=' '&&output.back()!='\n'){
    const auto font_it=fonts.find(selected);
    const double next_x=text_line_x+line_a*operands[0]+line_c*operands[1];
    const double next_y=text_line_y+line_b*operands[0]+line_d*operands[1];
    if(font_it!=fonts.end()&&has_position_gap(text_line_x,text_line_y,
       next_x,next_y,line_a,line_b,text_cursor,
       tj_space_threshold(font_it->second),font_size,horizontal_scale)){
     if(output.size()>=4*1024*1024)return false;
     output+=' ';
    }
   }
   if(!suppressed){
    const auto selected_font=fonts.find(selected);
    const bool vertical=selected_font!=fonts.end()&&
      selected_font->second.subtype=="Type0"&&
      selected_font->second.encoding=="Identity-V";
    if(vertical){
     // Td translations along the vertical writing axis remain in-column.
     // Only a displacement perpendicular to (c,d) starts another column.
     const double axis_length=std::hypot(line_c,line_d);
     if(!std::isfinite(axis_length)||axis_length<=1e-12)return false;
     const double move_x=line_a*operands[0]+line_c*operands[1];
     const double move_y=line_b*operands[0]+line_d*operands[1];
     const double across=(move_x*line_d-move_y*line_c)/axis_length;
     if(!std::isfinite(across))return false;
     if(text_run_emitted&&std::abs(across)>
        std::max(0.001,font_size*axis_length*0.15))
      new_line_pending=true;
    }else if(operands[1]!=0)new_line_pending=true;
   }
   text_cursor=0;text_cursor_known=true;
   vertical_cursor=0;vertical_cursor_known=true;text_run_emitted=false;
   if(t.value=="TD")text_leading=-operands[1];
   // Td/TD operate in the current text-line coordinate system.
   const double dx=operands[0],dy=operands[1];
   if(std::abs(dx)>1e12||std::abs(dy)>1e12)return false;
   text_line_x+=line_a*dx+line_c*dy;
   text_line_y+=line_b*dx+line_d*dy;
   if(!std::isfinite(text_line_x)||!std::isfinite(text_line_y)||
      std::abs(text_line_x)>1e12||std::abs(text_line_y)>1e12)return false;
   has_line_origin=true;
   operands.clear();continue;
  }
  if(text&&t.value=="Tm"){
   if(operands.size()!=6||have_pending)return false;
   const double next_x=operands[4],next_y=operands[5];
   // Bound the complete matrix, including shear and translation, before
   // propagating it to Td/T*/quote line positions.
   for(const double component:operands)
    if(!std::isfinite(component)||std::abs(component)>1e12)return false;
   const double old_length=std::hypot(line_axis_x,line_axis_y);
   const double next_length=std::hypot(operands[0],operands[1]);
   if(next_length<=1e-12||!std::isfinite(next_length)||
      old_length<=1e-12||!std::isfinite(old_length))return false;
   // A new Tm can also describe text along a rotated baseline.
   // Compare displacement normal to the prior baseline, not the page Y axis.
   const double displacement_normal=
      -(next_x-text_line_x)*line_axis_y/old_length+
       (next_y-text_line_y)*line_axis_x/old_length;
   // Matrix setup alone does not emit glyphs. Do not infer a line break
   // from successive Tm operators before any text has been shown.
   if(has_line_origin&&text_run_emitted&&!suppressed){
    const auto font_it=fonts.find(selected);
    const bool vertical=font_it!=fonts.end()&&
      font_it->second.subtype=="Type0"&&
      font_it->second.encoding=="Identity-V";
    if(vertical){
     // Identity-V writes along the negative second column of Tm.
     // A displacement perpendicular to that axis begins another column.
     const double axis_length=std::hypot(line_c,line_d);
     if(!std::isfinite(axis_length)||axis_length<=1e-12)return false;
     const double dx=next_x-text_line_x;
     const double dy=next_y-text_line_y;
     const double cross=(dx*line_d-dy*line_c)/axis_length;
     const double tolerance=std::max(0.001,font_size*axis_length*0.15);
     if(!std::isfinite(cross)||!std::isfinite(tolerance))return false;
     if(std::abs(cross)>tolerance)new_line_pending=true;
    }else if(std::abs(displacement_normal)>0.001)
     new_line_pending=true;
   }
   if(!suppressed&&has_line_origin&&text_run_emitted&&text_cursor_known&&
      !new_line_pending&&operands[0]==line_a&&operands[1]==line_b&&
      operands[2]==line_c&&operands[3]==line_d&&
      !output.empty()&&output.back()!=' '&&output.back()!='\n'){
    const auto font_it=fonts.find(selected);
    if(font_it!=fonts.end()&&has_position_gap(text_line_x,text_line_y,
       next_x,next_y,line_a,line_b,text_cursor,
       tj_space_threshold(font_it->second),font_size,horizontal_scale)){
     if(output.size()>=4*1024*1024)return false;
     output+=' ';
    }
   }
   // A new text object begins with an identity text matrix, so its
   // first Tm must be compared with the preceding object's endpoint.
   // Use the previous baseline normal to distinguish a new line from
   // a horizontal word gap.
   if(cross_object_pending&&previous_end_valid&&!suppressed&&
      operands[0]==previous_a&&operands[1]==previous_b){
    const double axis_length=std::hypot(previous_a,previous_b);
    if(axis_length>1e-12&&std::isfinite(axis_length)){
     const double dx=next_x-previous_end_x;
     const double dy=next_y-previous_end_y;
     const double perpendicular=(dy*previous_a-dx*previous_b)/axis_length;
     // Ignore sub-glyph baseline jitter; a fixed 0.001 unit tolerance
     // incorrectly split text objects with tiny positioning differences.
     const double tolerance=std::max(0.001,font_size*axis_length*0.15);
     if(std::isfinite(perpendicular)&&std::isfinite(tolerance)&&
        std::abs(perpendicular)>tolerance)new_line_pending=true;
    }
   }
   if(cross_object_pending&&previous_end_valid&&!suppressed&&
      !new_line_pending&&!output.empty()&&output.back()!=' '&&
      output.back()!='\n'&&
      operands[0]==previous_a&&operands[1]==previous_b){
    const auto font_it=fonts.find(selected);
    if(font_it!=fonts.end()&&has_position_gap(previous_end_x,previous_end_y,
       next_x,next_y,previous_a,previous_b,0,
       tj_space_threshold(font_it->second),font_size,horizontal_scale)){
     if(output.size()>=4*1024*1024)return false;
     output+=' ';
    }
   }
   cross_object_pending=false;
   text_cursor=0;text_cursor_known=true;
   vertical_cursor=0;vertical_cursor_known=true;text_run_emitted=false;
   text_line_x=next_x;text_line_y=next_y;
   line_axis_x=operands[0]/next_length;
   line_axis_y=operands[1]/next_length;
   line_a=operands[0];line_b=operands[1];
   line_c=operands[2];line_d=operands[3];
   has_line_origin=true;
   operands.clear();continue;
  }
  if(t.value=="BT"){if(text||have_pending||!operands.empty())return false;
   text=true;object_glyph_emitted=false;operands.clear();has_line_origin=false;
   pending_tj_adjustment=0;text_run_emitted=false;
   text_cursor=0;text_cursor_known=true;
   vertical_cursor=0;vertical_cursor_known=true;
   text_line_x=0;text_line_y=0;line_axis_x=1;line_axis_y=0;
   line_a=1;line_b=0;line_c=0;line_d=1;
   cross_object_pending=previous_end_valid;continue;}
  if(t.value=="T*"&&text){
   if(have_pending||!operands.empty())return false;
   // Move to the next line's origin, not the prior line's glyph cursor.
   text_cursor=0;text_cursor_known=true;
   vertical_cursor=0;vertical_cursor_known=true;text_run_emitted=false;
   text_line_x-=line_c*text_leading;
   text_line_y-=line_d*text_leading;
   if(!std::isfinite(text_line_x)||!std::isfinite(text_line_y)||
      std::abs(text_line_x)>1e12||std::abs(text_line_y)>1e12)return false;
   has_line_origin=true;
   if(!suppressed)new_line_pending=true;
   continue;
  }
  if(t.value=="ET"){if(!text||have_pending||!operands.empty())return false;
   if(object_glyph_emitted){
    if(text_run_emitted&&has_line_origin&&text_cursor_known){
     previous_end_x=text_line_x+line_a*text_cursor;
     previous_end_y=text_line_y+line_b*text_cursor;
     previous_a=line_a;previous_b=line_b;
     previous_end_valid=std::isfinite(previous_end_x)&&
       std::isfinite(previous_end_y);
    }else{
     // A displayed run with unknown metrics invalidates the old endpoint.
     // Only truly empty text objects may preserve that prior position.
     previous_end_valid=false;
    }
   }
   // Empty text objects emit no glyphs and must not erase the last
   // usable endpoint from an earlier text object.
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
    text_line_x-=line_c*text_leading;
    text_line_y-=line_d*text_leading;
    if(!std::isfinite(text_line_x)||!std::isfinite(text_line_y)||
      std::abs(text_line_x)>1e12||std::abs(text_line_y)>1e12)return false;
    has_line_origin=true;
    pending_tj_adjustment=0;
    text_run_emitted=false;
    text_cursor=0;text_cursor_known=true;
   vertical_cursor=0;vertical_cursor_known=true;
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
    bool prior_cursor_known=false;
    if(!decode_and_advance(pending,fonts.at(selected),decoded,
                           prior_cursor_known))return false;
    if(!decoded.empty()){
     const auto& active_font=fonts.at(selected);
     const double threshold=text_gap_threshold(active_font,font_size,
                                               character_spacing,word_spacing);
     if(!append_extracted(decoded,
          !(active_font.subtype=="Type0"&&active_font.encoding=="Identity-V")&&
          text_run_emitted&&font_size>0&&
          horizontal_scale>0&&std::isfinite(threshold)&&threshold>0&&
          pending_tj_adjustment<=-threshold))return false;
     text_run_emitted=true;
     object_glyph_emitted=true;
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
