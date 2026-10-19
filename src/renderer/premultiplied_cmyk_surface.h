#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rinpdf::renderer {

// DeviceCMYK group workspace: four premultiplied ink channels plus alpha.
// This stores native group components; RGB conversion happens only at the
// input/output boundary. It does not implement ICC color management.
struct PremultipliedCMYKSurface {
 unsigned width=0,height=0;
 std::vector<uint8_t> ink; // C, M, Y, K, alpha (5 bytes per pixel)
 std::vector<uint8_t> shape;
 static unsigned mul255(unsigned a,unsigned b){return (a*b+127u)/255u;}
 bool resize(unsigned w,unsigned h){
  if(!w||!h||size_t(w)*h>1048576u)return false;
  width=w;height=h;
  ink.assign(size_t(w)*h*5,0);
  shape.assign(size_t(w)*h,0);
  return true;
 }
 static void from_rgb(uint8_t r,uint8_t g,uint8_t b,unsigned out[4]){
  const unsigned c=255u-r,m=255u-g,y=255u-b;
  const unsigned k=std::min(c,std::min(m,y));
  out[0]=c-k;out[1]=m-k;out[2]=y-k;out[3]=k;
 }
 bool paint_rgb(unsigned x,unsigned y,uint8_t r,uint8_t g,uint8_t b,
                uint8_t alpha,unsigned mode=0,uint8_t geometry=255){
  if(mode>2||x>=width||y>=height||
     ink.size()!=size_t(width)*height*5)return false;
  const size_t p=size_t(y)*width+x, i=p*5;
  unsigned src[4]{};
  from_rgb(r,g,b,src);
  const unsigned ad=ink[i+4];
  for(unsigned c=0;c<4;++c){
   const unsigned d=ad?std::min(255u,
      (unsigned(ink[i+c])*255u+ad/2u)/ad):0u;
   const unsigned blended=mode==1?mul255(src[c],d):
      mode==2?255u-mul255(255u-src[c],255u-d):src[c];
   const unsigned result=mul255(ink[i+c],255u-alpha)+
      mul255(src[c],mul255(alpha,255u-ad))+
      mul255(blended,mul255(alpha,ad));
   ink[i+c]=uint8_t(std::min(255u,result));
  }
  ink[i+4]=uint8_t(std::min(255u,alpha+mul255(ad,255u-alpha)));
  shape[p]=uint8_t(std::min(255u,geometry+mul255(shape[p],255u-geometry)));
  return true;
 }
 bool to_rgba(std::vector<uint8_t>& rgba) const {
  if(ink.size()!=size_t(width)*height*5)return false;
  rgba.assign(size_t(width)*height*4,0);
  for(size_t p=0;p<size_t(width)*height;++p){
   const size_t i=p*5,o=p*4;
   const unsigned a=ink[i+4];
   for(unsigned c=0;c<3;++c){
    const unsigned color=a?std::min(255u,
      (unsigned(ink[i+c])*255u+a/2u)/a):0u;
    const unsigned black=a?std::min(255u,
      (unsigned(ink[i+3])*255u+a/2u)/a):0u;
    rgba[o+c]=uint8_t(mul255(255u-std::min(255u,color+black),a));
   }
   rgba[o+3]=uint8_t(a);
  }
  return true;
 }
};
} // namespace rinpdf::renderer
