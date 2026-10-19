#include "../src/renderer/rectangles.h"
#include <cstdint>
#include <string>
#include <vector>
int main(){
 using namespace rinpdf;
 parser::PageForm group;
 group.transparency_group=true;
 group.isolated_transparency_group=true;
 group.knockout_transparency_group=true;
 group.bbox[0]=0;group.bbox[1]=0;group.bbox[2]=8;group.bbox[3]=8;
 const std::string inside="/Half gs 1 0 0 rg 0 0 8 8 re f "
                          "0 0 1 rg 0 0 8 8 re f";
 group.content.assign(inside.begin(),inside.end());
 renderer::FormResources forms{{"Knockout",group}};
 parser::PageGraphicsState half;half.fill_alpha=0.5;half.has_fill_alpha=true;
 renderer::GraphicsStateResources states{{"Half",half}};
 const std::string page="/Knockout Do";
 std::vector<uint8_t> rgba(8*8*4);
 if(!renderer::render_rectangles_box_with_fonts(
      reinterpret_cast<const uint8_t*>(page.data()),page.size(),
      8,8,rgba.data(),32,{0,0,8,8},{},{},{},forms,true,0,states))
  return 1;
 // Later blue primitive replaces earlier red on knockout overlap;
 // half-blue over white is approximately (127,127,255).
 for(size_t i=0;i<rgba.size();i+=4)
  if(rgba[i]<126||rgba[i]>129||
     rgba[i+1]<126||rgba[i+1]>129||
     rgba[i+2]!=255||rgba[i+3]!=255)return 2;
 return 0;
}
