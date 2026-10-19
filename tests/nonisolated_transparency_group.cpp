#include "../src/renderer/rectangles.h"
#include <cstdint>
#include <string>
#include <vector>
int main(){
 using namespace rinpdf;
 parser::PageForm group;
 group.transparency_group=true;
 group.bbox[0]=0;group.bbox[1]=0;group.bbox[2]=8;group.bbox[3]=8;
 const std::string inside="q /Half gs 1 0 0 rg 0 0 8 8 re f Q";
 group.content.assign(inside.begin(),inside.end());
 renderer::FormResources forms{{"NonIsolated",group}};
 parser::PageGraphicsState half;
 half.fill_alpha=0.5;half.has_fill_alpha=true;
 renderer::GraphicsStateResources states{{"Half",half}};
 const std::string stream="0.5 g 0 0 8 8 re f /NonIsolated Do";
 std::vector<uint8_t> opaque(8*8*4);
 if(!renderer::render_rectangles_box_with_fonts(
    reinterpret_cast<const uint8_t*>(stream.data()),stream.size(),
    8,8,opaque.data(),32,{0,0,8,8},{},{},{},forms,true,0,states))
  return 1;
 for(size_t i=0;i<opaque.size();i+=4)
  if(opaque[i]<190||opaque[i]>193||
     opaque[i+1]<63||opaque[i+1]>65||
     opaque[i+2]<63||opaque[i+2]>65||opaque[i+3]!=255)return 2;
 renderer::PremultipliedSurface transparent;
 if(!transparent.resize(8,8))return 3;
 std::vector<uint8_t> scratch(8*8*4);
 if(renderer::render_rectangles_box_with_fonts(
    reinterpret_cast<const uint8_t*>(stream.data()),stream.size(),
    8,8,scratch.data(),32,{0,0,8,8},{},{},{},forms,false,0,
    states,&transparent))return 4;
 return 0;
}
