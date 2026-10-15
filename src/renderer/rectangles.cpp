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
 if((size_t)width*height>16777216)return false;
 for(unsigned y=0;y<height;++y)std::memset(rgba+(size_t)y*stride,255,(size_t)width*4);
 double red=0,green=0,blue=0;
 struct Color { double r,g,b,a,d,e,f,width,sr,sg,sb; };
 double a=1,d=1,e=0,f=0;
 double line_width=1;
 double stroke_red=0,stroke_green=0,stroke_blue=0;
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
   saved.push_back({red,green,blue,a,d,e,f,line_width,stroke_red,stroke_green,stroke_blue});
  }else if(word=="Q"){
   if(!stack.empty()||saved.empty()||(!path.empty()||!polygons.empty()))return false;
   auto color=saved.back();saved.pop_back();red=color.r;green=color.g;blue=color.b;a=color.a;d=color.d;e=color.e;f=color.f;line_width=color.width;stroke_red=color.sr;stroke_green=color.sg;stroke_blue=color.sb;
  }else if(word=="cm"){
   if(stack.size()!=6||stack[1]!=0||stack[2]!=0)return false;
   double na=a*stack[0],nd=d*stack[3],ne=a*stack[4]+e,nf=d*stack[5]+f;
   if(!std::isfinite(na)||!std::isfinite(nd)||!std::isfinite(ne)||!std::isfinite(nf)||std::abs(na)>1000000||std::abs(nd)>1000000||std::abs(ne)>10000000||std::abs(nf)>10000000)return false;
   a=na;d=nd;e=ne;f=nf;stack.clear();
  }else if(word=="w"){
   if(stack.size()!=1||stack[0]<=0||stack[0]>1024)return false;
   line_width=stack[0];stack.clear();
  }else if(word=="RG"){
   if(stack.size()!=3)return false;
   for(double channel:stack)if(channel<0||channel>1)return false;
   stroke_red=stack[0];stroke_green=stack[1];stroke_blue=stack[2];stack.clear();
  }else if(word=="G"){
   if(stack.size()!=1||stack[0]<0||stack[0]>1)return false;
   stroke_red=stroke_green=stroke_blue=stack[0];stack.clear();
  }else if(word=="K"){
   if(stack.size()!=4)return false;
   for(double channel:stack)if(channel<0||channel>1)return false;
   stroke_red=(1-stack[0])*(1-stack[3]);
   stroke_green=(1-stack[1])*(1-stack[3]);
   stroke_blue=(1-stack[2])*(1-stack[3]);stack.clear();
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
  }else if(word=="c"||word=="v"||word=="y"){
   const size_t required=word=="c"?6:4;
   if(stack.size()!=required||polygons.empty()||polygons.back().empty()||polygons.back().size()>4096-16)return false;
   auto& poly=polygons.back();
   Point start=poly.back();
   auto transform=[&](double x,double y){return Point{a*x+e,d*y+f};};
   Point p1{},p2{},p3{};
   if(word=="c"){
    p1=transform(stack[0],stack[1]);p2=transform(stack[2],stack[3]);p3=transform(stack[4],stack[5]);
   }else if(word=="v"){
    p1=start;p2=transform(stack[0],stack[1]);p3=transform(stack[2],stack[3]);
   }else{
    p1=transform(stack[0],stack[1]);p2=transform(stack[2],stack[3]);p3=p2;
   }
   if(!std::isfinite(p1.x)||!std::isfinite(p1.y)||!std::isfinite(p2.x)||!std::isfinite(p2.y)||!std::isfinite(p3.x)||!std::isfinite(p3.y))return false;
   for(unsigned step=1;step<=16;++step){
    double t=(double)step/16,u=1-t;
    Point point{u*u*u*start.x+3*u*u*t*p1.x+3*u*t*t*p2.x+t*t*t*p3.x,
                u*u*u*start.y+3*u*u*t*p1.y+3*u*t*t*p2.y+t*t*t*p3.y};
    if(!std::isfinite(point.x)||!std::isfinite(point.y))return false;
    poly.push_back(point);
   }
   stack.clear();
  }else if(word=="h"){
   if(!stack.empty()||polygons.empty()||polygons.back().size()<2)return false;
   if(polygons.back().size()>=4096)return false;
   polygons.back().push_back(polygons.back().front());
  }else if(word=="re"){
   if(stack.size()!=4||path.size()>=4096)return false;
   path.push_back({a*stack[0]+e,d*stack[1]+f,a*stack[2],d*stack[3]});stack.clear();
  }else if(word=="S"||word=="s"){
   if(!stack.empty()||(!polygons.size()&&!path.size())||width>1024||height>1024)return false;
   if(word=="s"&&!polygons.empty()){
    auto& poly=polygons.back();
    if(poly.size()<2||poly.size()>=4096)return false;
    poly.push_back(poly.front());
   }
   const unsigned char cr=(unsigned char)std::lround(stroke_red*255),cg=(unsigned char)std::lround(stroke_green*255),cb=(unsigned char)std::lround(stroke_blue*255);
   auto segment=[&](Point p,Point q){
    double x0=(p.x-box.x0)*sx,y0=(box.y1-p.y)*sy;
    double x1=(q.x-box.x0)*sx,y1=(box.y1-q.y)*sy;
    double dx=x1-x0,dy=y1-y0,denom=dx*dx+dy*dy;
    double radius=0.5*line_width*std::max(std::abs(a*sx),std::abs(d*sy));
    if(!std::isfinite(denom)||!std::isfinite(radius)||radius>2048)return false;
    for(unsigned yy=0;yy<height;++yy)for(unsigned xx=0;xx<width;++xx){
     double px=xx+0.5,py=yy+0.5;
     double t=denom>0?std::clamp(((px-x0)*dx+(py-y0)*dy)/denom,0.0,1.0):0;
     double distance=std::hypot(px-(x0+t*dx),py-(y0+t*dy));
     if(distance<=radius){uint8_t* pixel=rgba+(size_t)yy*stride+(size_t)xx*4;pixel[0]=cr;pixel[1]=cg;pixel[2]=cb;pixel[3]=255;}
    }
    return true;
   };
   for(const auto& poly:polygons)for(size_t i=1;i<poly.size();++i)if(!segment(poly[i-1],poly[i]))return false;
   for(const Rect& rect:path){
    Point p0{rect.x,rect.y},p1{rect.x+rect.w,rect.y},p2{rect.x+rect.w,rect.y+rect.h},p3{rect.x,rect.y+rect.h};
    if(!segment(p0,p1)||!segment(p1,p2)||!segment(p2,p3)||!segment(p3,p0))return false;
   }
   path.clear();polygons.clear();
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
    if((size_t)width*height>1048576)return false;
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
      unsigned x_begin=(unsigned)std::clamp(std::ceil(lo-0.5),0.0,(double)width);
      unsigned x_end=(unsigned)std::clamp(std::ceil(hi-0.5),0.0,(double)width);
      for(unsigned x=x_begin;x<x_end;++x){
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
