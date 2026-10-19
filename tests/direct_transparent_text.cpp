#include "../src/renderer/rectangles.h"
#include <cstdint>
#include <vector>

int main(){
 using namespace rinpdf;
 renderer::FontResources fonts{{"FD",{"RinPDF-Diagnostic","Type1"}}};
 parser::PageGraphicsState half;
 half.fill_alpha=0.5;half.has_fill_alpha=true;
 renderer::GraphicsStateResources states{{"Half",half}};
 renderer::PremultipliedSurface surface;
 if(!surface.resize(32,16))return 1;
 std::vector<uint8_t> scratch(32*16*4,0);
 const char content[]="/Half gs 0 g BT /FD 10 Tf 1 0 0 1 2 10 Tm (HI) Tj ET";
 if(!renderer::render_rectangles_box_with_fonts(
      reinterpret_cast<const uint8_t*>(content),sizeof(content)-1,
      32,16,scratch.data(),32*4,{0,0,32,16},fonts,{},{},{},
      false,0,states,&surface))return 2;
 bool ink=false;
 for(size_t i=0;i<surface.pixels.size();i+=4){
  const unsigned alpha=surface.pixels[i+3];
  if(alpha){
   ink=true;
   if(alpha>129||surface.pixels[i]!=0||
      surface.pixels[i+1]!=0||surface.pixels[i+2]!=0)return 3;
  }
 }
 if(!ink)return 4;
 return 0;
}
