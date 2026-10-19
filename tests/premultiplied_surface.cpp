#include "../src/renderer/premultiplied_surface.h"
#include <cstdint>
#include <cstdio>

int main(){
 rinpdf::renderer::PremultipliedSurface surface;
 if(!surface.resize(2,1))return 1;
 const uint8_t black0[3]={128,0,0};
 const uint8_t white0[3]={255,127,127};
 const uint8_t black1[3]={0,0,0};
 const uint8_t white1[3]={255,255,255};
 if(!surface.set_reconstructed(0,0,black0,white0)||
    !surface.set_reconstructed(1,0,black1,white1))return 2;
 uint8_t output[8]={128,128,128,255,18,36,54,255};
 if(!surface.composite_over_opaque(output,8))return 3;
 if(output[0]<190||output[0]>193||
    output[1]<63||output[1]>65||
    output[2]<63||output[2]>65)return 4;
 if(output[4]!=18||output[5]!=36||output[6]!=54||
    output[7]!=255)return 5;
 // Inconsistent channel coverage is not a valid premultiplied pixel.
 const uint8_t broken_black[3]={0,0,0};
 const uint8_t broken_white[3]={255,100,255};
 if(surface.set_reconstructed(0,0,broken_black,broken_white))return 6;
 if(surface.resize(4096,4096))return 7;
 return 0;
}
