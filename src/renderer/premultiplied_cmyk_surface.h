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
 bool knockout=false;
 bool nonisolated_knockout=false;
 std::vector<uint8_t> backdrop_ink;
 static unsigned mul255(unsigned a,unsigned b){return (a*b+127u)/255u;}
 // Compute a separable source-over channel in one rounded division.
 // Avoid repeated 8-bit rounding in each partial product.
 static uint8_t blend_premultiplied(unsigned destination_p,
     unsigned src_unassociated,unsigned blended,
     unsigned source_alpha,unsigned destination_alpha){
  const uint64_t numerator=
      uint64_t(destination_p)*(255u-source_alpha)*255u+
      uint64_t(src_unassociated)*source_alpha*(255u-destination_alpha)+
      uint64_t(blended)*source_alpha*destination_alpha;
  return uint8_t(std::min<uint64_t>(255u,(numerator+32512u)/65025u));
 }
 bool resize(unsigned w,unsigned h){
  if(!w||!h||size_t(w)*h>1048576u)return false;
  width=w;height=h;
  ink.assign(size_t(w)*h*5,0);
  shape.assign(size_t(w)*h,0);
  knockout=false;
  nonisolated_knockout=false;
  backdrop_ink.clear();
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
  return paint_ink(x,y,src,alpha,mode,geometry);
 }
 // Paint exact DeviceCMYK components without an RGB round-trip.
 bool paint_ink(unsigned x,unsigned y,const unsigned src[4],
                uint8_t alpha,unsigned mode=0,uint8_t geometry=255){
  if(!src||mode>2||x>=width||y>=height||
     ink.size()!=size_t(width)*height*5||
     (nonisolated_knockout&&backdrop_ink.size()!=ink.size()))return false;
  for(unsigned c=0;c<4;++c)if(src[c]>255)return false;
  const size_t p=size_t(y)*width+x,i=p*5;
  if(knockout){
   if(alpha>geometry)return false;
   const unsigned keep=255u-geometry;
   const unsigned backdrop_alpha=nonisolated_knockout?
       backdrop_ink[i+4]:0u;
   for(unsigned c=0;c<5;++c){
    const unsigned inherited=nonisolated_knockout?
       mul255(backdrop_ink[i+c],geometry-alpha):0u;
    const unsigned current=mul255(ink[i+c],keep);
    unsigned added=alpha;
    if(c<4){
     const unsigned backdrop=backdrop_alpha?
         std::min(255u,(unsigned(backdrop_ink[i+c])*255u+
                        backdrop_alpha/2u)/backdrop_alpha):0u;
     const unsigned blended=mode==1?mul255(src[c],backdrop):
        mode==2?255u-mul255(255u-src[c],255u-backdrop):src[c];
     // Use the immutable group backdrop for blending, never a preceding
     // knockout sibling. On an isolated backdrop this reduces to source.
     added=blend_premultiplied(0,src[c],blended,
                              alpha,backdrop_alpha);
    }
    ink[i+c]=uint8_t(std::min(255u,current+inherited+added));
   }
   for(unsigned channel=0;channel<4;++channel)
    ink[i+channel]=uint8_t(std::min<unsigned>(ink[i+channel],ink[i+4]));
   shape[p]=uint8_t(std::min(255u,geometry+mul255(shape[p],keep)));
   return true;
  }
  const unsigned ad=ink[i+4];
  for(unsigned c=0;c<4;++c){
   const unsigned d=ad?std::min(255u,
      (unsigned(ink[i+c])*255u+ad/2u)/ad):0u;
   const unsigned blended=mode==1?mul255(src[c],d):
      mode==2?255u-mul255(255u-src[c],255u-d):src[c];
   ink[i+c]=blend_premultiplied(ink[i+c],src[c],blended,
                                alpha,ad);
  }
  ink[i+4]=uint8_t(std::min(255u,alpha+mul255(ad,255u-alpha)));
  for(unsigned c=0;c<4;++c)
   ink[i+c]=uint8_t(std::min<unsigned>(ink[i+c],ink[i+4]));
  shape[p]=uint8_t(std::min(255u,geometry+mul255(shape[p],255u-geometry)));
  return true;
 }
 // Paint a complete nested Form as one knockout object. In an isolated
 // group the child starts transparent. In a non-isolated group the child
 // must have been rendered on the immutable inherited backdrop.
 bool knockout_object(const PremultipliedCMYKSurface& object,unsigned mode=0){
  if(mode>2||!knockout||width!=object.width||height!=object.height||
     ink.size()!=object.ink.size()||shape.size()!=object.shape.size()||
     ink.size()!=size_t(width)*height*5||
     (nonisolated_knockout&&backdrop_ink.size()!=ink.size()))return false;
  for(size_t p=0;p<shape.size();++p){
   const unsigned coverage=object.shape[p];
   if(!coverage)continue;
   const size_t i=p*5;
   const unsigned keep=255u-coverage;
   for(unsigned c=0;c<5;++c){
    const unsigned old_part=mul255(ink[i+c],keep);
    unsigned painted=object.ink[i+c];
    if(nonisolated_knockout){
     const unsigned inherited=mul255(backdrop_ink[i+c],keep);
     painted=painted>inherited?painted-inherited:0u;
    }
    if(mode!=0&&c<4){
     // Blend the completed object against the immutable group backdrop,
     // not against earlier knockout siblings.
     const unsigned ba=nonisolated_knockout?backdrop_ink[i+4]:0u;
     const unsigned inherited_alpha=mul255(ba,keep);
     const unsigned oa=object.ink[i+4]>inherited_alpha?
       unsigned(object.ink[i+4])-inherited_alpha:0u;
     const unsigned src=oa?std::min(255u,
       (painted*255u+oa/2u)/oa):0u;
     const unsigned dst=ba?std::min(255u,
       (unsigned(backdrop_ink[i+c])*255u+ba/2u)/ba):0u;
     const unsigned blended=mode==1?mul255(src,dst):
       255u-mul255(255u-src,255u-dst);
     // An isolated knockout has no backdrop and therefore reduces to
     // Normal; a non-isolated group uses the saved initial backdrop.
     if(ba){
      const unsigned blend_p=mul255(blended,mul255(oa,ba));
      const unsigned source_only=mul255(src,mul255(oa,255u-ba));
      painted=std::min(255u,blend_p+source_only);
     }
    }
    ink[i+c]=uint8_t(std::min(255u,old_part+painted));
   }
   // The opacity channel bounds every premultiplied ink channel.
   // Rounding and CMYK blend reconstruction must never violate this.
   for(unsigned c=0;c<4;++c)
    ink[i+c]=uint8_t(std::min<unsigned>(ink[i+c],ink[i+4]));
   shape[p]=uint8_t(std::min(255u,
      coverage+mul255(shape[p],keep)));
  }
  return true;
 }
 bool begin_knockout(bool isolated){
  if(ink.size()!=size_t(width)*height*5||
     shape.size()!=size_t(width)*height)return false;
  knockout=true;
  nonisolated_knockout=!isolated;
  if(!isolated)backdrop_ink=ink;
  else backdrop_ink.clear();
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
    ink[i+c]=blend_premultiplied(ink[i+c],source,blend,as,ad);
   }
   ink[i+4]=uint8_t(std::min(255u,as+mul255(ad,255u-as)));
   for(unsigned c=0;c<4;++c)
    ink[i+c]=uint8_t(std::min<unsigned>(ink[i+c],ink[i+4]));
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
