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
 // Semi-transparent destination: blend modes must use unassociated
 // backdrop color for B(), then return premultiplied output.
 for(unsigned mode:{0u,1u,2u}){
  rinpdf::renderer::PremultipliedSurface layered;
  if(!layered.resize(1,1))return 8;
  if(!layered.blend_pixel(0,0,255,0,0,128,0))return 9;
  if(!layered.blend_pixel(0,0,0,0,255,128,mode))return 10;
  const auto& p=layered.pixels;
  if(p[3]<190||p[3]>193)return 11;
  if(mode==0&&!(p[0]>=62&&p[0]<=65&&p[2]>=127&&p[2]<=129))return 12;
  if(mode==1&&!(p[0]>=62&&p[0]<=65&&p[2]>=62&&p[2]<=65))return 13;
  if(mode==2&&!(p[0]>=127&&p[0]<=129&&p[2]>=127&&p[2]<=129))return 14;
  if(p[0]>p[3]||p[1]>p[3]||p[2]>p[3])return 15;
 }
 // Coverage/shape differs from opacity: an invisible paint can
 // contribute shape without changing premultiplied color alpha.
 rinpdf::renderer::PremultipliedSurface shapes,child;
 if(!shapes.resize(1,1)||!child.resize(1,1))return 16;
 if(!child.mark_shape(0,0,255)||!child.fill_pixel(0,0,255,0,0,0))
  return 17;
 if(child.shape[0]!=255||child.pixels[3]!=0)return 18;
 if(!child.composite_over(shapes)||shapes.shape[0]!=255||
    shapes.pixels[3]!=0)return 19;
 // Half-pixel shape coverage removes only half of earlier color,
 // independent of the second object's opacity.
 rinpdf::renderer::PremultipliedSurface edge;
 if(!edge.resize(1,1))return 20;
 edge.knockout=true;
 if(!edge.blend_pixel(0,0,255,0,0,255,0,255))return 21;
 if(!edge.blend_pixel(0,0,0,0,255,128,0,128))return 22;
 const auto& ep=edge.pixels;
 if(ep[0]<126||ep[0]>129||ep[2]<63||ep[2]>65||
    ep[3]<190||ep[3]>193)return 23;
 return 0;
}
