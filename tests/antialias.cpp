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
 return 0;
}
