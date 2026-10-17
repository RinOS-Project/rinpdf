#include "../src/renderer/sfnt.h"
#include <vector>
#include <cstdint>
using namespace rinpdf::renderer;
static void be16(std::vector<uint8_t>& b,size_t i,uint16_t v){
 b[i]=uint8_t(v>>8);b[i+1]=uint8_t(v);
}
static void be32(std::vector<uint8_t>& b,size_t i,uint32_t v){
 b[i]=uint8_t(v>>24);b[i+1]=uint8_t(v>>16);b[i+2]=uint8_t(v>>8);b[i+3]=uint8_t(v);
}
int main(){
 std::vector<uint8_t> f(192,0);
 be32(f,0,0x00010000);be16(f,4,4);
 // 4 directory entries: head, hhea, hmtx, maxp.
 const uint32_t tags[]={0x68656164,0x68686561,0x686d7478,0x6d617870};
 const uint32_t offs[]={76,130,166,180};
 const uint32_t lens[]={54,36,14,6};
 for(size_t i=0;i<4;++i){
  const size_t p=12+16*i;
  be32(f,p,tags[i]);be32(f,p+8,offs[i]);be32(f,p+12,lens[i]);
 }
 be16(f,76+18,1000);
 be16(f,130+34,2);
 be16(f,166,500);be16(f,170,700);
 be16(f,174,0);be16(f,176,0); // bearings for glyphs 2 and 3
 be32(f,180,0x00010000);be16(f,184,4);
 SfntFont font;
 if(!parse_sfnt(f.data(),f.size(),font))return 1;
 uint16_t units=0,advance=0;
 if(!sfnt_horizontal_metrics(font,0,units,advance)||units!=1000||advance!=500)return 2;
 if(!sfnt_horizontal_metrics(font,1,units,advance)||advance!=700)return 3;
 if(!sfnt_horizontal_metrics(font,3,units,advance)||advance!=700)return 4;
 if(sfnt_horizontal_metrics(font,4,units,advance))return 5;
 auto invalid=f;be16(invalid,130+34,5);
 SfntFont broken;
 if(!parse_sfnt(invalid.data(),invalid.size(),broken))return 6;
 if(sfnt_horizontal_metrics(broken,1,units,advance))return 7;
 return 0;
}
