#include "../src/renderer/truetype_draw.h"
#include <vector>
#include <cstdint>
#include <cmath>
using namespace rinpdf::renderer;
static void w16(std::vector<uint8_t>& a,size_t p,uint16_t v){a[p]=v>>8;a[p+1]=v;}
static void w32(std::vector<uint8_t>& a,size_t p,uint32_t v){
 a[p]=v>>24;a[p+1]=v>>16;a[p+2]=v>>8;a[p+3]=v;
}
int main(){
 // A tiny synthetic TrueType font: cmap U+0041 -> glyph 1, a square.
 std::vector<uint8_t> f(308,0);
 w32(f,0,0x00010000);w16(f,4,7);
 const uint32_t tags[]={0x68656164,0x68686561,0x686d7478,0x6d617870,
                        0x6c6f6361,0x676c7966,0x636d6170};
 const uint32_t off[]={124,178,214,222,228,234,268};
 const uint32_t len[]={54,36,8,6,6,34,40};
 for(size_t i=0;i<7;++i){
  size_t p=12+i*16;w32(f,p,tags[i]);w32(f,p+8,off[i]);w32(f,p+12,len[i]);
 }
 w16(f,124+18,1000);w16(f,124+50,0);
 w16(f,178+34,2);
 w16(f,214,500);w16(f,218,700);
 w32(f,222,0x00010000);w16(f,226,2);
 w16(f,228,0);w16(f,230,0);w16(f,232,17);
 w16(f,234,1);w16(f,236,0);w16(f,238,0);
 w16(f,240,500);w16(f,242,500);
 w16(f,244,3);w16(f,246,0);
 for(size_t i=0;i<4;++i)f[248+i]=1;
 w16(f,252,0);w16(f,254,500);w16(f,256,0);w16(f,258,uint16_t(-500));
 w16(f,260,0);w16(f,262,0);w16(f,264,500);w16(f,266,0);
 w16(f,268,0);w16(f,270,1);
 w16(f,272,3);w16(f,274,10);w32(f,276,12);
 w16(f,280,12);w32(f,284,28);w32(f,292,1);
 w32(f,296,65);w32(f,300,65);w32(f,304,1);
 std::vector<uint8_t> rgba(64*64*4,255);
 double next=0;
 if(!render_embedded_truetype_runs(f,{{false,"A",0}},16,8,8,
       64,64,rgba.data(),64*4,0,64,1,1,0,0,0,next))return 1;
 if(std::abs(next-19.2)>0.001)return 2;
 bool ink=false;
 for(size_t i=0;i<rgba.size();i+=4)if(rgba[i]==0){ink=true;break;}
 if(!ink)return 3;
 double untouched=next;
 if(render_embedded_truetype_runs(f,{{false,"Z",0}},16,8,8,
       64,64,rgba.data(),64*4,0,64,1,1,0,0,0,next))return 4;
 if(next!=untouched)return 5;
 std::vector<uint8_t> all_clipped(64*64,0);
 std::vector<uint8_t> no_ink(64*64*4,255);
 double clipped_next=0;
 if(!render_embedded_truetype_runs(f,{{false,"A",0}},16,8,8,
       64,64,no_ink.data(),64*4,0,64,1,1,0,0,0,clipped_next,
       &all_clipped,0,0,64,64))return 6;
 for(size_t i=0;i<no_ink.size();i+=4)if(no_ink[i]!=255)return 7;
 if(std::abs(clipped_next-19.2)>0.001)return 8;
 // Non-uniform scaling is valid even without a font transform.
 std::vector<uint8_t> stretched(64*64*4,255);
 double stretched_next=0;
 if(!render_embedded_truetype_runs(f,{{false,"A",0}},16,8,8,
       64,64,stretched.data(),64*4,0,64,1,2,0,0,0,stretched_next))
   return 9;
 bool stretched_ink=false;
 for(size_t i=0;i<stretched.size();i+=4)if(stretched[i]<255){stretched_ink=true;break;}
 if(!stretched_ink)return 10;
 // CTM translation, nontrivial text basis and shear must paint
 // valid pixels using the same embedded outline.
 std::vector<uint8_t> translated(64*64*4,255);
 double moved=0;
 if(!render_embedded_truetype_runs(f,{{false,"A",0}},16,8,8,
       64,64,translated.data(),64*4,0,64,1,1,0,0,0,moved,
       nullptr,0,0,64,64,{1,0.25,0.4,1,12,3,0,1,-1,0}))return 11;
 bool transformed_ink=false;
 for(size_t i=0;i<translated.size();i+=4)
  if(translated[i]<255){transformed_ink=true;break;}
 if(!transformed_ink)return 12;
 // Reject non-finite matrices without changing the output position.
 const double previous=moved;
 if(render_embedded_truetype_runs(f,{{false,"A",0}},16,8,8,
       64,64,translated.data(),64*4,0,64,1,1,0,0,0,moved,
       nullptr,0,0,64,64,{1,0,0,1e300,0,0,1,0,0,1}))return 13;
 if(moved!=previous)return 14;
 // PDF Tc/Tw/Tz affect advances; Tz also scales glyph outlines.
 double spaced=0;
 std::vector<uint8_t> spaced_pixels(64*64*4,255);
 if(!render_embedded_truetype_runs(f,{{false,"A",0}},16,8,8,
       64,64,spaced_pixels.data(),64*4,0,64,1,1,0,0,0,spaced,
       nullptr,0,0,64,64,{1,0,0,1,0,0,1,0,0,1,2,0,0.5}))return 15;
 if(std::abs(spaced-14.6)>0.001)return 16;
 double adjusted=0;
 if(!render_embedded_truetype_runs(f,{{true,{},-250}},16,8,8,
       64,64,spaced_pixels.data(),64*4,0,64,1,1,0,0,0,adjusted,
       nullptr,0,0,64,64,{1,0,0,1,0,0,1,0,0,1,0,0,0.5}))return 17;
 if(std::abs(adjusted-10)>0.001)return 18;
 // Identity-H uses two bytes per CID; CID 1 selects glyph 1.
 const parser::CIDGlyphMap identity;
 const std::string cid_one({char(0),char(1)});
 std::vector<uint8_t> cid_pixels(64*64*4,255);
 double cid_next=0;
 if(!render_embedded_truetype_runs(f,{{false,cid_one,0}},16,8,8,
       64,64,cid_pixels.data(),64*4,0,64,1,1,0,0,0,cid_next,
       nullptr,0,0,64,64,{},nullptr,"Identity-H",&identity))return 19;
 if(std::abs(cid_next-19.2)>0.001)return 20;
 bool cid_ink=false;
 for(size_t i=0;i<cid_pixels.size();i+=4)if(cid_pixels[i]<255)cid_ink=true;
 if(!cid_ink)return 21;
 double before=cid_next;
 if(render_embedded_truetype_runs(f,{{false,std::string(1,'A'),0}},16,8,8,
       64,64,cid_pixels.data(),64*4,0,64,1,1,0,0,0,cid_next,
       nullptr,0,0,64,64,{},nullptr,"Identity-H",&identity))return 22;
 if(cid_next!=before)return 23;
 const parser::CIDGlyphMap mapped{false,{0,0,0,1}};
 if(!render_embedded_truetype_runs(f,{{false,cid_one,0}},16,8,8,
       64,64,cid_pixels.data(),64*4,0,64,1,1,0,0,0,cid_next,
       nullptr,0,0,64,64,{},nullptr,"Identity-H",&mapped))return 24;
 return 0;
}
