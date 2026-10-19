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
 // The parent starts 50% gray on transparency, then paints 50% red
 // through the non-isolated Form without a second group compositing pass.
 const std::string translucent="/Half gs 0.5 g 0 0 8 8 re f /NonIsolated Do";
 if(!renderer::render_rectangles_box_with_fonts(
    reinterpret_cast<const uint8_t*>(translucent.data()),translucent.size(),
    8,8,scratch.data(),32,{0,0,8,8},{},{},{},forms,false,0,
    states,&transparent))return 4;
 for(size_t i=0;i<transparent.pixels.size();i+=4)
  if(transparent.pixels[i]<158||transparent.pixels[i]>162||
     transparent.pixels[i+1]<30||transparent.pixels[i+1]>34||
     transparent.pixels[i+2]<30||transparent.pixels[i+2]>34||
     transparent.pixels[i+3]<190||transparent.pixels[i+3]>193)return 5;
 // Non-isolated groups use the live backdrop for internal blend modes.
 parser::PageGraphicsState mul,scr;
 mul.has_blend_mode=true;mul.blend_mode="Multiply";
 scr.has_blend_mode=true;scr.blend_mode="Screen";
 states.emplace("Mul",mul);states.emplace("Scr",scr);
 for(const auto& item:std::vector<std::pair<std::string,unsigned>>{
     {"Mul",64u},{"Scr",192u}}){
  const std::string body="/"+item.first+" gs 0.5 g 0 0 8 8 re f";
  group.content.assign(body.begin(),body.end());
  forms["NonIsolated"]=group;
  const std::string page="0.5 g 0 0 8 8 re f /NonIsolated Do";
  std::vector<uint8_t> frame(8*8*4);
  if(!renderer::render_rectangles_box_with_fonts(
    reinterpret_cast<const uint8_t*>(page.data()),page.size(),
    8,8,frame.data(),32,{0,0,8,8},{},{},{},forms,true,0,states))
   return 6;
  for(size_t i=0;i<frame.size();i+=4)
   if(frame[i]<item.second-2||frame[i]>item.second+2||
      frame[i+1]!=frame[i]||frame[i+2]!=frame[i]||
      frame[i+3]!=255)return 7;
 }
 return 0;
}
