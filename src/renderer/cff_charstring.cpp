#include "sfnt.h"
#include <cmath>
#include <cstdint>
#include <vector>
namespace rinpdf::renderer {
bool sfnt_cff_simple_outline(const SfntFont& font,uint16_t glyph,
 std::vector<CffPath>& contours){
 contours.clear();
 const uint8_t* program=nullptr;
 size_t length=0;
 if(!sfnt_cff_glyph_program(font,glyph,program,length)||length>65536)return false;
 std::vector<double> stack;
 std::vector<CffPath> paths;
 double x=0,y=0;
 bool ended=false;
 size_t segments=0;
 size_t stems=0;
 bool path_started=false;
 auto coordinate=[&](double dx,double dy)->bool{
  x+=dx;y+=dy;
  return std::isfinite(x)&&std::isfinite(y)&&
   std::abs(x)<=1000000&&std::abs(y)<=1000000;
 };
 auto begin=[&](double dx,double dy)->bool{
  if(!coordinate(dx,dy)||paths.size()>=256)return false;
  path_started=true;
  paths.push_back({{x,y}});
  return true;
 };
 auto line=[&](double dx,double dy)->bool{
  if(paths.empty()||!coordinate(dx,dy)||++segments>8192)return false;
  paths.back().push_back({x,y});
  return true;
 };
 auto curve=[&](double a,double b,double c,double d,double e,double f)->bool{
  if(paths.empty()||++segments>8192)return false;
  const double x0=x,y0=y;
  if(!coordinate(a,b))return false;
  const double c1x=x,c1y=y;
  if(!coordinate(c,d))return false;
  const double c2x=x,c2y=y;
  if(!coordinate(e,f))return false;
  // Fixed subdivision provides an initial supported cubic path. Curves
  // can be flattened adaptively once the complete Type2 model exists.
  for(int i=1;i<=16;++i){
   const double t=double(i)/16,u=1-t;
   const double px=u*u*u*x0+3*u*u*t*c1x+3*u*t*t*c2x+t*t*t*x;
   const double py=u*u*u*y0+3*u*u*t*c1y+3*u*t*t*c2y+t*t*t*y;
   if(!std::isfinite(px)||!std::isfinite(py))return false;
   paths.back().push_back({px,py});
  }
  return true;
 };
 for(size_t pos=0;pos<length;){
  const unsigned op=program[pos++];
  if(op==28){
   if(length-pos<2)return false;
   stack.push_back(double(int16_t(uint16_t(program[pos])<<8|program[pos+1])));
   pos+=2;
  }else if(op>=32&&op<=246)stack.push_back(int(op)-139);
  else if(op>=247&&op<=250){
   if(pos>=length)return false;
   stack.push_back((int(op)-247)*256+program[pos++]+108);
  }else if(op>=251&&op<=254){
   if(pos>=length)return false;
   stack.push_back(-(int(op)-251)*256-program[pos++]-108);
  }else if(op==255){
   if(length-pos<4)return false;
   const uint32_t bits=uint32_t(program[pos])<<24|
      uint32_t(program[pos+1])<<16|uint32_t(program[pos+2])<<8|program[pos+3];
   pos+=4;
   stack.push_back(double(int32_t(bits))/65536.0);
  }else{
   const size_t n=stack.size();
   if(op==14){
    if(n!=0||pos!=length)return false;
    ended=true;break;
   }
   if(op==1||op==3||op==18||op==23){
    // Stem hints do not affect unhinted outline geometry.
    if(path_started||n%2||stems+n/2>96)return false;
    stems+=n/2;
   }else if(op==19||op==20){
    if(path_started||n%2||stems+n/2>96)return false;
    stems+=n/2;
    const size_t mask_bytes=(stems+7)/8;
    if(mask_bytes>length-pos)return false;
    pos+=mask_bytes;
   }else    if(op==21){
    if(n!=2||!begin(stack[0],stack[1]))return false;
   }else if(op==22||op==4){
    if(n!=1||!begin(op==22?stack[0]:0,op==4?stack[0]:0))return false;
   }else if(op==5){
    if(n<2||(n%2))return false;
    for(size_t i=0;i<n;i+=2)if(!line(stack[i],stack[i+1]))return false;
   }else if(op==6||op==7){
    if(!n)return false;
    bool horizontal=op==6;
    for(double v:stack){
     if(!line(horizontal?v:0,horizontal?0:v))return false;
     horizontal=!horizontal;
    }
   }else if(op==24){
    // rcurveline: one or more curves followed by a line.
    if(n<8||(n-2)%6)return false;
    for(size_t i=0;i+8<=n;i+=6)
     if(!curve(stack[i],stack[i+1],stack[i+2],stack[i+3],
               stack[i+4],stack[i+5]))return false;
    if(!line(stack[n-2],stack[n-1]))return false;
   }else if(op==25){
    // rlinecurve: one or more lines followed by a curve.
    if(n<8||(n-6)%2)return false;
    for(size_t i=0;i<n-6;i+=2)
     if(!line(stack[i],stack[i+1]))return false;
    if(!curve(stack[n-6],stack[n-5],stack[n-4],stack[n-3],
              stack[n-2],stack[n-1]))return false;
   }else if(op==8){
    if(n<6||(n%6))return false;
    for(size_t i=0;i<n;i+=6)
     if(!curve(stack[i],stack[i+1],stack[i+2],stack[i+3],
               stack[i+4],stack[i+5]))return false;
   }else return false;
   stack.clear();
  }
  if(stack.size()>48)return false;
 }
 if(!ended)return false;
 contours=std::move(paths);
 return true;
}
}
