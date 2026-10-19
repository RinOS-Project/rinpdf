#include "../src/renderer/rectangles.h"
#include <cstdint>
#include <string>
#include <vector>

int main(){
 using namespace rinpdf;
 renderer::PremultipliedSurface surface;
 if(!surface.resize(4,4))return 1;
 std::vector<uint8_t> scratch(4*4*4,0);
 parser::PageGraphicsState half;
 half.fill_alpha=0.5;half.has_fill_alpha=true;
 renderer::GraphicsStateResources states{{"Half",half}};
 parser::PageForm child;
 child.bbox[0]=0;child.bbox[1]=0;child.bbox[2]=4;child.bbox[3]=4;
 const std::string red="1 0 0 rg 0 0 4 4 re f";
 child.content.assign(red.begin(),red.end());
 parser::PageForm parent=child;
 parent.content.assign({'/','C','h','i','l','d',' ','D','o'});
 renderer::FormResources forms{{"Child",child},{"Parent",parent}};
 const std::string instructions="/Half gs /Parent Do";
 if(!renderer::render_rectangles_box_with_fonts(
    reinterpret_cast<const uint8_t*>(instructions.data()),instructions.size(),
    4,4,scratch.data(),16,{0,0,4,4},{},{},{},forms,false,0,states,&surface))
  return 2;
 for(size_t i=0;i<surface.pixels.size();i+=4)
  if(surface.pixels[i]<127||surface.pixels[i]>129||
     surface.pixels[i+1]!=0||surface.pixels[i+2]!=0||
     surface.pixels[i+3]<127||surface.pixels[i+3]>129)return 3;
 // An isolated child must source-over the transparent parent correctly.
 forms["Parent"].content.assign({'/','G','r','o','u','p',' ','D','o'});
 parser::PageForm group=child;
 group.isolated_transparency_group=true;
 group.content.assign(red.begin(),red.end());
 forms.emplace("Group",group);
 renderer::PremultipliedSurface isolated;
 if(!isolated.resize(4,4))return 4;
 if(!renderer::render_rectangles_box_with_fonts(
    reinterpret_cast<const uint8_t*>(instructions.data()),instructions.size(),
    4,4,scratch.data(),16,{0,0,4,4},{},{},{},forms,false,0,states,&isolated))
  return 5;
 for(size_t i=0;i<isolated.pixels.size();i+=4)
  if(isolated.pixels[i]<127||isolated.pixels[i]>129||
     isolated.pixels[i+3]<127||isolated.pixels[i+3]>129)return 6;
 return 0;
}
