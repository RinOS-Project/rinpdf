#include "../src/renderer/rectangles.h"
#include "../src/parser/contents.h"
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

int main(){
 rinpdf::parser::PageForm form;
 form.bbox[0]=0;form.bbox[1]=0;form.bbox[2]=8;form.bbox[3]=8;
 form.isolated_transparency_group=true;
 const std::string content="q /Half gs 0 g 0 0 8 8 re f Q";
 form.content.assign(content.begin(),content.end());
 rinpdf::renderer::FormResources forms{{"Group",form}};
 rinpdf::parser::PageGraphicsState alpha;
 alpha.fill_alpha=0.5;alpha.has_fill_alpha=true;
 rinpdf::renderer::GraphicsStateResources states{{"Half",alpha}};
 const std::string page="0.5 g 0 0 8 8 re f /Group Do";
 std::vector<uint8_t> rgba(8*8*4);
 if(!rinpdf::renderer::render_rectangles_box_with_fonts(
      reinterpret_cast<const uint8_t*>(page.data()),page.size(),
      8,8,rgba.data(),32,{0,0,8,8},{},{},{},forms,true,0,states))
  return 1;
 // 50% black over gray (128) produces approximately 64.
 for(size_t i=0;i<rgba.size();i+=4)
  if(rgba[i]<63||rgba[i]>65||rgba[i]!=rgba[i+1]||
     rgba[i]!=rgba[i+2]||rgba[i+3]!=255)return 2;
 // An RGB source must produce the same recovered coverage in all
 // channels, rather than reconstructing independent per-channel alphas.
 const std::string colored="q /Half gs 1 0 0 rg 0 0 8 8 re f Q";
 form.content.assign(colored.begin(),colored.end());
 forms["Group"]=form;
 const std::string page_rgb="0.5 g 0 0 8 8 re f /Group Do";
 std::vector<uint8_t> rgb(8*8*4);
 if(!rinpdf::renderer::render_rectangles_box_with_fonts(
      reinterpret_cast<const uint8_t*>(page_rgb.data()),page_rgb.size(),
      8,8,rgb.data(),32,{0,0,8,8},{},{},{},forms,true,0,states))
  return 3;
 for(size_t i=0;i<rgb.size();i+=4)
  if(rgb[i]<190||rgb[i]>193||rgb[i+1]<63||rgb[i+1]>65||
     rgb[i+2]<63||rgb[i+2]>65||rgb[i+3]!=255)return 4;
 // Group primitives must accumulate alpha internally before the group
 // is composited over the destination: two 50% paints do not make 100%.
 const std::string overlapping=
   "q /Half gs 1 0 0 rg 0 0 8 8 re f "
   "0 0 1 rg 0 0 8 8 re f Q";
 form.content.assign(overlapping.begin(),overlapping.end());
 forms["Group"]=form;
 const std::string gray="0.5 g 0 0 8 8 re f /Group Do";
 std::vector<uint8_t> overlap_pixels(8*8*4);
 if(!rinpdf::renderer::render_rectangles_box_with_fonts(
      reinterpret_cast<const uint8_t*>(gray.data()),gray.size(),
      8,8,overlap_pixels.data(),32,{0,0,8,8},{},{},{},forms,true,0,states))
  return 5;
 for(size_t i=0;i<overlap_pixels.size();i+=4){
  // Premultiplied group: red ~64, blue ~128, alpha ~192.
  // Over opaque 128 gray: RGB should be approximately 96, 32, 160.
  if(overlap_pixels[i]<94||overlap_pixels[i]>98||
     overlap_pixels[i+1]<30||overlap_pixels[i+1]>34||
     overlap_pixels[i+2]<158||overlap_pixels[i+2]>162||
     overlap_pixels[i+3]!=255)return 6;
 }
 return 0;
}
