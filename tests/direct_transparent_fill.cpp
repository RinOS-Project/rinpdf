#include "../src/renderer/rectangles.h"
#include <cstdint>
#include <cstdio>
#include <vector>

int main(){
 rinpdf::renderer::PremultipliedSurface surface;
 if(!surface.resize(4,4))return 1;
 std::vector<uint8_t> scratch(4*4*4,0);
 rinpdf::renderer::GraphicsStateResources gs;
 rinpdf::parser::PageGraphicsState half;
 half.fill_alpha=0.5;half.has_fill_alpha=true;
 gs.emplace("Half",half);
 const char path[]="/Half gs 1 0 0 rg 0 0 4 4 re f 0 0 1 rg 0 0 4 4 re f";
 if(!rinpdf::renderer::render_rectangles_box_with_fonts(
   reinterpret_cast<const uint8_t*>(path),sizeof(path)-1,
   4,4,scratch.data(),16,{0,0,4,4},{},{},{},{},false,0,gs,&surface))
  return 2;
 for(size_t i=0;i<surface.pixels.size();i+=4){
  const auto& px=surface.pixels;
  // 50% red over transparent followed by 50% blue:
  // approximately 25% red, 50% blue, 75% alpha.
  if(px[i]<62||px[i]>65||px[i+1]!=0||
     px[i+2]<127||px[i+2]>129||
     px[i+3]<190||px[i+3]>193)return 3;
 }
 return 0;
}
