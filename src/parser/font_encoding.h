#pragma once
#include "value.h"
#include <cstdint>
#include <map>
#include <string>
#include <utility>
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
inline bool font_byte_unicode(const std::string& encoding,uint8_t byte,
 const std::map<uint8_t,uint32_t>& differences,uint32_t& unicode){
 const auto changed=differences.find(byte);
 if(changed!=differences.end()){unicode=changed->second;return true;}
 if(encoding=="WinAnsiEncoding"){
  static constexpr uint32_t special[32]={
   0x20ac,0x0000,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,0x02c6,0x2030,0x0160,0x2039,0x0152,0x0000,0x017d,0x0000,0x0000,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x02dc,0x2122,0x0161,0x203a,0x0153,0x0000,0x017e,0x0178
  };
  if(byte>=32&&byte<=126){unicode=byte;return true;}
  if(byte>=128&&byte<160){
   const uint32_t value=special[byte-128];if(!value)return false;
   unicode=value;return true;
  }
  if(byte>=160){unicode=byte;return true;}
  return false;
 }
 if(encoding=="MacRomanEncoding"){
  static constexpr uint32_t upper[128]={
   0x00c4,0x00c5,0x00c7,0x00c9,0x00d1,0x00d6,0x00dc,0x00e1,0x00e0,0x00e2,0x00e4,0x00e3,0x00e5,0x00e7,0x00e9,0x00e8,0x00ea,0x00eb,0x00ed,0x00ec,0x00ee,0x00ef,0x00f1,0x00f3,0x00f2,0x00f4,0x00f6,0x00f5,0x00fa,0x00f9,0x00fb,0x00fc,0x2020,0x00b0,0x00a2,0x00a3,0x00a7,0x2022,0x00b6,0x00df,0x00ae,0x00a9,0x2122,0x00b4,0x00a8,0x2260,0x00c6,0x00d8,0x221e,0x00b1,0x2264,0x2265,0x00a5,0x00b5,0x2202,0x2211,0x220f,0x03c0,0x222b,0x00aa,0x00ba,0x03a9,0x00e6,0x00f8,0x00bf,0x00a1,0x00ac,0x221a,0x0192,0x2248,0x2206,0x00ab,0x00bb,0x2026,0x00a0,0x00c0,0x00c3,0x00d5,0x0152,0x0153,0x2013,0x2014,0x201c,0x201d,0x2018,0x2019,0x00f7,0x25ca,0x00ff,0x0178,0x2044,0x20ac,0x2039,0x203a,0xfb01,0xfb02,0x2021,0x00b7,0x201a,0x201e,0x2030,0x00c2,0x00ca,0x00c1,0x00cb,0x00c8,0x00cd,0x00ce,0x00cf,0x00cc,0x00d3,0x00d4,0xf8ff,0x00d2,0x00da,0x00db,0x00d9,0x0131,0x02c6,0x02dc,0x00af,0x02d8,0x02d9,0x02da,0x00b8,0x02dd,0x02db,0x02c7
  };
  if(byte>=32&&byte<=126){unicode=byte;return true;}
  if(byte>=128){unicode=upper[byte-128];return true;}
  return false;
 }
 if(encoding=="StandardEncoding"){
  // StandardEncoding's glyph names differ at quote positions.
  if(byte>=32&&byte<=126){
   unicode=byte==39?0x2019:byte==96?0x2018:byte;
   return true;
  }
  return false; // Other StandardEncoding glyphs need an AGL mapping.
 }
 return false;
}

}
