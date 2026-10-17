#pragma once
#include "value.h"
#include <cstdint>
#include <map>
#include <string>
namespace rinpdf::parser {
// Strict subset of Adobe glyph names; unknown names fail closed.
inline bool glyph_name_unicode(const std::string& name,uint32_t& code){
 if(name.size()==1){
  unsigned char c=static_cast<unsigned char>(name[0]);
  if(c>=33&&c<=126){code=c;return true;}
 }
 static const std::map<std::string,uint32_t> names={
  {"space",32},{"exclam",33},{"quotedbl",34},{"numbersign",35},
  {"dollar",36},{"percent",37},{"ampersand",38},{"quotesingle",39},
  {"parenleft",40},{"parenright",41},{"asterisk",42},{"plus",43},
  {"comma",44},{"hyphen",45},{"period",46},{"slash",47},
  {"colon",58},{"semicolon",59},{"less",60},{"equal",61},
  {"greater",62},{"question",63},{"at",64},{"bracketleft",91},
  {"backslash",92},{"bracketright",93},{"asciicircum",94},
  {"underscore",95},{"grave",96},{"braceleft",123},{"bar",124},
  {"braceright",125},{"asciitilde",126},{"zero",48},{"one",49},
  {"two",50},{"three",51},{"four",52},{"five",53},{"six",54},
  {"seven",55},{"eight",56},{"nine",57},{"Euro",0x20ac},
  {"Aacute",0xc1},{"eacute",0xe9},{"adieresis",0xe4},
  {"aring",0xe5},{"ntilde",0xf1},{"copyright",0xa9}
 };
 auto it=names.find(name);
 if(it==names.end())return false;
 code=it->second;return true;
}
inline bool decode_font_differences(const Value& array,std::map<uint8_t,uint32_t>& result){
 if(array.kind!=Value::Kind::Array||array.array.empty()||
    array.array.size()>512)return false;
 unsigned cursor=256;
 std::map<uint8_t,uint32_t> decoded;
 for(const auto& v:array.array){
  if(v.kind==Value::Kind::Integer){
   if(v.integer<0||v.integer>255)return false;
   cursor=static_cast<unsigned>(v.integer);
  }else if(v.kind==Value::Kind::Name){
   if(cursor>=256)return false;
   uint32_t unicode=0;
   if(!glyph_name_unicode(v.text,unicode))return false;
   if(!decoded.emplace(static_cast<uint8_t>(cursor),unicode).second)return false;
   ++cursor;
  }else return false;
 }
 result=std::move(decoded);return true;
}
}
