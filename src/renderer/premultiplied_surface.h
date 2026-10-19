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
 // Geometric group shape is distinct from composited opacity.
 std::vector<uint8_t> shape;
 bool knockout=false;
 bool knockout_nonisolated=false;
 std::vector<uint8_t> knockout_backdrop;
 bool resize(unsigned w,unsigned h){
  if(!w||!h||size_t(w)*h>1048576u)return false;
  width=w;height=h;
  pixels.assign(size_t(w)*h*4,0);
  shape.assign(size_t(w)*h,0);
  return true;
 }
 // Start a non-isolated knockout group with its parent's backdrop.
 bool begin_nonisolated_knockout(const PremultipliedSurface& parent){
  if(!copy_backdrop_from(parent))return false;
  knockout=true;
  knockout_nonisolated=true;
  knockout_backdrop=pixels;
  return true;
 }
 // Union geometric coverage independently of source opacity.
 bool mark_shape(unsigned x,unsigned y,uint8_t coverage){
  if(x>=width||y>=height||shape.size()!=size_t(width)*height)
   return false;
  const size_t i=size_t(y)*width+x;
  shape[i]=uint8_t(std::min(255u,
      unsigned(coverage)+mul255(shape[i],255u-coverage)));
  return true;
 }
 static unsigned mul255(unsigned a,unsigned b){
  return (a*b+127)/255;
 }
 // Source-over a solid fill at the specified 8-bit coverage.
 // Color is supplied unassociated; storage remains premultiplied.
 bool fill_pixel(unsigned x,unsigned y,uint8_t red,uint8_t green,
                 uint8_t blue,uint8_t coverage,uint8_t geometry=255){
  if(x>=width||y>=height||
     pixels.size()!=size_t(width)*height*4)return false;
  const size_t index=(size_t(y)*width+x)*4;
  if(knockout_nonisolated)
   return blend_pixel(x,y,red,green,blue,coverage,0,geometry);
  if(knockout){
   const unsigned remaining=255u-geometry;
   for(unsigned c=0;c<4;++c)
    pixels[index+c]=uint8_t(mul255(pixels[index+c],remaining));
  }
  // In a knockout group, geometry has already reduced the prior
  // contribution. Multiplying it again by (1-alpha) double-attenuates.
  const unsigned remain=knockout?255u:255u-coverage;
  const unsigned colors[3]={red,green,blue};
  for(unsigned c=0;c<3;++c)
   pixels[index+c]=uint8_t(std::min(255u,
      mul255(colors[c],coverage)+mul255(pixels[index+c],remain)));
  pixels[index+3]=uint8_t(std::min(255u,
      unsigned(coverage)+mul255(pixels[index+3],remain)));
  return true;
 }
 // PDF separable blending on a transparent premultiplied backdrop.
 // Modes: 0 Normal, 1 Multiply, 2 Screen. Source colors are unassociated.
 bool blend_pixel(unsigned x,unsigned y,uint8_t red,uint8_t green,
                  uint8_t blue,uint8_t source_alpha,unsigned mode,
                  uint8_t geometry=255){
  if(mode>2||(knockout&&mode!=0)||x>=width||y>=height||
     pixels.size()!=size_t(width)*height*4)return false;
  const size_t i=(size_t(y)*width+x)*4;
  if(knockout_nonisolated){
   if(mode!=0||knockout_backdrop.size()!=pixels.size()||
      source_alpha>geometry)return false;
   const unsigned keep=255u-geometry;
   const unsigned backdrop_weight=geometry-source_alpha;
   const unsigned src[3]={red,green,blue};
   for(unsigned c=0;c<4;++c){
    const unsigned source=c==3?source_alpha:mul255(src[c],source_alpha);
    pixels[i+c]=uint8_t(std::min(255u,
      mul255(pixels[i+c],keep)+
      mul255(knockout_backdrop[i+c],backdrop_weight)+source));
   }
   return true;
  }
  if(knockout){
   const unsigned remaining=255u-geometry;
   for(unsigned c=0;c<4;++c)
    pixels[i+c]=uint8_t(mul255(pixels[i+c],remaining));
  }
  if(knockout){
   // Knockout replacement adds source paint after shape attenuation.
   // Alpha is independent of shape and must not attenuate the prior twice.
   const unsigned src[3]={red,green,blue};
   for(unsigned c=0;c<3;++c)
    pixels[i+c]=uint8_t(std::min(255u,
        unsigned(pixels[i+c])+mul255(src[c],source_alpha)));
   pixels[i+3]=uint8_t(std::min(255u,
        unsigned(pixels[i+3])+unsigned(source_alpha)));
   return true;
  }
  const unsigned as=source_alpha,ad=pixels[i+3];
  const unsigned src[3]={red,green,blue};
  for(unsigned c=0;c<3;++c){
   const unsigned dest_p=pixels[i+c];
   // Unassociate only for computing the blend function; never divide by 0.
   const unsigned dest=ad?std::min(255u,(dest_p*255u+ad/2)/ad):0u;
   const unsigned blended=mode==1?(src[c]*dest+127u)/255u:
      mode==2?255u-((255u-src[c])*(255u-dest)+127u)/255u:src[c];
   // Co = (1-as)*Dp + (1-ad)*as*Cs + as*ad*Blend(Cs,Cd).
   const uint64_t numerator=uint64_t(dest_p)*(255u-as)*255u+
      uint64_t(src[c])*as*(255u-ad)+uint64_t(blended)*as*ad;
   pixels[i+c]=uint8_t(std::min<uint64_t>(255u,
                           (numerator+32512u)/65025u));
  }
  pixels[i+3]=uint8_t(std::min(255u,as+mul255(ad,255u-as)));
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
 // Snapshot the parent's premultiplied backdrop without inheriting its
 // accumulated object shape. A non-isolated group needs the colors/alpha
 // of its parent, while its own shape starts empty.
 bool copy_backdrop_from(const PremultipliedSurface& parent){
  if(width!=parent.width||height!=parent.height||
     pixels.size()!=size_t(width)*height*4||
     parent.pixels.size()!=pixels.size()||
     shape.size()!=size_t(width)*height)return false;
  pixels=parent.pixels;
  std::fill(shape.begin(),shape.end(),0);
  return true;
 }
 // Track the group's contribution separately from the inherited backdrop.
 // Only Normal source-over is currently valid for this difference.
 bool composite_backdrop_difference_over(
     PremultipliedSurface& parent,const PremultipliedSurface& backdrop) const {
  if(width!=parent.width||height!=parent.height||
     width!=backdrop.width||height!=backdrop.height||
     pixels.size()!=parent.pixels.size()||
     pixels.size()!=backdrop.pixels.size()||
     shape.size()!=parent.shape.size()||
     shape.size()!=size_t(width)*height)return false;
  // The parent must still be the backdrop against which the group was
  // rendered. Replacing pixels after a parent mutation would discard paint.
  if(parent.pixels!=backdrop.pixels)return false;
  // A non-isolated Normal group already contains its inherited backdrop.
  // Replacing those pixels (not source-overing them) prevents painting the
  // inherited backdrop twice. Preserve parent shape outside this group.
  for(size_t p=0;p<shape.size();++p){
   if(!shape[p])continue;
   const size_t i=4*p;
   for(unsigned c=0;c<4;++c)parent.pixels[i+c]=pixels[i+c];
   parent.shape[p]=uint8_t(std::min(255u,
     unsigned(shape[p])+mul255(parent.shape[p],255u-shape[p])));
  }
  return true;
 }
 // Apply an entire rendered object to a knockout group as one unit.
 // The object's geometric shape removes previous objects; its color and
 // alpha are added once, avoiding per-primitive knockout inside nested Forms.
 bool composite_knockout_object(const PremultipliedSurface& object){
  if(!knockout||width!=object.width||height!=object.height||
     pixels.size()!=object.pixels.size()||
     shape.size()!=object.shape.size()||
     pixels.size()!=size_t(width)*height*4||
     shape.size()!=size_t(width)*height)return false;
  for(size_t p=0;p<shape.size();++p){
   const unsigned coverage=object.shape[p];
   if(!coverage)continue;
   const unsigned keep=255u-coverage;
   const size_t i=p*4;
   for(unsigned c=0;c<4;++c)
    pixels[i+c]=uint8_t(std::min(255u,
       mul255(pixels[i+c],keep)+unsigned(object.pixels[i+c])));
   shape[p]=uint8_t(std::min(255u,
       coverage+mul255(shape[p],keep)));
  }
  return true;
 }
 // Source-over another premultiplied surface; preserve both alphas.
 bool composite_over(PremultipliedSurface& destination) const {
  if(width!=destination.width||height!=destination.height||
     pixels.size()!=destination.pixels.size()||
     shape.size()!=destination.shape.size())return false;
  for(size_t p=0;p<shape.size();++p)
   destination.shape[p]=uint8_t(std::min(255u,
       unsigned(shape[p])+mul255(destination.shape[p],255u-shape[p])));
  for(size_t i=0;i<pixels.size();i+=4){
   const unsigned remaining=255u-pixels[i+3];
   for(unsigned c=0;c<3;++c)
    destination.pixels[i+c]=uint8_t(std::min(255u,
       unsigned(pixels[i+c])+mul255(destination.pixels[i+c],remaining)));
   destination.pixels[i+3]=uint8_t(std::min(255u,
       unsigned(pixels[i+3])+mul255(destination.pixels[i+3],remaining)));
  }
  return true;
 }
 // Composite an isolated group with the group's external blend mode.
 // The input remains premultiplied; its unassociated color is used for B().
 bool composite_blend_over(PremultipliedSurface& destination,unsigned mode) const {
  if(mode>2||width!=destination.width||height!=destination.height||
     pixels.size()!=destination.pixels.size()||
     pixels.size()!=size_t(width)*height*4)return false;
  if(mode==0)return composite_over(destination);
  if(shape.size()!=size_t(width)*height||
     destination.shape.size()!=shape.size())return false;
  for(size_t p=0;p<shape.size();++p)
   destination.shape[p]=uint8_t(std::min(255u,
       unsigned(shape[p])+mul255(destination.shape[p],255u-shape[p])));
  for(size_t i=0;i<pixels.size();i+=4){
   const unsigned alpha=pixels[i+3];
   if(!alpha)continue;
   const uint8_t red=uint8_t(std::min(255u,
                       (unsigned(pixels[i])*255u+alpha/2)/alpha));
   const uint8_t green=uint8_t(std::min(255u,
                       (unsigned(pixels[i+1])*255u+alpha/2)/alpha));
   const uint8_t blue=uint8_t(std::min(255u,
                       (unsigned(pixels[i+2])*255u+alpha/2)/alpha));
   if(!destination.blend_pixel(unsigned((i/4)%width),
          unsigned((i/4)/width),red,green,blue,uint8_t(alpha),mode))
    return false;
  }
  return true;
 }
 bool composite_blend_over_opaque(uint8_t* dst,size_t stride,
                                  unsigned mode) const {
  if(mode>2||!dst||stride<size_t(width)*4||
     pixels.size()!=size_t(width)*height*4)return false;
  if(mode==0)return composite_over_opaque(dst,stride);
  for(unsigned y=0;y<height;++y)
   for(unsigned x=0;x<width;++x){
    const size_t i=(size_t(y)*width+x)*4;
    const unsigned alpha=pixels[i+3];
    if(!alpha)continue;
    uint8_t* d=dst+size_t(y)*stride+size_t(x)*4;
    for(unsigned c=0;c<3;++c){
     const unsigned src=std::min(255u,
              (unsigned(pixels[i+c])*255u+alpha/2)/alpha);
     const unsigned backdrop=d[c];
     const unsigned blend=mode==1?(src*backdrop+127)/255:
              255-((255-src)*(255-backdrop)+127)/255;
     d[c]=uint8_t(mul255(blend,alpha)+
                     mul255(backdrop,255-alpha));
    }
    d[3]=255;
   }
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
