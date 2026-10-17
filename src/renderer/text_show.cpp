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
bool courier_text_advance(const std::string& font,
 const std::vector<TextRun>& runs,double size,
 double char_spacing,double word_spacing,double horizontal_scale,
 double& advance){
 if(font!="Courier"&&font!="Courier-Bold"&&
    font!="Courier-Oblique"&&font!="Courier-BoldOblique")return false;
 if(!std::isfinite(size)||size<=0||size>10000||
    !std::isfinite(char_spacing)||std::abs(char_spacing)>10000||
    !std::isfinite(word_spacing)||std::abs(word_spacing)>10000||
    !std::isfinite(horizontal_scale)||horizontal_scale<0||
    horizontal_scale>10||runs.size()>4096)return false;
 double total=0;
 size_t count=0;
 for(const auto& run:runs){
  if(run.adjustment){
   if(!std::isfinite(run.displacement)||
      std::abs(run.displacement)>10000000)return false;
   total-=run.displacement*size/1000;
  }else{
   if(run.bytes.size()>1048576-count)return false;
   count+=run.bytes.size();
   for(unsigned char c:run.bytes){
    // This path supports printable ASCII in standard single-byte encoding.
    if(c<32||c>126)return false;
    total+=size*0.6+char_spacing+(c==32?word_spacing:0);
   }
  }
  if(!std::isfinite(total)||std::abs(total)>1e12)return false;
 }
 const double result=total*horizontal_scale;
 if(!std::isfinite(result)||std::abs(result)>1e12)return false;
 advance=result;return true;
}

bool advance_text_origin(double advance,double axis_x,double axis_y,
 double& origin_x,double& origin_y){
 if(!std::isfinite(advance)||!std::isfinite(axis_x)||
    !std::isfinite(axis_y)||!std::isfinite(origin_x)||
    !std::isfinite(origin_y))return false;
 const double next_x=origin_x+advance*axis_x;
 const double next_y=origin_y+advance*axis_y;
 if(!std::isfinite(next_x)||!std::isfinite(next_y)||
    std::abs(next_x)>1e12||std::abs(next_y)>1e12)return false;
 origin_x=next_x;origin_y=next_y;
 return true;
}

}
