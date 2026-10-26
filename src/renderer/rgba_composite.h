#pragma once
#include <algorithm>
#include <cstdint>
namespace rinpdf::renderer {
// Straight-alpha source-over for a solid RGB source with 8-bit coverage.
// The destination is unassociated (non-premultiplied) RGBA.
inline void composite_solid_rgba(uint8_t* pixel,uint8_t r,uint8_t g,
                                 uint8_t b,unsigned alpha){
 if(!pixel||alpha==0)return;
 alpha=std::min(alpha,255u);
 const unsigned dst_alpha=pixel[3];
 const unsigned retained=(255-alpha)*dst_alpha;
 const unsigned output_alpha=alpha+(retained+127)/255;
 if(!output_alpha)return;
 auto channel=[&](unsigned src,unsigned dst)->uint8_t{
  const unsigned numerator=alpha*src+(retained*dst+127)/255;
  return uint8_t(std::min(255u,(numerator+output_alpha/2)/output_alpha));
 };
 pixel[0]=channel(r,pixel[0]);
 pixel[1]=channel(g,pixel[1]);
 pixel[2]=channel(b,pixel[2]);
 pixel[3]=uint8_t(output_alpha);
}
// Opaque page-buffer source-over. The caller provides already evaluated
// blend-mode RGB values; this routine only applies coverage/opacity.
inline void composite_opaque_rgb(uint8_t* pixel,unsigned r,unsigned g,
                                 unsigned b,unsigned alpha){
 if(!pixel||alpha==0)return;
 alpha=std::min(alpha,255u);
 const unsigned inverse=255-alpha;
 pixel[0]=uint8_t((alpha*r+inverse*pixel[0]+127)/255);
 pixel[1]=uint8_t((alpha*g+inverse*pixel[1]+127)/255);
 pixel[2]=uint8_t((alpha*b+inverse*pixel[2]+127)/255);
 pixel[3]=255;
}

}
