#include "../src/renderer/rectangles.h"
#include <cstring>
#include <vector>
int main(){
 std::vector<unsigned char> pixels(8*8*4);
 const char content[]="0 g 2 2 4 4 re f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(content),sizeof(content)-1,8,8,pixels.data(),8*4))return 1;
 auto pixel=[&](unsigned x,unsigned y){return pixels[(y*8+x)*4];};
 if(pixel(0,0)!=255||pixel(3,3)!=0||pixel(7,7)!=255)return 2;
 const char unsupported[]="BT /F1 12 Tf ET";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(unsupported),sizeof(unsupported)-1,8,8,pixels.data(),32))return 3;
 const char rgb[]="q 1 0 0 rg 1 1 2 2 re f Q 0 g 4 4 2 2 re f";
 if(!rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(rgb),sizeof(rgb)-1,8,8,pixels.data(),32))return 4;
 auto channel=[&](unsigned x,unsigned y,unsigned k){return pixels[(y*8+x)*4+k];};
 if(channel(1,5,0)!=255||channel(1,5,1)!=0||channel(1,5,2)!=0)return 5;
 if(channel(4,2,0)!=0||channel(4,2,1)!=0||channel(4,2,2)!=0)return 6;
 const char invalid[]="Q";
 if(rinpdf::renderer::render_rectangles(reinterpret_cast<const unsigned char*>(invalid),sizeof(invalid)-1,8,8,pixels.data(),32))return 7;
 return 0;
}
