#include "../src/renderer/sfnt.h"
#include <vector>
#include <cstdint>
using namespace rinpdf::renderer;
static void put16(std::vector<uint8_t>& b,size_t p,uint16_t v){
 b[p]=uint8_t(v>>8);b[p+1]=uint8_t(v);
}
static void put32(std::vector<uint8_t>& b,size_t p,uint32_t v){
 b[p]=uint8_t(v>>24);b[p+1]=uint8_t(v>>16);
 b[p+2]=uint8_t(v>>8);b[p+3]=uint8_t(v);
}
int main(){
 // Minimal sfnt directory: maxp and a format-12 cmap.
 std::vector<uint8_t> b(96,0);
 put32(b,0,0x00010000);put16(b,4,2);
 put32(b,12,0x6d617870);put32(b,20,44);put32(b,24,6);
 put32(b,28,0x636d6170);put32(b,36,52);put32(b,40,44);
 put32(b,44,0x00010000);put16(b,48,4);
 put16(b,52,0);put16(b,54,1);
 put16(b,56,3);put16(b,58,10);put32(b,60,12);
 put16(b,64,12);put32(b,68,28);put32(b,76,1);
 put32(b,80,65);put32(b,84,66);put32(b,88,1);
 SfntFont font;
 if(!parse_sfnt(b.data(),b.size(),font))return 1;
 uint16_t glyph=0;
 if(!sfnt_glyph_id(font,65,glyph)||glyph!=1)return 2;
 if(!sfnt_glyph_id(font,66,glyph)||glyph!=2)return 3;
 if(sfnt_glyph_id(font,67,glyph))return 4;
 auto corrupt=b;
 put32(corrupt,36,1000);
 if(parse_sfnt(corrupt.data(),corrupt.size(),font))return 5;
 if(font.glyph_count!=4)return 6; // failure preserves previous result
 corrupt=b;put16(corrupt,4,257);
 if(parse_sfnt(corrupt.data(),corrupt.size(),font))return 7;
 return 0;
}
