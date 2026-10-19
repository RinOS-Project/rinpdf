#include "../src/parser/ccitt_image.h"
#include <cstdint>
#include <cstdio>
#include <vector>

// Two 16x2 binary fax strips encoded with libtiff. The synthetic image
// contains 8 white and 8 black pixels on each row, in opposite orders.
// These fixture strips were independently checked through TIFF decoding.
static constexpr uint8_t group3[]={0x00,0x19,0x8a,0x00,0x26,0xa2,0xcc};
static constexpr uint8_t group4[]={0x33,0x14,0x9a,0x8b,0x00,0x10,0x01};

static bool check(const uint8_t* bytes,size_t length,int k){
 std::vector<uint8_t> normal,inverted;
 if(!rinpdf::parser::decode_ccitt_image(
       bytes,length,16,2,k,false,true,normal) ||
    !rinpdf::parser::decode_ccitt_image(
       bytes,length,16,2,k,false,false,inverted)){
  std::fprintf(stderr,"CCITT Group %d failed to decode valid strip\n",k==-1?4:3);
  return false;
 }
 if(normal.size()!=32||inverted.size()!=32)return false;
 // Each row changes exactly at its midpoint; the second is reversed.
 for(unsigned y=0;y<2;++y){
  for(unsigned x=0;x<16;++x){
   const size_t p=y*16+x;
   const uint8_t expected=((x<8)==(y==0))?255:0;
   if(normal[p]!=expected||inverted[p]!=255-expected){
    std::fprintf(stderr,"CCITT pixel mismatch at %u,%u\n",x,y);
    return false;
   }
  }
 }
 return true;
}
int main(){
 return check(group3,sizeof(group3),0) &&
        check(group4,sizeof(group4),-1)?0:1;
}
