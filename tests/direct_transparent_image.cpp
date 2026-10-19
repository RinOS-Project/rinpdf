#include "../src/renderer/rectangles.h"
#include <cstdint>
#include <vector>

int main(){
 using namespace rinpdf;
 renderer::PremultipliedSurface surface;
 if(!surface.resize(2,1))return 1;
 parser::PageImage image;
 image.width=2;image.height=1;image.channels=3;
 image.pixels={255,0,0,0,0,255};
 image.alpha={128,255};
 renderer::ImageResources images{{"Im",image}};
 parser::PageGraphicsState half;
 half.fill_alpha=0.5;half.has_fill_alpha=true;
 renderer::GraphicsStateResources states{{"Half",half}};
 // Scale the image's unit square to two page pixels.
 const char stream[]="/Half gs q 2 0 0 1 0 0 cm /Im Do Q";
 std::vector<uint8_t> scratch(2*4,0);
 if(!renderer::render_rectangles_box_with_fonts(
      reinterpret_cast<const uint8_t*>(stream),sizeof(stream)-1,
      2,1,scratch.data(),8,{0,0,2,1},{},{},images,{},false,0,
      states,&surface))return 2;
 const auto& p=surface.pixels;
 // Alpha is the image's SMask multiplied by the fill opacity.
 // Red pixel: 128*128/255=64. Blue pixel: 255*128/255=128.
 if(p.size()!=8||p[0]!=64||p[1]!=0||p[2]!=0||p[3]!=64||
    p[4]!=0||p[5]!=0||p[6]!=128||p[7]!=128)return 3;
 return 0;
}
