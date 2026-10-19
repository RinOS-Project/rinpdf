#include "../src/renderer/premultiplied_surface.h"
#include <cstdint>
#include <cstdio>

int main(){
 rinpdf::renderer::PremultipliedSurface surface;
 if(!surface.resize(2,1))return 1;
 const uint8_t black0[3]={128,0,0};
 const uint8_t white0[3]={255,127,127};
 const uint8_t black1[3]={0,0,0};
 const uint8_t white1[3]={255,255,255};
 if(!surface.set_reconstructed(0,0,black0,white0)||
    !surface.set_reconstructed(1,0,black1,white1))return 2;
 uint8_t output[8]={128,128,128,255,18,36,54,255};
 if(!surface.composite_over_opaque(output,8))return 3;
 if(output[0]<190||output[0]>193||
    output[1]<63||output[1]>65||
    output[2]<63||output[2]>65)return 4;
 if(output[4]!=18||output[5]!=36||output[6]!=54||
    output[7]!=255)return 5;
 // Inconsistent channel coverage is not a valid premultiplied pixel.
 const uint8_t broken_black[3]={0,0,0};
 const uint8_t broken_white[3]={255,100,255};
 if(surface.set_reconstructed(0,0,broken_black,broken_white))return 6;
 if(surface.resize(4096,4096))return 7;
 // Semi-transparent destination: blend modes must use unassociated
 // backdrop color for B(), then return premultiplied output.
 for(unsigned mode:{0u,1u,2u}){
  rinpdf::renderer::PremultipliedSurface layered;
  if(!layered.resize(1,1))return 8;
  if(!layered.blend_pixel(0,0,255,0,0,128,0))return 9;
  if(!layered.blend_pixel(0,0,0,0,255,128,mode))return 10;
  const auto& p=layered.pixels;
  if(p[3]<190||p[3]>193)return 11;
  if(mode==0&&!(p[0]>=62&&p[0]<=65&&p[2]>=127&&p[2]<=129))return 12;
  if(mode==1&&!(p[0]>=62&&p[0]<=65&&p[2]>=62&&p[2]<=65))return 13;
  if(mode==2&&!(p[0]>=127&&p[0]<=129&&p[2]>=127&&p[2]<=129))return 14;
  if(p[0]>p[3]||p[1]>p[3]||p[2]>p[3])return 15;
 }
 // Coverage/shape differs from opacity: an invisible paint can
 // contribute shape without changing premultiplied color alpha.
 rinpdf::renderer::PremultipliedSurface shapes,child;
 if(!shapes.resize(1,1)||!child.resize(1,1))return 16;
 if(!child.mark_shape(0,0,255)||!child.fill_pixel(0,0,255,0,0,0))
  return 17;
 if(child.shape[0]!=255||child.pixels[3]!=0)return 18;
 if(!child.composite_over(shapes)||shapes.shape[0]!=255||
    shapes.pixels[3]!=0)return 19;
 // Half-pixel shape coverage removes only half of earlier color,
 // independent of the second object's opacity.
 rinpdf::renderer::PremultipliedSurface edge;
 if(!edge.resize(1,1))return 20;
 edge.knockout=true;
 if(!edge.blend_pixel(0,0,255,0,0,255,0,255))return 21;
 if(!edge.blend_pixel(0,0,0,0,255,64,0,128))return 22;
 const auto& ep=edge.pixels;
 if(ep[0]<126||ep[0]>129||ep[2]<63||ep[2]>65||
    ep[3]<190||ep[3]>193)return 23;
 // Fully covered translucent source replaces earlier paint's backdrop,
 // without a second (1-alpha) attenuation. Zero-alpha knockout must clear it.
 rinpdf::renderer::PremultipliedSurface transparent_erase;
 if(!transparent_erase.resize(1,1))return 24;
 transparent_erase.knockout=true;
 if(!transparent_erase.blend_pixel(0,0,255,0,0,255,0,255)||
    !transparent_erase.blend_pixel(0,0,0,0,255,0,0,255))return 25;
 for(auto c:transparent_erase.pixels)if(c!=0)return 26;
 if(transparent_erase.blend_pixel(0,0,255,0,0,255,1,255))return 27;
 // A Form with multiple internal paints must be treated as one object:
 // knockout happens once with its union shape, not once per child stroke.
 rinpdf::renderer::PremultipliedSurface parent,object;
 if(!parent.resize(1,1)||!object.resize(1,1))return 28;
 parent.knockout=true;
 if(!parent.blend_pixel(0,0,255,0,0,255,0,255))return 29;
 if(!object.mark_shape(0,0,255)||
    !object.blend_pixel(0,0,0,255,0,128,0,255)||
    !object.blend_pixel(0,0,0,0,255,128,0,255))return 30;
 if(!parent.composite_knockout_object(object))return 31;
 const auto& result=parent.pixels;
 if(result[0]!=0||result[1]<63||result[1]>65||
    result[2]<127||result[2]>129||
    result[3]<190||result[3]>193)return 32;
 // A blended nested group must propagate its geometric shape even
 // if its alpha is zero; parent knockout uses shape, not alpha.
 rinpdf::renderer::PremultipliedSurface blending_parent,blending_child;
 if(!blending_parent.resize(1,1)||!blending_child.resize(1,1))
  return 33;
 if(!blending_child.mark_shape(0,0,255))return 34;
 for(unsigned mode:{1u,2u}){
  if(!blending_child.composite_blend_over(blending_parent,mode))return 35;
  if(blending_parent.shape[0]!=255||
     blending_parent.pixels[3]!=0)return 36;
 }
 // A non-isolated group inherits premultiplied backdrop color but not
 // the parent's shape. Committing it must not composite backdrop twice.
 rinpdf::renderer::PremultipliedSurface background,nonisolated,original;
 if(!background.resize(1,1)||!nonisolated.resize(1,1)||
    !original.resize(1,1))return 37;
 if(!background.fill_pixel(0,0,128,128,128,128)||
    !background.mark_shape(0,0,255))return 38;
 if(!original.copy_backdrop_from(background)||
    !nonisolated.copy_backdrop_from(background))return 39;
 if(nonisolated.shape[0]!=0||nonisolated.pixels[3]!=128)return 40;
 if(!nonisolated.mark_shape(0,0,255)||
    !nonisolated.fill_pixel(0,0,255,0,0,128))return 41;
 if(!nonisolated.composite_backdrop_difference_over(background,original))
  return 42;
 if(background.pixels[0]<158||background.pixels[0]>162||
    background.pixels[1]<30||background.pixels[1]>34||
    background.pixels[3]<190||background.pixels[3]>193||
    background.shape[0]!=255)return 43;
 // Non-isolated knockout restores inherited backdrop instead of
 // replacing covered pixels with transparent black.
 rinpdf::renderer::PremultipliedSurface host,ko;
 if(!host.resize(1,1)||!ko.resize(1,1))return 44;
 if(!host.fill_pixel(0,0,128,128,128,255)||
    !ko.begin_nonisolated_knockout(host))return 45;
 if(!ko.mark_shape(0,0,255)||
    !ko.blend_pixel(0,0,255,0,0,128,0,255)||
    !ko.blend_pixel(0,0,0,0,255,128,0,255))return 46;
 if(ko.pixels[0]<63||ko.pixels[0]>65||
    ko.pixels[1]<63||ko.pixels[1]>65||
    ko.pixels[2]!=255||ko.pixels[3]!=255)return 47;
 if(!ko.composite_backdrop_difference_over(host,host)||
    host.pixels!=ko.pixels)return 48;
 // A non-isolated group cannot overwrite a parent that changed after
 // the backdrop snapshot was taken.
 rinpdf::renderer::PremultipliedSurface stale_parent,stale_backdrop,stale_group;
 if(!stale_parent.resize(1,1)||!stale_backdrop.resize(1,1)||
    !stale_group.resize(1,1))return 49;
 if(!stale_parent.fill_pixel(0,0,120,80,40,255)||
    !stale_backdrop.copy_backdrop_from(stale_parent)||
    !stale_group.copy_backdrop_from(stale_parent))return 50;
 if(!stale_group.mark_shape(0,0,255)||
    !stale_group.fill_pixel(0,0,255,0,0,128))return 51;
 if(!stale_parent.fill_pixel(0,0,0,0,255,255))return 52;
 const auto saved=stale_parent.pixels;
 if(stale_group.composite_backdrop_difference_over(stale_parent,stale_backdrop))
  return 53;
 if(stale_parent.pixels!=saved)return 54;
 // Non-isolated knockout must use an independent immutable snapshot.
 // Passing an alias of the parent cannot detect concurrent parent changes.
 rinpdf::renderer::PremultipliedSurface nkparent,nkbackdrop,nkgroup;
 if(!nkparent.resize(1,1)||!nkbackdrop.resize(1,1)||
    !nkgroup.resize(1,1))return 55;
 if(!nkparent.fill_pixel(0,0,128,128,128,255)||
    !nkbackdrop.copy_backdrop_from(nkparent)||
    !nkgroup.begin_nonisolated_knockout(nkparent))return 56;
 if(!nkgroup.mark_shape(0,0,255)||
    !nkgroup.blend_pixel(0,0,255,0,0,128,0,255))return 57;
 if(!nkparent.fill_pixel(0,0,0,0,255,255))return 58;
 const auto protected_pixels=nkparent.pixels;
 if(nkgroup.composite_backdrop_difference_over(nkparent,nkbackdrop)||
    nkparent.pixels!=protected_pixels)return 59;
 return 0;
}
