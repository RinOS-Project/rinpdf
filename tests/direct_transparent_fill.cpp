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
 // A horizontal line at 50% stroking opacity must affect only its
 // covered pixels, preserving transparent pixels elsewhere.
 rinpdf::renderer::PremultipliedSurface strokes;
 if(!strokes.resize(8,8))return 4;
 std::vector<uint8_t> scratch_stroke(8*8*4);
 rinpdf::parser::PageGraphicsState half_stroke;
 half_stroke.stroke_alpha=0.5;
 half_stroke.has_stroke_alpha=true;
 rinpdf::renderer::GraphicsStateResources stroke_states{{"HalfStroke",half_stroke}};
 const char line[]="/HalfStroke gs 1 0 0 RG 1 w 1 4 m 7 4 l S";
 if(!rinpdf::renderer::render_rectangles_box_with_fonts(
      reinterpret_cast<const uint8_t*>(line),sizeof(line)-1,
      8,8,scratch_stroke.data(),32,{0,0,8,8},{},{},{},{},
      false,0,stroke_states,&strokes))return 5;
 bool found=false;
 for(size_t i=0;i<strokes.pixels.size();i+=4){
  const unsigned alpha=strokes.pixels[i+3];
  if(alpha>0){
   found=true;
   if(strokes.pixels[i]!=alpha||
      strokes.pixels[i+1]!=0||strokes.pixels[i+2]!=0||
      alpha>129)return 6;
  }
 }
 if(!found||strokes.pixels[3]!=0)return 7;
 return 0;
}
