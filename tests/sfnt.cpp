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
 // Minimal simple glyf triangle with short loca offsets.
 std::vector<uint8_t> t(160,0);
 put32(t,0,0x00010000);put16(t,4,4);
 // Directory ends at 76; head=76, maxp=130, loca=136, glyf=142.
 put32(t,12,0x68656164);put32(t,20,76);put32(t,24,54);
 put32(t,28,0x6d617870);put32(t,36,130);put32(t,40,6);
 put32(t,44,0x6c6f6361);put32(t,52,136);put32(t,56,6);
 put32(t,60,0x676c7966);put32(t,68,142);put32(t,72,18);
 put16(t,126,0); // indexToLocFormat short
 put32(t,130,0x00010000);put16(t,134,2);
 put16(t,136,0);put16(t,138,0);put16(t,140,9);
 // Glyph 1: 1 contour, last point index 0, single point at (0,0).
 put16(t,142,1);put16(t,144,0);put16(t,146,0);
 put16(t,148,0);put16(t,150,0);
 put16(t,152,0);put16(t,154,0);
 t[156]=0x31; // on-curve, x/y coordinate same
 SfntFont test_font;
 if(!parse_sfnt(t.data(),t.size(),test_font))return 8;
 SimpleGlyph shape;
 if(!sfnt_simple_glyph(test_font,1,shape))return 9;
 if(shape.contour_ends.size()!=1||shape.points.size()!=1||
    !shape.points[0].on_curve||shape.points[0].x!=0||
    shape.points[0].y!=0)return 10;
 if(!sfnt_simple_glyph(test_font,0,shape)||!shape.points.empty())return 11;
 // Unsupported glyph IDs must not modify the output.
 SimpleGlyph unchanged=shape;
 if(sfnt_glyph_outline(test_font,2,shape))return 14;
 if(shape.points.size()!=unchanged.points.size())return 15;
 auto damaged=t;put16(damaged,140,100);
 if(!parse_sfnt(damaged.data(),damaged.size(),test_font))return 12;
 if(sfnt_simple_glyph(test_font,1,shape))return 13;
 return 0;
}
