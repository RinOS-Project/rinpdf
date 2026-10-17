#include "../src/parser/cid_font.h"
#include <cstdint>
using namespace rinpdf::parser;
int main(){
 CIDGlyphMap identity;
 uint16_t gid=0;
 if(!cid_to_gid(identity,42,gid)||gid!=42)return 1;
 CIDGlyphMap explicit_map{false,{0,0,0,7,1,0}};
 if(!cid_map_valid(explicit_map))return 2;
 if(!cid_to_gid(explicit_map,1,gid)||gid!=7)return 3;
 if(!cid_to_gid(explicit_map,2,gid)||gid!=256)return 4;
 if(cid_to_gid(explicit_map,3,gid))return 5;
 if(gid!=256)return 6;
 CIDGlyphMap odd{false,{0,2,3}};
 if(cid_map_valid(odd))return 7;
 CIDGlyphMap invalid_identity{true,{0,1}};
 if(cid_map_valid(invalid_identity))return 8;
 auto integer=[](long long v){
  Value x;x.kind=Value::Kind::Integer;x.integer=v;return x;
 };
 Value widths;widths.kind=Value::Kind::Array;
 Value listed;listed.kind=Value::Kind::Array;
 listed.array={integer(350),integer(425)};
 widths.array={integer(10),listed,integer(20),integer(22),integer(600)};
 CIDWidths parsed;
 const Value default_width=integer(900);
 if(!parse_cid_widths(&default_width,&widths,parsed))return 9;
 double width=0;
 if(!cid_width(parsed,10,width)||width!=350)return 10;
 if(!cid_width(parsed,11,width)||width!=425)return 11;
 if(!cid_width(parsed,21,width)||width!=600)return 12;
 if(!cid_width(parsed,99,width)||width!=900)return 13;
 Value invalid=widths;
 invalid.array.push_back(integer(20));
 invalid.array.push_back(integer(22));
 invalid.array.push_back(integer(100));
 if(parse_cid_widths(&default_width,&invalid,parsed))return 14;
 if(!cid_width(parsed,21,width)||width!=600)return 15;
 Value decimal;decimal.kind=Value::Kind::Real;decimal.real=412.5;
 Value ranges;ranges.kind=Value::Kind::Array;
 Value items;items.kind=Value::Kind::Array;
 items.array={decimal};
 ranges.array={integer(30),items,integer(40),integer(41),decimal};
 if(!parse_cid_widths(&decimal,&ranges,parsed))return 16;
 if(!cid_width(parsed,30,width)||width!=412.5)return 17;
 if(!cid_width(parsed,41,width)||width!=412.5)return 18;
 if(!cid_width(parsed,99,width)||width!=412.5)return 19;
 decimal.real=-0.1;
 if(parse_cid_widths(&decimal,&ranges,parsed))return 20;
 return 0;
}
