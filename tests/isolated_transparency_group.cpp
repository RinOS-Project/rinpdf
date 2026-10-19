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
 // Unsupported group definitions must never be silently rendered.
 return 0;
}
