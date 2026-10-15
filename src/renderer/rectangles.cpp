#include "rectangles.h"
#include "path_mask.h"
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
 struct Color { double r,g,b,a,bx,cx,d,e,f,width,sr,sg,sb; int cap,join; double miter; };
 double a=1,bx=0,cx=0,d=1,e=0,f=0;
 double line_width=1;
 int line_cap=0,line_join=0;
 double miter_limit=10;
 double stroke_red=0,stroke_green=0,stroke_blue=0;
 std::vector<Color> saved;
 std::vector<double> stack;
 std::vector<Rect> path;
 std::vector<std::vector<Point>> polygons;
 struct Matrix {double a,b,c,d;};
 std::vector<std::vector<Matrix>> polygon_matrices;
 std::vector<Matrix> rect_matrices;
 auto matrix=[&](){return Matrix{a,bx,cx,d};};
 struct Clip {unsigned x0,y0,x1,y1;};
 Clip clip{0,0,width,height};
 std::vector<Clip> clip_stack;
 std::vector<uint8_t> clip_mask;
 std::vector<std::vector<uint8_t>> clip_mask_stack;
 size_t saved_clip_bytes=0;
 constexpr size_t max_saved_clip_bytes=32u*1024u*1024u;
 bool pending_rect_clip=false;
 bool pending_path_clip=false;
 bool mixed_path_ctm=false;
 FillRule pending_rule=FillRule::NonZero;
 auto transform=[&](double x,double y){return Point{a*x+cx*y+e,bx*x+d*y+f};};
 auto shapes_from_path=[&](){
  std::vector<std::vector<MaskPoint>> shapes;
  for(const Rect& rect:path){
   auto pixel=[&](double x,double y){return MaskPoint{(x-box.x0)*sx,(box.y1-y)*sy};};
   shapes.push_back({pixel(rect.x,rect.y),pixel(rect.x+rect.w,rect.y),pixel(rect.x+rect.w,rect.y+rect.h),pixel(rect.x,rect.y+rect.h)});
  }
  for(const auto& poly:polygons){std::vector<MaskPoint> shape;for(const Point& p:poly)shape.push_back({(p.x-box.x0)*sx,(box.y1-p.y)*sy});shapes.push_back(std::move(shape));}
  return shapes;
 };

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
   if((pending_rect_clip||pending_path_clip)||!stack.empty()||saved.size()>=64)return false;
   saved.push_back({red,green,blue,a,bx,cx,d,e,f,line_width,stroke_red,stroke_green,stroke_blue,line_cap,line_join,miter_limit});
   if(clip_mask.size()>max_saved_clip_bytes-saved_clip_bytes)return false;
   clip_stack.push_back(clip);
   clip_mask_stack.push_back(clip_mask);
   saved_clip_bytes+=clip_mask.size();

  }else if(word=="Q"){
   if((pending_rect_clip||pending_path_clip)||!stack.empty()||saved.empty())return false;
   auto color=saved.back();saved.pop_back();
   if((!path.empty()||!polygons.empty())&&(a!=color.a||bx!=color.bx||cx!=color.cx||d!=color.d||e!=color.e||f!=color.f))mixed_path_ctm=true;
   red=color.r;green=color.g;blue=color.b;a=color.a;bx=color.bx;cx=color.cx;d=color.d;e=color.e;f=color.f;line_width=color.width;stroke_red=color.sr;stroke_green=color.sg;stroke_blue=color.sb;line_cap=color.cap;line_join=color.join;miter_limit=color.miter;
   clip=clip_stack.back();clip_stack.pop_back();
   saved_clip_bytes-=clip_mask_stack.back().size();
   clip_mask=std::move(clip_mask_stack.back());clip_mask_stack.pop_back();

  }else if(word=="cm"){
   if(pending_rect_clip||pending_path_clip)return false;
   if(stack.size()!=6)return false;
   if(!path.empty()||!polygons.empty())mixed_path_ctm=true;
   const double A=stack[0],B=stack[1],C=stack[2],D=stack[3],E=stack[4],F=stack[5];
   const double na=a*A+cx*B,nb=bx*A+d*B,nc=a*C+cx*D,nd=bx*C+d*D;
   const double ne=a*E+cx*F+e,nf=bx*E+d*F+f;
   for(double value:{na,nb,nc,nd,ne,nf})if(!std::isfinite(value)||std::abs(value)>10000000)return false;
   a=na;bx=nb;cx=nc;d=nd;e=ne;f=nf;stack.clear();
  }else if(word=="w"){
   if(stack.size()!=1||stack[0]<0||stack[0]>1024)return false;
   line_width=stack[0];stack.clear();
  }else if(word=="J"){
   if(stack.size()!=1||stack[0]<0||stack[0]>2||std::floor(stack[0])!=stack[0])return false;
   line_cap=(int)stack[0];stack.clear();
  }else if(word=="j"){
   if(stack.size()!=1||stack[0]<0||stack[0]>2||std::floor(stack[0])!=stack[0])return false;
   line_join=(int)stack[0];stack.clear();
  }else if(word=="M"){
   if(stack.size()!=1||stack[0]<1||stack[0]>1000)return false;
   miter_limit=stack[0];stack.clear();
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
   if(pending_path_clip)return false;
   if(pending_rect_clip||pending_path_clip)return false;
   if(stack.size()!=2||polygons.size()>=4096)return false;
   const Point point=transform(stack[0],stack[1]);
   if(!std::isfinite(point.x)||!std::isfinite(point.y))return false;
   polygons.push_back({point});polygon_matrices.push_back({matrix()});stack.clear();
  }else if(word=="l"){
   if(pending_path_clip)return false;
   if(pending_rect_clip)return false;
   if(stack.size()!=2||polygons.empty()||polygons.back().size()>=4096)return false;
   const Point point=transform(stack[0],stack[1]);
   if(!std::isfinite(point.x)||!std::isfinite(point.y))return false;
   polygons.back().push_back(point);polygon_matrices.back().push_back(matrix());stack.clear();
  }else if(word=="c"||word=="v"||word=="y"){
   if(pending_path_clip||pending_rect_clip)return false;
   const size_t required=word=="c"?6:4;
   if(stack.size()!=required||polygons.empty()||polygons.back().empty()||polygons.back().size()>4096-16)return false;
   auto& poly=polygons.back();
   Point start=poly.back();
   auto transform_curve=[&](double x,double y){return transform(x,y);};
   Point p1{},p2{},p3{};
   if(word=="c"){
    p1=transform_curve(stack[0],stack[1]);p2=transform_curve(stack[2],stack[3]);p3=transform_curve(stack[4],stack[5]);
   }else if(word=="v"){
    p1=start;p2=transform_curve(stack[0],stack[1]);p3=transform_curve(stack[2],stack[3]);
   }else{
    p1=transform_curve(stack[0],stack[1]);p2=transform_curve(stack[2],stack[3]);p3=p2;
   }
   if(!std::isfinite(p1.x)||!std::isfinite(p1.y)||!std::isfinite(p2.x)||!std::isfinite(p2.y)||!std::isfinite(p3.x)||!std::isfinite(p3.y))return false;
   for(unsigned step=1;step<=16;++step){
    double t=(double)step/16,u=1-t;
    Point point{u*u*u*start.x+3*u*u*t*p1.x+3*u*t*t*p2.x+t*t*t*p3.x,
                u*u*u*start.y+3*u*u*t*p1.y+3*u*t*t*p2.y+t*t*t*p3.y};
    if(!std::isfinite(point.x)||!std::isfinite(point.y))return false;
    poly.push_back(point);polygon_matrices.back().push_back(matrix());
   }
   stack.clear();
  }else if(word=="h"){
   if(pending_path_clip)return false;
   if(pending_rect_clip)return false;
   if(!stack.empty()||polygons.empty()||polygons.back().size()<2)return false;
   if(polygons.back().size()>=4096)return false;
   const Point first=polygons.back().front(),last=polygons.back().back();
   if(first.x!=last.x||first.y!=last.y){polygons.back().push_back(first);polygon_matrices.back().push_back(matrix());}
  }else if(word=="re"){
   if(pending_path_clip)return false;
   if(pending_rect_clip)return false;
   if(stack.size()!=4||path.size()>=4096||polygons.size()>=4096)return false;
   {const Point p=transform(stack[0],stack[1]);const Point q=transform(stack[0]+stack[2],stack[1]+stack[3]);
   const Point p1=transform(stack[0]+stack[2],stack[1]),p3=transform(stack[0],stack[1]+stack[3]);
   for(const Point point:{p,p1,q,p3})if(!std::isfinite(point.x)||!std::isfinite(point.y))return false;
   if(bx!=0||cx!=0){polygons.push_back({p,p1,q,p3,p});polygon_matrices.push_back(std::vector<Matrix>(5,matrix()));}
   else {path.push_back({p.x,p.y,q.x-p.x,q.y-p.y});rect_matrices.push_back(matrix());}stack.clear();}
  }else if(word=="W"||word=="W*"){
   if(!stack.empty()||pending_rect_clip||pending_path_clip)return false;
   pending_rule=word=="W*"?FillRule::EvenOdd:FillRule::NonZero;
   pending_rect_clip=path.size()==1&&polygons.empty();
   pending_path_clip=!pending_rect_clip;
  }else if(word=="S"||word=="s"){
   if(pending_rect_clip||pending_path_clip||!stack.empty()||width>1024||height>1024)return false;
   if(polygons.empty()&&path.empty())continue;
   // Move-only subpaths contain no drawable segments.
   bool drawable=!path.empty();
   for(const auto& poly:polygons)if(poly.size()>1)drawable=true;
   if(!drawable){path.clear();polygons.clear();polygon_matrices.clear();rect_matrices.clear();mixed_path_ctm=false;continue;}
   if(word=="s"&&!polygons.empty()){
    auto& poly=polygons.back();
    // A final move-only subpath has no closing segment.
    if(poly.size()>1){
     if(poly.size()>=4096)return false;
     const Point first=poly.front(),last=poly.back();
     if(first.x!=last.x||first.y!=last.y){poly.push_back(first);polygon_matrices.back().push_back(matrix());}
    }
   }
   const unsigned char cr=(unsigned char)std::lround(stroke_red*255),cg=(unsigned char)std::lround(stroke_green*255),cb=(unsigned char)std::lround(stroke_blue*255);
   size_t stroke_work=0;
   const bool stroke_aa=(size_t)width*height<=262144;
   std::vector<uint8_t> stroke_samples((size_t)width*height,0);
   auto segment=[&](Point p,Point q,Matrix mat,bool join_disk=false){
    double x0=(p.x-box.x0)*sx,y0=(box.y1-p.y)*sy;
    double x1=(q.x-box.x0)*sx,y1=(box.y1-q.y)*sy;
    double dx=x1-x0,dy=y1-y0,denom=dx*dx+dy*dy;
    // A zero-length butt/square segment has no stroke area. Round caps
    // deliberately retain the circular point footprint.
    if(denom==0&&line_cap!=1&&!join_disk)return true;
    const double metric=std::hypot(std::hypot(mat.a*sx,mat.b*sy),std::hypot(mat.c*sx,mat.d*sy));
    const double effective_width=line_width==0?(metric>0?1.0/metric:0):line_width;
    const double radius=0.5*effective_width*metric;
    const double determinant=mat.a*mat.d-mat.b*mat.c;
    if(!std::isfinite(determinant)||std::abs(determinant)<1e-12)return false;
    if(!std::isfinite(denom)||!std::isfinite(radius)||radius>2048)return false;
    double extra=radius+1;
    double low_x=std::min(x0,x1)-extra,high_x=std::max(x0,x1)+extra;
    double low_y=std::min(y0,y1)-extra,high_y=std::max(y0,y1)+extra;
    if(!std::isfinite(low_x)||!std::isfinite(high_x)||!std::isfinite(low_y)||!std::isfinite(high_y))return false;
    unsigned x_begin=(unsigned)std::clamp(std::floor(low_x),0.0,(double)width);
    unsigned x_end=(unsigned)std::clamp(std::ceil(high_x),0.0,(double)width);
    unsigned y_begin=(unsigned)std::clamp(std::floor(low_y),0.0,(double)height);
    unsigned y_end=(unsigned)std::clamp(std::ceil(high_y),0.0,(double)height);
    size_t work=(size_t)(x_end-x_begin)*(y_end-y_begin);
    if(work>16777216-stroke_work)return false;
    stroke_work+=work;
    for(unsigned yy=y_begin;yy<y_end;++yy)for(unsigned xx=x_begin;xx<x_end;++xx){
     auto covered=[&](double px,double py){
      const double ux=(px-x0)/sx,uy=-(py-y0)/sy;
      const double vx=dx/sx,vy=-dy/sy;
      const double qx=(mat.d*ux-mat.c*uy)/determinant,qy=(-mat.b*ux+mat.a*uy)/determinant;
      const double ex=(mat.d*vx-mat.c*vy)/determinant,ey=(-mat.b*vx+mat.a*vy)/determinant;
      const double length2=ex*ex+ey*ey;
      double projection=length2>0?(qx*ex+qy*ey)/length2:0;
      if(!join_disk&&line_cap==0&&(projection<0||projection>1))return false;
      const double extension=length2>0?(effective_width*0.5)/std::sqrt(length2):0;
      if(!join_disk&&line_cap==2&&(projection< -extension||projection>1+extension))return false;
      const double t=(join_disk||line_cap==1)?std::clamp(projection,0.0,1.0):projection;
      return std::hypot(qx-t*ex,qy-t*ey)<=effective_width*0.5;
     };
     uint8_t bits=0;
     if(stroke_aa){
      if(covered(xx+0.25,yy+0.25))bits|=1;
      if(covered(xx+0.75,yy+0.25))bits|=2;
      if(covered(xx+0.25,yy+0.75))bits|=4;
      if(covered(xx+0.75,yy+0.75))bits|=8;
     }else if(covered(xx+0.5,yy+0.5))bits=15;
     stroke_samples[(size_t)yy*width+xx]|=bits;
    }
    return true;
   };
   for(size_t k=0;k<polygons.size();++k){const auto& poly=polygons[k];for(size_t i=1;i<poly.size();++i)if(!segment(poly[i-1],poly[i],polygon_matrices[k][i]))return false;}
   // Round joins are disks at connected vertices. Keep the union in
   // stroke_samples so each subsample is blended only once.
   if(line_join==1){
    for(size_t k=0;k<polygons.size();++k){
     const auto& poly=polygons[k];
     for(size_t i=1;i+1<poly.size();++i){
      if(!segment(poly[i],poly[i],polygon_matrices[k][i],true))return false;
     }
     if(poly.size()>2&&poly.front().x==poly.back().x&&poly.front().y==poly.back().y){
      if(!segment(poly.front(),poly.front(),polygon_matrices[k].front(),true))return false;
     }
    }
   }
   for(size_t k=0;k<path.size();++k){const Rect& rect=path[k];
    Point p0{rect.x,rect.y},p1{rect.x+rect.w,rect.y},p2{rect.x+rect.w,rect.y+rect.h},p3{rect.x,rect.y+rect.h};
    if(!segment(p0,p1,rect_matrices[k])||!segment(p1,p2,rect_matrices[k])||!segment(p2,p3,rect_matrices[k])||!segment(p3,p0,rect_matrices[k]))return false;
   }
   if(line_join==1){
    for(size_t k=0;k<path.size();++k){
     const Rect& rect=path[k];
     for(const Point p:{Point{rect.x,rect.y},Point{rect.x+rect.w,rect.y},Point{rect.x+rect.w,rect.y+rect.h},Point{rect.x,rect.y+rect.h}}){
      if(!segment(p,p,rect_matrices[k],true))return false;
     }
    }
   }
   for(size_t i=0;i<stroke_samples.size();++i){
    unsigned x=(unsigned)(i%width),y=(unsigned)(i/width);
    if(x<clip.x0||x>=clip.x1||y<clip.y0||y>=clip.y1||(!clip_mask.empty()&&!clip_mask[i]))continue;
    unsigned bits=stroke_samples[i],hits=(unsigned)((bits&1)!=0)+((bits&2)!=0)+((bits&4)!=0)+((bits&8)!=0);
    if(!hits)continue;
    unsigned alpha=(hits*255+2)/4;
    if(!clip_mask.empty())alpha=(alpha*clip_mask[i]+127)/255;
    if(!alpha)continue;
    uint8_t* pixel=rgba+(size_t)y*stride+(size_t)x*4;
    pixel[0]=(uint8_t)((alpha*cr+(255-alpha)*pixel[0]+127)/255);
    pixel[1]=(uint8_t)((alpha*cg+(255-alpha)*pixel[1]+127)/255);
    pixel[2]=(uint8_t)((alpha*cb+(255-alpha)*pixel[2]+127)/255);
    pixel[3]=255;
   }
   path.clear();polygons.clear();polygon_matrices.clear();rect_matrices.clear();mixed_path_ctm=false;
  }else if(word=="f"||word=="F"||word=="f*"){
   if((pending_rect_clip||pending_path_clip)||!stack.empty())return false;
   unsigned char cr=(unsigned char)std::lround(red*255);
   unsigned char cg=(unsigned char)std::lround(green*255);
   unsigned char cb=(unsigned char)std::lround(blue*255);
   if((size_t)width*height>1048576)return false;

   auto shapes=shapes_from_path();
   std::vector<uint8_t> coverage;
   const FillRule rule=word=="f*"?FillRule::EvenOdd:FillRule::NonZero;
   const bool antialias=(size_t)width*height<=262144;
   if(antialias){
    if(!rasterize_path_coverage_2x(shapes,width,height,rule,coverage))return false;
   }else{
    if(!rasterize_path_mask(shapes,width,height,rule,coverage))return false;
   }
   for(size_t i=0;i<coverage.size();++i){
    unsigned x=(unsigned)(i%width),y=(unsigned)(i/width);
    if(!coverage[i]||(!clip_mask.empty()&&!clip_mask[i])||x<clip.x0||x>=clip.x1||y<clip.y0||y>=clip.y1)continue;
    uint8_t* pixel=rgba+(size_t)y*stride+(size_t)x*4;
    unsigned alpha=antialias?coverage[i]:255;
    if(!clip_mask.empty())alpha=(alpha*clip_mask[i]+127)/255;
    if(!alpha)continue;
    pixel[0]=(uint8_t)((alpha*cr+(255-alpha)*pixel[0]+127)/255);
    pixel[1]=(uint8_t)((alpha*cg+(255-alpha)*pixel[1]+127)/255);
    pixel[2]=(uint8_t)((alpha*cb+(255-alpha)*pixel[2]+127)/255);
    pixel[3]=255;
   }
   path.clear();polygons.clear();polygon_matrices.clear();rect_matrices.clear();mixed_path_ctm=false;
  }else if(word=="n"){
   if(!stack.empty())return false;
   if(pending_path_clip){
    auto shapes=shapes_from_path();
    std::vector<uint8_t> mask;
    if((size_t)width*height<=262144){
     if(!rasterize_path_coverage_2x(shapes,width,height,pending_rule,mask))return false;
    }else{
     if(!rasterize_path_mask(shapes,width,height,pending_rule,mask))return false;
     for(auto& v:mask)v=v?255:0;
    }
    if(clip_mask.empty())clip_mask=std::move(mask);
    else if(!intersect_path_masks(clip_mask,mask))return false;
    pending_path_clip=false;
   }
   if(pending_rect_clip){
    auto shapes=shapes_from_path();
    std::vector<uint8_t> mask;
    if((size_t)width*height<=262144){
     if(!rasterize_path_coverage_2x(shapes,width,height,pending_rule,mask))return false;
    }else{
     if(!rasterize_path_mask(shapes,width,height,pending_rule,mask))return false;
     for(auto& value:mask)value=value?255:0;
    }
    if(clip_mask.empty())clip_mask=std::move(mask);
    else if(!intersect_path_masks(clip_mask,mask))return false;
    pending_rect_clip=false;
   }
   path.clear();polygons.clear();polygon_matrices.clear();rect_matrices.clear();mixed_path_ctm=false;
  }else return false; // Do not silently ignore unknown operators.
 }
 return stack.empty()&&path.empty()&&polygons.empty()&&saved.empty()&&!pending_rect_clip&&!pending_path_clip;
}
bool render_rectangles(const uint8_t *contents,size_t size,unsigned width,unsigned height,uint8_t *rgba,size_t stride){
 return render_rectangles_box(contents,size,width,height,rgba,stride,{0,0,(double)width,(double)height});
}
}
