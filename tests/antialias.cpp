#include "../src/renderer/rectangles.h"
#include <cstdint>
#include <cstring>
int main(){
 uint8_t pixels[2*2*4]{};
 const char stream[]="0 g 0 0 1.5 2 re f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const uint8_t*>(stream),
 sizeof(stream)-1,2,2,pixels,8))return 1;
 auto at=[&](unsigned x,unsigned y,unsigned channel){return pixels[(y*2+x)*4+channel];};
 if(at(0,0,0)!=0||at(0,1,0)!=0)return 2;
 if(at(1,0,0)!=127||at(1,1,0)!=127)return 3;
 if(at(1,0,3)!=255)return 4;
 const char red[]="1 0 0 rg 0 0 1.5 2 re f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const uint8_t*>(red),
 sizeof(red)-1,2,2,pixels,8))return 5;
 if(at(1,0,0)!=255||at(1,0,1)!=127||at(1,0,2)!=127)return 6;
 uint8_t stroke_pixels[4*4*4]{};
 const char stroke[]="0 G 0.5 w 0 J 0 2 m 4 2 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const uint8_t*>(stroke),
 sizeof(stroke)-1,4,4,stroke_pixels,16))return 7;
 unsigned shade=stroke_pixels[(1*4+2)*4];
 if(shade==0||shade==255)return 8;
 if(stroke_pixels[(0*4+2)*4]!=255)return 9;
 uint8_t joined[4*4*4]{};
 const char overlapped[]="0 G 0.5 w 0 J 0 2 m 4 2 l 0 2 m 4 2 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const uint8_t*>(overlapped),
 sizeof(overlapped)-1,4,4,joined,16))return 10;
 for(unsigned i=0;i<sizeof(joined);++i)if(joined[i]!=stroke_pixels[i])return 11;
 uint8_t clipped[4*4*4]{};
 const char clip[]="q 0 0 m 2.5 0 l 2.5 4 l 0 4 l h W n 0 g 0 0 4 4 re f Q";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const uint8_t*>(clip),
 sizeof(clip)-1,4,4,clipped,16))return 12;
 auto cp=[&](unsigned x,unsigned y){return clipped[(y*4+x)*4];};
 if(cp(1,1)!=0||cp(2,1)!=127||cp(3,1)!=255)return 13;
 uint8_t rect_clip_pixels[4*4*4]{};
 const char rect_clip[]="q 0 0 2.5 4 re W n 0 g 0 0 4 4 re f Q";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const uint8_t*>(rect_clip),
 sizeof(rect_clip)-1,4,4,rect_clip_pixels,16))return 14;
 for(unsigned i=0;i<sizeof(clipped);++i)if(rect_clip_pixels[i]!=clipped[i])return 15;
 uint8_t default_cap[4*4*4]{};
 uint8_t explicit_cap[4*4*4]{};
 const char implicit_butt[]="0 G 1 w 1 2 m 3 2 l S";
 const char explicit_butt[]="0 J 0 G 1 w 1 2 m 3 2 l S";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const uint8_t*>(implicit_butt),
 sizeof(implicit_butt)-1,4,4,default_cap,16))return 16;
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const uint8_t*>(explicit_butt),
 sizeof(explicit_butt)-1,4,4,explicit_cap,16))return 17;
 for(unsigned i=0;i<sizeof(default_cap);++i)if(default_cap[i]!=explicit_cap[i])return 18;
 return 0;
}
