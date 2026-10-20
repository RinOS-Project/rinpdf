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
 // Composite a child isolated CMYK group in native premultiplied ink.
 // A non-isolated CMYK child starts with the parent's ink backdrop,
 // but tracks its own painted shape independently.
 bool inherit_backdrop(const PremultipliedCMYKSurface& parent){
  if(width!=parent.width||height!=parent.height||
     ink.size()!=parent.ink.size()||
     shape.size()!=parent.shape.size())return false;
  ink=parent.ink;
  std::fill(shape.begin(),shape.end(),0);
  return true;
 }
 // Commit an inherited-backdrop group without source-over compositing the
 // inherited pixels twice. The parent's ink must match the saved snapshot.
 bool commit_nonisolated(const PremultipliedCMYKSurface& child,
                         const PremultipliedCMYKSurface& backdrop){
  if(width!=child.width||height!=child.height||
     width!=backdrop.width||height!=backdrop.height||
     ink.size()!=child.ink.size()||
     ink.size()!=backdrop.ink.size()||
     shape.size()!=child.shape.size()||
     shape.size()!=backdrop.shape.size()||
     ink!=backdrop.ink)return false;
  for(size_t p=0;p<shape.size();++p){
   const unsigned cover=child.shape[p];
   if(!cover)continue;
   for(unsigned c=0;c<5;++c)ink[p*5+c]=child.ink[p*5+c];
   shape[p]=uint8_t(std::min(255u,
      cover+mul255(shape[p],255u-cover)));
  }
  return true;
 }
 bool composite_group(const PremultipliedCMYKSurface& child,unsigned mode=0){
  if(mode>2||width!=child.width||height!=child.height||
     ink.size()!=child.ink.size()||shape.size()!=child.shape.size()||
     ink.size()!=size_t(width)*height*5)return false;
  for(size_t p=0;p<shape.size();++p){
   const size_t i=p*5;
   const unsigned as=child.ink[i+4],ad=ink[i+4];
   for(unsigned c=0;c<4;++c){
    const unsigned source=as?std::min(255u,
       (unsigned(child.ink[i+c])*255u+as/2u)/as):0u;
    const unsigned dest=ad?std::min(255u,
       (unsigned(ink[i+c])*255u+ad/2u)/ad):0u;
    const unsigned blend=mode==1?mul255(source,dest):
       mode==2?255u-mul255(255u-source,255u-dest):source;
    const unsigned result=mul255(ink[i+c],255u-as)+
       mul255(source,mul255(as,255u-ad))+
       mul255(blend,mul255(as,ad));
    ink[i+c]=uint8_t(std::min(255u,result));
   }
   ink[i+4]=uint8_t(std::min(255u,as+mul255(ad,255u-as)));
   shape[p]=uint8_t(std::min(255u,unsigned(child.shape[p])+
      mul255(shape[p],255u-child.shape[p])));
  }
  return true;
 }
 // Bridge for existing RGB rasterizers until they can emit native CMYK.
 // Premultiplied RGB is unassociated before conversion to ink components.
 bool import_rgba(const uint8_t* rgba,size_t stride){
  if(!rgba||stride<size_t(width)*4||
     ink.size()!=size_t(width)*height*5)return false;
  for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
   const size_t source=size_t(y)*stride+size_t(x)*4;
   const size_t destination=(size_t(y)*width+x)*5;
   const unsigned a=rgba[source+3];
   const auto unpremultiply=[&](unsigned channel)->uint8_t{
    return uint8_t(a?std::min(255u,
       (unsigned(rgba[source+channel])*255u+a/2u)/a):0u);
   };
   unsigned converted[4]{};
   from_rgb(unpremultiply(0),unpremultiply(1),unpremultiply(2),converted);
   for(unsigned c=0;c<4;++c)
    ink[destination+c]=uint8_t(mul255(converted[c],a));
   ink[destination+4]=uint8_t(a);
  }
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
