#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rinpdf::renderer {

// Internal RGBA surface with premultiplied color channels.
// Invariant: each color component is <= alpha.
struct PremultipliedSurface {
 unsigned width=0,height=0;
 std::vector<uint8_t> pixels;
 bool resize(unsigned w,unsigned h){
  if(!w||!h||size_t(w)*h>1048576u)return false;
  width=w;height=h;
  pixels.assign(size_t(w)*h*4,0);
  return true;
 }
 static unsigned mul255(unsigned a,unsigned b){
  return (a*b+127)/255;
 }
 // Source-over a solid fill at the specified 8-bit coverage.
 // Color is supplied unassociated; storage remains premultiplied.
 bool fill_pixel(unsigned x,unsigned y,uint8_t red,uint8_t green,
                 uint8_t blue,uint8_t coverage){
  if(x>=width||y>=height||
     pixels.size()!=size_t(width)*height*4)return false;
  const size_t index=(size_t(y)*width+x)*4;
  const unsigned remain=255u-coverage;
  const unsigned colors[3]={red,green,blue};
  for(unsigned c=0;c<3;++c)
   pixels[index+c]=uint8_t(std::min(255u,
      mul255(colors[c],coverage)+mul255(pixels[index+c],remain)));
  pixels[index+3]=uint8_t(std::min(255u,
      unsigned(coverage)+mul255(pixels[index+3],remain)));
  return true;
 }
 bool set_reconstructed(unsigned x,unsigned y,
                        const uint8_t* black,const uint8_t* white){
  if(x>=width||y>=height||!black||!white)return false;
  unsigned coverage[3]{};
  for(unsigned c=0;c<3;++c){
   const int estimate=255+int(black[c])-int(white[c]);
   coverage[c]=unsigned(std::clamp(estimate,0,255));
  }
  std::sort(coverage,coverage+3);
  // All three channels must describe the same coverage, modulo rounding.
  if(coverage[2]-coverage[0]>2)return false;
  const unsigned alpha=coverage[1];
  const size_t index=(size_t(y)*width+x)*4;
  for(unsigned c=0;c<3;++c)
   pixels[index+c]=uint8_t(std::min<unsigned>(black[c],alpha));
  pixels[index+3]=uint8_t(alpha);
  return true;
 }
 bool composite_over_opaque(uint8_t* dst,size_t stride) const {
  if(!dst||stride<size_t(width)*4||pixels.size()!=size_t(width)*height*4)
   return false;
  for(unsigned y=0;y<height;++y){
   for(unsigned x=0;x<width;++x){
    const size_t i=(size_t(y)*width+x)*4;
    uint8_t* d=dst+size_t(y)*stride+size_t(x)*4;
    const unsigned remaining=255u-pixels[i+3];
    for(unsigned c=0;c<3;++c)
     d[c]=uint8_t(std::min<unsigned>(255u,
                       unsigned(pixels[i+c])+mul255(d[c],remaining)));
    d[3]=255;
   }
  }
  return true;
 }
};
} // namespace rinpdf::renderer
