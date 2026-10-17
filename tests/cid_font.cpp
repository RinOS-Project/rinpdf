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
 return 0;
}
