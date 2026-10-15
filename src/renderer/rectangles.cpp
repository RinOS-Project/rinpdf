#include "rectangles.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
namespace rinpdf::renderer {
static bool number(const std::string& s,double& v){
 if(s.empty()||s.size()>64)return false;
 auto r=std::from_chars(s.data(),s.data()+s.size(),v);
 return r.ec==std::errc()&&r.ptr==s.data()+s.size()&&std::isfinite(v)&&std::abs(v)<10000000.0;
}
static bool space(unsigned char c){return c==0||c==9||c==10||c==12||c==13||c==32;}
struct Rect {double x,y,w,h;};
bool render_rectangles(const uint8_t *contents,size_t size,unsigned width,unsigned height,uint8_t *rgba,size_t stride){
 if(!rgba||(!contents&&size)||!width||!height||width>16384||height>16384||stride<(size_t)width*4)return false;
 if(size>16*1024*1024)return false;
 for(unsigned y=0;y<height;++y)std::memset(rgba+(size_t)y*stride,255,(size_t)width*4);
 double red=0,green=0,blue=0;
 struct Color { double r,g,b; };
 std::vector<Color> saved;
 std::vector<double> stack;
 std::vector<Rect> path;
 size_t pos=0;
 while(pos<size){
  while(pos<size&&space(contents[pos]))++pos;
  if(pos==size)break;
  if(contents[pos]=='%'){while(pos<size&&contents[pos]!='\n'&&contents[pos]!='\r')++pos;continue;}
  size_t begin=pos;
  while(pos<size&&!space(contents[pos]))++pos;
  std::string word(reinterpret_cast<const char*>(contents+begin),pos-begin);
  double v=0;
  if(number(word,v)){if(stack.size()>=16)return false;stack.push_back(v);continue;}
  if(word=="q"){
   if(!stack.empty()||saved.size()>=64)return false;
   saved.push_back({red,green,blue});
  }else if(word=="Q"){
   if(!stack.empty()||saved.empty()||!path.empty())return false;
   auto color=saved.back();saved.pop_back();red=color.r;green=color.g;blue=color.b;
  }else if(word=="rg"){
   if(stack.size()!=3)return false;
   for(double channel:stack)if(channel<0||channel>1)return false;
   red=stack[0];green=stack[1];blue=stack[2];stack.clear();
  }else if(word=="g"){
   if(stack.size()!=1||stack[0]<0||stack[0]>1)return false;
   red=green=blue=stack[0];stack.clear();
  }else if(word=="re"){
   if(stack.size()!=4||path.size()>=4096)return false;
   path.push_back({stack[0],stack[1],stack[2],stack[3]});stack.clear();
  }else if(word=="f"||word=="F"){
   if(!stack.empty())return false;
   unsigned char cr=(unsigned char)std::lround(red*255);
   unsigned char cg=(unsigned char)std::lround(green*255);
   unsigned char cb=(unsigned char)std::lround(blue*255);
   for(const Rect& r:path){
    double left=std::clamp(std::min(r.x,r.x+r.w),0.0,(double)width);
    double right=std::clamp(std::max(r.x,r.x+r.w),0.0,(double)width);
    double top=std::clamp((double)height-std::max(r.y,r.y+r.h),0.0,(double)height);
    double bottom=std::clamp((double)height-std::min(r.y,r.y+r.h),0.0,(double)height);
    for(unsigned y=(unsigned)top;y<(unsigned)bottom;++y){
     for(unsigned x=(unsigned)left;x<(unsigned)right;++x){
      uint8_t *pixel=rgba+(size_t)y*stride+(size_t)x*4;
      pixel[0]=cr;pixel[1]=cg;pixel[2]=cb;pixel[3]=255;
     }
    }
   }
   path.clear();
  }else if(word=="n"){
   if(!stack.empty())return false;
   path.clear();
  }else return false; // Do not silently ignore unknown operators.
 }
 return stack.empty()&&path.empty()&&saved.empty();
}
}
