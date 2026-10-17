#include "../src/renderer/sfnt.h"
#include <vector>
#include <cstdint>
using namespace rinpdf::renderer;
static void w16(std::vector<uint8_t>& b,size_t p,uint16_t v){b[p]=v>>8;b[p+1]=uint8_t(v);}
static void w32(std::vector<uint8_t>& b,size_t p,uint32_t v){b[p]=v>>24;b[p+1]=v>>16;b[p+2]=v>>8;b[p+3]=v;}
int main(){
 std::vector<uint8_t> bytes(180,0);
 w32(bytes,0,0x00010000);w16(bytes,4,4);
 const uint32_t tags[]={0x68656164,0x6d617870,0x6c6f6361,0x676c7966};
 const uint32_t offs[]={76,130,136,144};
 const uint32_t lens[]={54,6,8,36};
 for(size_t i=0;i<4;++i){
  size_t p=12+i*16;w32(bytes,p,tags[i]);w32(bytes,p+8,offs[i]);w32(bytes,p+12,lens[i]);
 }
 w16(bytes,126,0); // short loca
 w32(bytes,130,0x00010000);w16(bytes,134,3);
 w16(bytes,136,0);w16(bytes,138,0);w16(bytes,140,9);w16(bytes,142,18);
 // Glyph 1: one contour with one on-curve point (0,0).
 w16(bytes,144,1);w16(bytes,154,0);w16(bytes,156,0);bytes[158]=0x31;
 // Glyph 2: one translation-only component referring to glyph 1.
 w16(bytes,162,0xffff); // composite
 w16(bytes,172,0x0002); // ARGS_ARE_XY_VALUES, byte args
 w16(bytes,174,1);
 bytes[176]=5;bytes[177]=7;
 SfntFont font;
 if(!parse_sfnt(bytes.data(),bytes.size(),font))return 1;
 SimpleGlyph outline;
 if(!sfnt_glyph_outline(font,2,outline))return 2;
 if(outline.points.size()!=1||outline.points[0].x!=5||
    outline.points[0].y!=7||outline.contour_ends.size()!=1)return 3;
 auto unsupported=bytes;
 w16(unsupported,172,0x1802); // conflicting scaled/unscaled offset flags
 SfntFont broken;
 if(!parse_sfnt(unsupported.data(),unsupported.size(),broken))return 4;
 if(sfnt_glyph_outline(broken,2,outline))return 5;
 if(outline.points.size()!=1||outline.points[0].x!=5)return 6;
 // F2Dot14 0.5x scale with an explicit two-byte scale value.
 auto scaled=bytes;
 scaled.resize(182,0);
 w32(scaled,72,38); // glyf has two more scale bytes
 w16(scaled,142,19); // loca[3] in two-byte units
 w16(scaled,172,0x000a); // ARGS_ARE_XY_VALUES | WE_HAVE_A_SCALE
 w16(scaled,178,0x2000); // 0.5 as signed 2.14
 if(!parse_sfnt(scaled.data(),scaled.size(),broken))return 9;
 if(!sfnt_glyph_outline(broken,2,outline))return 10;
 if(outline.points.size()!=1||outline.points[0].x!=5||
    outline.points[0].y!=7)return 11;
 // Two components: second uses parent point 0 and child point 0.
 auto matched=bytes;
 matched.resize(188,0);
 w32(matched,72,44); // longer glyf table
 w16(matched,142,22); // glyph 2 occupies 26 bytes
 w16(matched,172,0x0022); // MORE_COMPONENTS + XY byte arguments
 w16(matched,178,0x0000); // second component: point-matching byte indices
 w16(matched,180,1);      // glyph 1
 matched[182]=0;matched[183]=0; // parent point 0, child point 0
 if(!parse_sfnt(matched.data(),matched.size(),broken))return 12;
 if(!sfnt_glyph_outline(broken,2,outline))return 13;
 if(outline.points.size()!=2||outline.points[0].x!=5||
    outline.points[1].x!=5||outline.points[1].y!=7)return 14;
 auto bad_match=matched;
 bad_match[182]=5; // parent point index out of range
 if(!parse_sfnt(bad_match.data(),bad_match.size(),broken))return 15;
 if(sfnt_glyph_outline(broken,2,outline))return 16;
 if(outline.points.size()!=2)return 17;
 auto self=bytes;w16(self,174,2);
 if(!parse_sfnt(self.data(),self.size(),broken))return 7;
 if(sfnt_glyph_outline(broken,2,outline))return 8;
 return 0;
}
