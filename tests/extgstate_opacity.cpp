#include "../src/renderer/rectangles.h"
#include <cstdint>
#include <cstdio>
#include <vector>

int main(){
 rinpdf::renderer::GraphicsStateResources states;
 rinpdf::parser::PageGraphicsState translucent;
 translucent.fill_alpha=0.5;translucent.has_fill_alpha=true;
 states.emplace("Half",translucent);
 std::vector<uint8_t> rgba(8*8*4,0);
 const char content[]="q /Half gs 0 g 0 0 8 8 re f Q 0 g 0 0 4 4 re f";
 if(!rinpdf::renderer::render_rectangles_box_with_fonts(
      reinterpret_cast<const uint8_t*>(content),sizeof(content)-1,
      8,8,rgba.data(),32,{0,0,8,8},{},{},{},{},true,0,states))
  return 1;
 // Top left is covered by the restored opaque black rectangle;
 // bottom right is painted with 50% opacity over white.
 const auto value=[&](unsigned x,unsigned y){
  return rgba[(size_t(y)*8+x)*4];
 };
 // Opaque black should cover the bottom-left quarter after Q restores
 // normal opacity. The other regions remain half black over white.
 if(value(1,6)!=0||value(6,1)!=127||value(6,6)!=127)return 2;
 return 0;
}
