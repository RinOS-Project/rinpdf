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
struct Point {double x,y;};
bool render_rectangles_box(const uint8_t *contents,size_t size,unsigned width,unsigned height,uint8_t *rgba,size_t stride,PageBox box){
 if(!std::isfinite(box.x0)||!std::isfinite(box.y0)||!std::isfinite(box.x1)||!std::isfinite(box.y1)||box.x1<=box.x0||box.y1<=box.y0)return false;
 const double sx=(double)width/(box.x1-box.x0),sy=(double)height/(box.y1-box.y0);
 if(!std::isfinite(sx)||!std::isfinite(sy))return false;
 if(!rgba||(!contents&&size)||!width||!height||width>16384||height>16384||stride<(size_t)width*4)return false;
 if(size>16*1024*1024)return false;
 for(unsigned y=0;y<height;++y)std::memset(rgba+(size_t)y*stride,255,(size_t)width*4);
 double red=0,green=0,blue=0;
 struct Color { double r,g,b,a,d,e,f; };
 double a=1,d=1,e=0,f=0;
 std::vector<Color> saved;
 std::vector<double> stack;
 std::vector<Rect> path;
 std::vector<std::vector<Point>> polygons;
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
   saved.push_back({red,green,blue,a,d,e,f});
  }else if(word=="Q"){
   if(!stack.empty()||saved.empty()||(!path.empty()||!polygons.empty()))return false;
   auto color=saved.back();saved.pop_back();red=color.r;green=color.g;blue=color.b;a=color.a;d=color.d;e=color.e;f=color.f;
  }else if(word=="cm"){
   if(stack.size()!=6||stack[1]!=0||stack[2]!=0)return false;
   double na=a*stack[0],nd=d*stack[3],ne=a*stack[4]+e,nf=d*stack[5]+f;
   if(!std::isfinite(na)||!std::isfinite(nd)||!std::isfinite(ne)||!std::isfinite(nf)||std::abs(na)>1000000||std::abs(nd)>1000000||std::abs(ne)>10000000||std::abs(nf)>10000000)return false;
   a=na;d=nd;e=ne;f=nf;stack.clear();
  }else if(word=="rg"){
   if(stack.size()!=3)return false;
   for(double channel:stack)if(channel<0||channel>1)return false;
   red=stack[0];green=stack[1];blue=stack[2];stack.clear();
  }else if(word=="k"){
   if(stack.size()!=4)return false;
   for(double channel:stack)if(channel<0||channel>1)return false;
   red=(1-stack[0])*(1-stack[3]);
   green=(1-stack[1])*(1-stack[3]);
   blue=(1-stack[2])*(1-stack[3]);
   stack.clear();
  }else if(word=="g"){
   if(stack.size()!=1||stack[0]<0||stack[0]>1)return false;
   red=green=blue=stack[0];stack.clear();
  }else if(word=="m"){
   if(stack.size()!=2||polygons.size()>=4096)return false;
   polygons.push_back({{a*stack[0]+e,d*stack[1]+f}});stack.clear();
  }else if(word=="l"){
   if(stack.size()!=2||polygons.empty()||polygons.back().size()>=4096)return false;
   polygons.back().push_back({a*stack[0]+e,d*stack[1]+f});stack.clear();
  }else if(word=="h"){
   if(!stack.empty()||polygons.empty()||polygons.back().size()<2)return false;
   if(polygons.back().size()>=4096)return false;
   polygons.back().push_back(polygons.back().front());
  }else if(word=="re"){
   if(stack.size()!=4||path.size()>=4096)return false;
   path.push_back({a*stack[0]+e,d*stack[1]+f,a*stack[2],d*stack[3]});stack.clear();
  }else if(word=="f"||word=="F"){
   if(!stack.empty())return false;
   unsigned char cr=(unsigned char)std::lround(red*255);
   unsigned char cg=(unsigned char)std::lround(green*255);
   unsigned char cb=(unsigned char)std::lround(blue*255);
   for(const Rect& r:path){
    double left=std::clamp((std::min(r.x,r.x+r.w)-box.x0)*sx,0.0,(double)width);
    double right=std::clamp((std::max(r.x,r.x+r.w)-box.x0)*sx,0.0,(double)width);
    double top=std::clamp((box.y1-std::max(r.y,r.y+r.h))*sy,0.0,(double)height);
    double bottom=std::clamp((box.y1-std::min(r.y,r.y+r.h))*sy,0.0,(double)height);
    for(unsigned y=(unsigned)top;y<(unsigned)bottom;++y){
     for(unsigned x=(unsigned)left;x<(unsigned)right;++x){
      uint8_t *pixel=rgba+(size_t)y*stride+(size_t)x*4;
      pixel[0]=cr;pixel[1]=cg;pixel[2]=cb;pixel[3]=255;
     }
    }
   }
   for(const auto& poly:polygons){
    if(poly.size()<3)return false;
    for(unsigned y=0;y<height;++y){
     double scan=(double)y+0.5;
     std::vector<double> intersections;
     for(size_t i=0;i<poly.size();++i){
      const Point& p0=poly[i];const Point& p1=poly[(i+1)%poly.size()];
      double x0=(p0.x-box.x0)*sx,x1=(p1.x-box.x0)*sx;
      double y0=(box.y1-p0.y)*sy,y1=(box.y1-p1.y)*sy;
      if((y0<=scan&&y1>scan)||(y1<=scan&&y0>scan))
       intersections.push_back(x0+(scan-y0)*(x1-x0)/(y1-y0));
     }
     std::sort(intersections.begin(),intersections.end());
     if(intersections.size()%2)return false;
     for(size_t i=0;i+1<intersections.size();i+=2){
      double lo=std::clamp(intersections[i],0.0,(double)width);
      double hi=std::clamp(intersections[i+1],0.0,(double)width);
      for(unsigned x=(unsigned)std::ceil(lo-0.5);x<width&&(double)x+0.5<hi;++x){
       uint8_t* pixel=rgba+(size_t)y*stride+(size_t)x*4;
       pixel[0]=cr;pixel[1]=cg;pixel[2]=cb;pixel[3]=255;
      }
     }
    }
   }
   path.clear();polygons.clear();
  }else if(word=="n"){
   if(!stack.empty())return false;
   path.clear();polygons.clear();
  }else return false; // Do not silently ignore unknown operators.
 }
 return stack.empty()&&path.empty()&&polygons.empty()&&saved.empty();
}
bool render_rectangles(const uint8_t *contents,size_t size,unsigned width,unsigned height,uint8_t *rgba,size_t stride){
 return render_rectangles_box(contents,size,width,height,rgba,stride,{0,0,(double)width,(double)height});
}
}
