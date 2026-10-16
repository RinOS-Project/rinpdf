#include "rectangles.h"
#include "path_mask.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <memory>
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
 struct Color { double r,g,b,a,bx,cx,d,e,f,width,sr,sg,sb; int cap,join; double miter; std::vector<double> dash; double dash_phase; };
 double a=1,bx=0,cx=0,d=1,e=0,f=0;
 double line_width=1;
 int line_cap=0,line_join=0;
 double miter_limit=10;
 std::vector<double> dash_pattern;
 double dash_phase=0;
 bool reading_dash=false;
 bool dash_array_ready=false;
 std::vector<double> dash_values;
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
 std::shared_ptr<const std::vector<uint8_t>> clip_mask;
 std::vector<std::shared_ptr<const std::vector<uint8_t>>> clip_mask_stack;
 bool pending_rect_clip=false;
 bool pending_path_clip=false;
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
  if(contents[pos]=='['||contents[pos]==']')++pos;
  else while(pos<size&&!space(contents[pos])&&contents[pos]!='['&&contents[pos]!=']')++pos;
  std::string word(reinterpret_cast<const char*>(contents+begin),pos-begin);
  double v=0;
  if(number(word,v)){if(reading_dash){if(dash_values.size()>=32)return false;dash_values.push_back(v);}else{if(dash_array_ready&&stack.size()>=1)return false;if(stack.size()>=16)return false;stack.push_back(v);}continue;}
  if(word=="["){if(reading_dash||dash_array_ready||!stack.empty())return false;reading_dash=true;dash_values.clear();continue;}
  if(word=="]"){if(!reading_dash)return false;reading_dash=false;dash_array_ready=true;continue;}
  // Only numeric elements are legal inside a dash array.
  if(reading_dash)return false;
  if(dash_array_ready&&word!="d"&&!(number(word,v)))return false;
  if(word=="q"){
   if((pending_rect_clip||pending_path_clip)||!stack.empty()||saved.size()>=64)return false;
   saved.push_back({red,green,blue,a,bx,cx,d,e,f,line_width,stroke_red,stroke_green,stroke_blue,line_cap,line_join,miter_limit,dash_pattern,dash_phase});
   clip_stack.push_back(clip);
   clip_mask_stack.push_back(clip_mask);

  }else if(word=="Q"){
   if((pending_rect_clip||pending_path_clip)||!stack.empty()||saved.empty())return false;
   auto color=saved.back();saved.pop_back();
   red=color.r;green=color.g;blue=color.b;a=color.a;bx=color.bx;cx=color.cx;d=color.d;e=color.e;f=color.f;line_width=color.width;stroke_red=color.sr;stroke_green=color.sg;stroke_blue=color.sb;line_cap=color.cap;line_join=color.join;miter_limit=color.miter;dash_pattern=color.dash;dash_phase=color.dash_phase;
   clip=clip_stack.back();clip_stack.pop_back();
   clip_mask=std::move(clip_mask_stack.back());clip_mask_stack.pop_back();

  }else if(word=="cm"){
   if(pending_rect_clip||pending_path_clip)return false;
   if(stack.size()!=6)return false;
   const double A=stack[0],B=stack[1],C=stack[2],D=stack[3],E=stack[4],F=stack[5];
   const double na=a*A+cx*B,nb=bx*A+d*B,nc=a*C+cx*D,nd=bx*C+d*D;
   const double ne=a*E+cx*F+e,nf=bx*E+d*F+f;
   for(double value:{na,nb,nc,nd,ne,nf})if(!std::isfinite(value)||std::abs(value)>10000000)return false;
   a=na;bx=nb;cx=nc;d=nd;e=ne;f=nf;stack.clear();
  }else if(word=="d"){
   if(reading_dash||!dash_array_ready||stack.size()!=1||dash_values.size()>32)return false;
   double total=0;
   for(double value:dash_values){if(!std::isfinite(value)||value<0||value>1000000)return false;total+=value;}
   if(!std::isfinite(total)||total>1000000||(!dash_values.empty()&&total<=0)||stack[0]<0)return false;
   dash_pattern=dash_values;dash_phase=total>0?std::fmod(stack[0],total*(dash_values.size()%2?2:1)):0;
   dash_values.clear();dash_array_ready=false;stack.clear();
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
   if(!stack.empty()||polygons.empty())return false;
   // Closing a move-only subpath does not create a drawable segment.
   if(polygons.back().size()==1)continue;
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
   if(!drawable){path.clear();polygons.clear();polygon_matrices.clear();rect_matrices.clear();continue;}
   if(word=="s"&&!polygons.empty()){
    auto& poly=polygons.back();
    // A final move-only subpath has no closing segment.
    if(poly.size()>1){
     const Point first=poly.front(),last=poly.back();
     if(first.x!=last.x||first.y!=last.y){
      if(poly.size()>=4096)return false;
      poly.push_back(first);
      polygon_matrices.back().push_back(matrix());
     }
    }
   }
   // Normalize rectangle stroke paths to closed polygons so every join
   // style shares the same vertex processing and closing-corner logic.
   for(size_t k=0;k<path.size();++k){
    const Rect& rect=path[k];
    const Point p0{rect.x,rect.y},p1{rect.x+rect.w,rect.y};
    const Point p2{rect.x+rect.w,rect.y+rect.h},p3{rect.x,rect.y+rect.h};
    polygons.push_back({p0,p1,p2,p3,p0});
    polygon_matrices.push_back(std::vector<Matrix>(5,rect_matrices[k]));
   }
   path.clear();
   rect_matrices.clear();
   const unsigned char cr=(unsigned char)std::lround(stroke_red*255),cg=(unsigned char)std::lround(stroke_green*255),cb=(unsigned char)std::lround(stroke_blue*255);
   size_t stroke_work=0;
   const bool stroke_aa=(size_t)width*height<=262144;
   std::vector<uint8_t> stroke_samples((size_t)width*height,0);
   auto segment=[&](Point p,Point q,Matrix mat,bool join_disk=false,bool start_cap=true,bool end_cap=true){
    double x0=(p.x-box.x0)*sx,y0=(box.y1-p.y)*sy;
    double x1=(q.x-box.x0)*sx,y1=(box.y1-q.y)*sy;
    double dx=x1-x0,dy=y1-y0,denom=dx*dx+dy*dy;
    // A zero-length butt/square segment has no stroke area. Round caps
    // deliberately retain the circular point footprint.
    if(denom==0&&line_cap==0&&!join_disk)return true;
    const double metric=std::hypot(std::hypot(mat.a*sx,mat.b*sy),std::hypot(mat.c*sx,mat.d*sy));
    const double effective_width=line_width==0?(metric>0?1.0/metric:0):line_width;
    // Frobenius norm bounds the transformed unit normals. This is a
    // conservative raster bound, not a substitute for exact coverage.
    const double radius=0.5*effective_width*metric;
    const double determinant=mat.a*mat.d-mat.b*mat.c;
    if(!std::isfinite(determinant)||std::abs(determinant)<1e-12)return false;
    if(!std::isfinite(denom)||!std::isfinite(radius)||radius>2048)return false;
    // Inverse-transform the segment direction once, not for each pixel.
    const double vx=dx/sx,vy=-dy/sy;
    const double ex=(mat.d*vx-mat.c*vy)/determinant;
    const double ey=(-mat.b*vx+mat.a*vy)/determinant;
    const double length2=ex*ex+ey*ey;
    if(!std::isfinite(length2))return false;
    const double inv_length=length2>0?1.0/std::sqrt(length2):0.0;
    const double extension=length2>0?(effective_width*0.5)*inv_length:0.0;
    if(!std::isfinite(inv_length)||!std::isfinite(extension))return false;
    // A square cap extends half a width along and across the segment.
    // Its diagonal reaches sqrt(2) times the half-width from an endpoint.
    // Only exposed square endpoints need the diagonal cap extension.
    // Interior dashed/solid pieces use the ordinary stroke bound.
    const bool exposed_square_cap=!join_disk&&line_cap==2&&(start_cap||end_cap);
    const double extra=radius*(exposed_square_cap?std::sqrt(2.0):1.0)+1;
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
      const double qx=(mat.d*ux-mat.c*uy)/determinant,qy=(-mat.b*ux+mat.a*uy)/determinant;
      if(!std::isfinite(qx)||!std::isfinite(qy))return false;
      if(length2==0&&!join_disk&&line_cap==2)
       return std::abs(qx)<=effective_width*0.5&&std::abs(qy)<=effective_width*0.5;
      double projection=length2>0?(qx*ex+qy*ey)/length2:0;
      if(!std::isfinite(projection))return false;
      if(!join_disk&&((!start_cap&&projection<0)||(!end_cap&&projection>1)||
        (line_cap==0&&(projection<0||projection>1))))return false;
      if(!join_disk&&line_cap==2){
       if(projection<(start_cap?-extension:0)||projection>(end_cap?1+extension:1))return false;
       // Square caps extend the stroke rectangle, not its circular ends.
       const double perpendicular=std::abs(qx*ey-qy*ex)*inv_length;
       return perpendicular<=effective_width*0.5;
      }
      const double t=join_disk?std::clamp(projection,0.0,1.0):
       (line_cap==1?(start_cap&&projection<0?0.0:end_cap&&projection>1?1.0:projection):projection);
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
   struct DashedJoin {Point prev,vertex,next;Matrix incoming,outgoing;};
   std::vector<DashedJoin> dashed_joins;
   std::vector<size_t> fully_painted_closed_paths;
   auto stroke_path=[&](size_t path_index,const std::vector<Point>& points,const std::vector<Matrix>& matrices)->bool{
    if(points.size()!=matrices.size())return false;
    if(dash_pattern.empty()){
     const bool closed=points.size()>2&&points.front().x==points.back().x&&points.front().y==points.back().y;
     for(size_t i=1;i<points.size();++i){
      const bool start_cap=!closed&&i==1,end_cap=!closed&&i+1==points.size();
      if(!segment(points[i-1],points[i],matrices[i],false,start_cap,end_cap))return false;
     }
     return true;
    }
    const size_t count=dash_pattern.size(),cycle=count*(count%2?2:1);
    size_t index=0;
    double remaining=dash_pattern[0],phase=dash_phase;
    // Skip zero-width dash elements without consuming path length.
    auto advance=[&]()->bool{
     for(size_t n=0;n<cycle;++n){
      index=(index+1)%cycle;
      remaining=dash_pattern[index%count];
      if(remaining>0)return true;
     }
     return false;
    };
    // Resolve the phase by bounded traversal of one normalized cycle.
    // Subtracting a very small dash repeatedly can otherwise lose
    // precision and fail to advance a large phase.
    for(size_t step=0;phase>0&&step<=cycle;++step){
     if(remaining<=0){if(!advance())return false;continue;}
     if(phase>=remaining){
      phase-=remaining;
      remaining=0;
     }else{
      remaining-=phase;
      phase=0;
     }
    }
    if(phase>0)return false;
    // Resolve exact phase boundaries before consuming the first segment.
    if(remaining<=0&&!advance())return false;
    // When a single painted dash covers the entire closed path, the
    // seam is a join, not two independent capped dash endpoints.
    const bool closed=points.size()>2&&points.front().x==points.back().x&&points.front().y==points.back().y;
    if(closed&&index%2==0){
     double total_length=0;
     for(size_t i=1;i<points.size();++i){
      const Matrix mat=matrices[i];
      const double det=mat.a*mat.d-mat.b*mat.c;
      if(!std::isfinite(det)||std::abs(det)<1e-12)return false;
      const double dx=points[i].x-points[i-1].x,dy=points[i].y-points[i-1].y;
      total_length+=std::hypot((mat.d*dx-mat.c*dy)/det,(-mat.b*dx+mat.a*dy)/det);
      if(!std::isfinite(total_length))return false;
     }
     if(total_length<=remaining){
      fully_painted_closed_paths.push_back(path_index);
      for(size_t i=1;i<points.size();++i)
       if(!segment(points[i-1],points[i],matrices[i],false,false,false))return false;
      return true;
     }
    }
    // Detect a painted run crossing the closed contour seam.
    // This does not imply that the whole contour is painted.
    const bool first_on=closed&&index%2==0;
    bool seam_on=false;
    if(first_on){
     double length=0;
     for(size_t i=1;i<points.size();++i){
      const Matrix mat=matrices[i];
      const double det=mat.a*mat.d-mat.b*mat.c;
      if(!std::isfinite(det)||std::abs(det)<1e-12)return false;
      const double dx=points[i].x-points[i-1].x,dy=points[i].y-points[i-1].y;
      length+=std::hypot((mat.d*dx-mat.c*dy)/det,(-mat.b*dx+mat.a*dy)/det);
      if(!std::isfinite(length))return false;
     }
     size_t probe=index;
     double left=remaining;
     bool resolved=length<=1e-10;
     // A zero-length contour has no incoming or outgoing tangent.
     if(resolved)seam_on=false;
     for(size_t n=0;n<65536&&length>1e-10;++n){
      if(left<=1e-12){
       bool found=false;
       for(size_t j=0;j<cycle;++j){
        probe=(probe+1)%cycle;
        left=dash_pattern[probe%count];
        if(left>0){found=true;break;}
       }
       if(!found)return false;
      }
      const double take=std::min(left,length);
      if(!std::isfinite(take)||take<=0)return false;
      const double before=length;
      length-=take;left-=take;
      if(length==before)return false;
      if(length<=1e-10){
       // If the perimeter ends exactly at a dash boundary, the
       // preceding on-run ends there; it does not continue across seam.
       seam_on=probe%2==0&&left>1e-10;
       resolved=true;
       break;
      }
     }
     // Do not silently render an incorrect cap when the finite traversal
     // budget cannot resolve the state at the closed contour seam.
     if(!resolved)return false;
    }
    const bool joined_seam=first_on&&seam_on;
    size_t pieces=0;
    bool in_on_run=false;
    Point run_end{};
    Matrix run_matrix{};
    std::vector<std::pair<Point,Matrix>> on_run_vertices;
    auto emit_run=[&]()->bool{
     if(!in_on_run)return true;
     // At a painted closed seam, the final dash endpoint is a join,
     // not a round cap. Interior dash runs retain their normal caps.
     const bool at_seam=joined_seam&&run_end.x==points.front().x&&run_end.y==points.front().y;
     if(line_cap==1&&!at_seam&&!segment(run_end,run_end,run_matrix,true))return false;
     if(line_join==1){
      for(const auto& vertex:on_run_vertices)
       if(!segment(vertex.first,vertex.first,vertex.second,true))return false;
     }
     on_run_vertices.clear();
     in_on_run=false;
     return true;
    };
    for(size_t i=1;i<points.size();++i){
     const Point p=points[i-1],q=points[i];
     // Dash lengths are measured in user-space units, not in the
     // transformed device-space coordinates stored in the current path.
     const Matrix mat=matrices[i];
     const double determinant=mat.a*mat.d-mat.b*mat.c;
     if(!std::isfinite(determinant)||std::abs(determinant)<1e-12)return false;
     const double dx=q.x-p.x,dy=q.y-p.y;
     const double ux=(mat.d*dx-mat.c*dy)/determinant;
     const double uy=(-mat.b*dx+mat.a*dy)/determinant;
     const double len=std::hypot(ux,uy);
     if(!std::isfinite(len))return false;
     if(len==0)continue;
     double pos=0;
     while(pos<len){
      if(++pieces>65536)return false;
      if(remaining<=1e-12){if(!advance())return false;continue;}
      double take=std::min(remaining,len-pos);
      if(take<=0)return false;
      if(index%2==0){
       const double t0=pos/len,t1=(pos+take)/len;
       Point a{p.x+(q.x-p.x)*t0,p.y+(q.y-p.y)*t0};
       Point b{p.x+(q.x-p.x)*t1,p.y+(q.y-p.y)*t1};
       if(!in_on_run){
        if(line_cap==1&&!(joined_seam&&i==1&&pos==0)&&!segment(a,a,matrices[i],true))return false;
       }else if(pos==0){
        if(line_join==1){
         if(on_run_vertices.size()>=4096)return false;
         on_run_vertices.push_back({a,matrices[i]});
        }
        if(i>=2&&line_join!=1){
         if(dashed_joins.size()>=4096)return false;
         dashed_joins.push_back({points[i-2],a,b,matrices[i-1],matrices[i]});
        }
       }
       // A dash run may cross a path vertex without acquiring a cap.
       // Only the start and end of a painted run receive square caps.
       bool breaks_after=false;
       if(remaining-take<=1e-12){
        for(size_t n=1;n<=cycle;++n){
         const size_t next_index=(index+n)%cycle;
         if(dash_pattern[next_index%count]>0){
          breaks_after=(next_index%2)!=0;
          break;
         }
        }
       }
       const bool last_piece=i+1==points.size()&&len-(pos+take)<=1e-12;
       const bool start_cap=!in_on_run&&!(joined_seam&&i==1&&pos==0);
       const bool end_cap=breaks_after||(last_piece&&!joined_seam);
       if(!segment(a,b,matrices[i],false,start_cap,end_cap))return false;
       in_on_run=true;run_end=b;run_matrix=matrices[i];
      }else if(!emit_run())return false;
      pos+=take;remaining-=take;
     }
    }
    if(!emit_run())return false;
    if(joined_seam&&points.size()>3){
     const Point& p=points.front();
     // Skip repeated vertices to recover the actual incoming and outgoing
     // tangents. The previous guard discarded a valid seam join entirely.
     size_t incoming=points.size()-2;
     while(incoming>0&&std::hypot(p.x-points[incoming].x,p.y-points[incoming].y)<=1e-10)--incoming;
     size_t outgoing=1;
     while(outgoing+1<points.size()&&std::hypot(p.x-points[outgoing].x,p.y-points[outgoing].y)<=1e-10)++outgoing;
     if(std::hypot(p.x-points[incoming].x,p.y-points[incoming].y)>1e-10&&
        std::hypot(p.x-points[outgoing].x,p.y-points[outgoing].y)>1e-10){
      if(line_join==1){
       if(!segment(p,p,matrices.back(),true))return false;
      }else if(line_join==0||line_join==2){
       if(dashed_joins.size()>=4096)return false;
       dashed_joins.push_back({points[incoming],p,points[outgoing],matrices[incoming+1],matrices[outgoing]});
      }
     }
    }
    return true;
   };
   for(size_t k=0;k<polygons.size();++k)if(!stroke_path(k,polygons[k],polygon_matrices[k]))return false;
   // Round joins are disks at connected vertices. Keep the union in
   // stroke_samples so each subsample is blended only once.
   if(line_join==1){
    for(size_t k=0;k<polygons.size();++k){
     if(!dash_pattern.empty()&&std::find(fully_painted_closed_paths.begin(),fully_painted_closed_paths.end(),k)==fully_painted_closed_paths.end())continue;
     const auto& poly=polygons[k];
     for(size_t i=1;i+1<poly.size();++i){
      if(!segment(poly[i],poly[i],polygon_matrices[k][i],true))return false;
     }
     if(poly.size()>2&&poly.front().x==poly.back().x&&poly.front().y==poly.back().y){
      if(!segment(poly.front(),poly.front(),polygon_matrices[k].front(),true))return false;
     }
    }
   }
   // Join geometry in device space. Supported for uniform, axis-aligned CTM.
   auto paint_triangle=[&](Point p0,Point p1,Point p2)->bool{
    const double x0=(p0.x-box.x0)*sx,y0=(box.y1-p0.y)*sy;
    const double x1=(p1.x-box.x0)*sx,y1=(box.y1-p1.y)*sy;
    const double x2=(p2.x-box.x0)*sx,y2=(box.y1-p2.y)*sy;
    const double area=(x1-x0)*(y2-y0)-(y1-y0)*(x2-x0);
    if(!std::isfinite(x0)||!std::isfinite(y0)||!std::isfinite(x1)||!std::isfinite(y1)||!std::isfinite(x2)||!std::isfinite(y2)||!std::isfinite(area))return false;
    if(std::abs(area)<1e-10)return true;
    unsigned xb=(unsigned)std::clamp(std::floor(std::min({x0,x1,x2})-1),0.0,(double)width);
    unsigned xe=(unsigned)std::clamp(std::ceil(std::max({x0,x1,x2})+1),0.0,(double)width);
    unsigned yb=(unsigned)std::clamp(std::floor(std::min({y0,y1,y2})-1),0.0,(double)height);
    unsigned ye=(unsigned)std::clamp(std::ceil(std::max({y0,y1,y2})+1),0.0,(double)height);
    size_t work=(size_t)(xe-xb)*(ye-yb);
    if(work>16777216-stroke_work)return false;
    stroke_work+=work;
    for(unsigned yy=yb;yy<ye;++yy)for(unsigned xx=xb;xx<xe;++xx){
     auto inside=[&](double px,double py){
      double e0=(x1-x0)*(py-y0)-(y1-y0)*(px-x0);
      double e1=(x2-x1)*(py-y1)-(y2-y1)*(px-x1);
      double e2=(x0-x2)*(py-y2)-(y0-y2)*(px-x2);
      return area>0?(e0>=0&&e1>=0&&e2>=0):(e0<=0&&e1<=0&&e2<=0);
     };
     uint8_t bits=0;
     if(stroke_aa){
      if(inside(xx+.25,yy+.25))bits|=1;
      if(inside(xx+.75,yy+.25))bits|=2;
      if(inside(xx+.25,yy+.75))bits|=4;
      if(inside(xx+.75,yy+.75))bits|=8;
     }else if(inside(xx+.5,yy+.5))bits=15;
     stroke_samples[(size_t)yy*width+xx]|=bits;
    }
    return true;
   };
   auto paint_corner=[&](Point prev,Point p,Point next,Matrix ma,Matrix mb)->bool{
    const double ax=p.x-prev.x,ay=p.y-prev.y,bx2=next.x-p.x,by2=next.y-p.y;
    const double al=std::hypot(ax,ay),bl=std::hypot(bx2,by2);
    if(!std::isfinite(al)||!std::isfinite(bl))return false;
    if(al<1e-10||bl<1e-10)return true;
    // Normalize before testing collinearity: the raw cross product
    // changes quadratically with segment length and loses small turns.
    const double ux0=ax/al,uy0=ay/al,ux1=bx2/bl,uy1=by2/bl;
    const double turn=ux0*uy1-uy0*ux1;
    const double alignment=ux0*ux1+uy0*uy1;
    if(!std::isfinite(turn)||!std::isfinite(alignment))return false;
    if(std::abs(turn)<1e-12&&alignment>0)return true;
    // A reversal has no finite miter intersection. Its caps are handled
    // by the stroke rasterizer; avoid producing an unbounded triangle.
    if(std::abs(turn)<1e-12)return true;
    const double side=turn>0?-1.0:1.0;
    // Compute each offset from its own segment CTM. The path points
    // are already transformed; the inverse maps each tangent back to
    // the user-space direction used to define stroke width.
    auto offset=[&](double tx,double ty,Matrix mat,Point& out)->bool{
     const double det=mat.a*mat.d-mat.b*mat.c;
     if(!std::isfinite(det)||std::abs(det)<1e-12)return false;
     const double ux=(mat.d*tx-mat.c*ty)/det;
     const double uy=(-mat.b*tx+mat.a*ty)/det;
     const double ul=std::hypot(ux,uy);
     if(!std::isfinite(ul)||ul<1e-12)return false;
     const double nx=side*(-uy/ul),ny=side*(ux/ul);
     const double half=line_width==0?0.5/std::max(sx,sy):line_width*.5;
     out={half*(mat.a*nx+mat.c*ny),half*(mat.b*nx+mat.d*ny)};
     return std::isfinite(out.x)&&std::isfinite(out.y);
    };
    Point oa{},ob{};
    if(!offset(ax,ay,ma,oa)||!offset(bx2,by2,mb,ob))return false;
    const double radius=std::max(std::hypot(oa.x,oa.y),std::hypot(ob.x,ob.y));
    if(radius<=0)return true;
    const Point u{p.x+oa.x,p.y+oa.y};
    const Point v{p.x+ob.x,p.y+ob.y};
    if(line_join==2)return paint_triangle(p,u,v);
    const double ux=ax/al,uy=ay/al,vx=bx2/bl,vy=by2/bl;
    const double det=ux*vy-uy*vx;
    // Near-parallel offset lines produce an unbounded miter. Use the
    // bevel fallback instead of relying on an arbitrary angle cutoff.
    if(!std::isfinite(det)||std::abs(det)<1e-14)return paint_triangle(p,u,v);
    const double t=((v.x-u.x)*vy-(v.y-u.y)*vx)/det;
    if(!std::isfinite(t))return paint_triangle(p,u,v);
    const Point tip{u.x+t*ux,u.y+t*uy};
    if(!std::isfinite(tip.x)||!std::isfinite(tip.y))return paint_triangle(p,u,v);
    // Miter limit is determined by the angle between the two
    // user-space tangents, not the Euclidean length after CTM.
    // For a turn angle theta, the miter/half-width ratio is
    // 1/cos(theta/2) = sqrt(2/(1+cos(theta))).
    // This also avoids numerical error from a distant intersection.
    double miter_ratio=0;
    if(ma.a==mb.a&&ma.b==mb.b&&ma.c==mb.c&&ma.d==mb.d){
     const double md=ma.a*ma.d-ma.b*ma.c;
     if(!std::isfinite(md)||std::abs(md)<1e-12)return false;
     const double iax=(ma.d*ax-ma.c*ay)/md;
     const double iay=(-ma.b*ax+ma.a*ay)/md;
     const double ibx=(ma.d*bx2-ma.c*by2)/md;
     const double iby=(-ma.b*bx2+ma.a*by2)/md;
     const double la=std::hypot(iax,iay),lb=std::hypot(ibx,iby);
     if(!std::isfinite(la)||!std::isfinite(lb)||la<=0||lb<=0)return false;
     // Normalize first: computing dot/(la*lb) can overflow for
     // finite but strongly anisotropic inverse-transformed tangents.
     const double nax=iax/la,nay=iay/la,nbx=ibx/lb,nby=iby/lb;
     const double cosine=std::clamp(nax*nbx+nay*nby,-1.0,1.0);
     if(!std::isfinite(cosine))return false;
     // Compare squared values to avoid a large intermediate miter ratio.
     const double denominator=1.0+cosine;
     if(denominator<=0||miter_limit*miter_limit*denominator<2.0)
      return paint_triangle(p,u,v);
     miter_ratio=std::sqrt(2.0/denominator);
    }else{
     // A path assembled under different CTMs has no single stroke
     // space. Use the existing device-space conservative fallback.
     miter_ratio=std::hypot(tip.x-p.x,tip.y-p.y)/radius;
    }
    if(!std::isfinite(miter_ratio)||miter_ratio>miter_limit)return paint_triangle(p,u,v);
    return paint_triangle(p,u,v)&&paint_triangle(u,tip,v);
   };
   if(line_join==0||line_join==2){
    {
     for(size_t k=0;k<polygons.size();++k){
      if(!dash_pattern.empty()&&std::find(fully_painted_closed_paths.begin(),fully_painted_closed_paths.end(),k)==fully_painted_closed_paths.end())continue;
      const auto& poly=polygons[k];
      const bool closed=poly.size()>3&&poly.front().x==poly.back().x&&poly.front().y==poly.back().y;
      for(size_t i=closed?0:1;i+1<poly.size();++i){
       const Point prev=i==0?poly[poly.size()-2]:poly[i-1];
       const Matrix ma=polygon_matrices[k][i==0?poly.size()-1:i],mb=polygon_matrices[k][i+1];
       if(!paint_corner(prev,poly[i],poly[i+1],ma,mb))return false;
      }
     }
    }
    for(const auto& join:dashed_joins)
     if(!paint_corner(join.prev,join.vertex,join.next,join.incoming,join.outgoing))return false;
   }
   for(size_t k=0;k<path.size();++k){const Rect& rect=path[k];
    Point p0{rect.x,rect.y},p1{rect.x+rect.w,rect.y},p2{rect.x+rect.w,rect.y+rect.h},p3{rect.x,rect.y+rect.h};
    if(!stroke_path(polygons.size()+k,{p0,p1,p2,p3,p0},std::vector<Matrix>(5,rect_matrices[k])))return false;
   }
   if(line_join==1&&dash_pattern.empty()){
    for(size_t k=0;k<path.size();++k){
     const Rect& rect=path[k];
     for(const Point p:{Point{rect.x,rect.y},Point{rect.x+rect.w,rect.y},Point{rect.x+rect.w,rect.y+rect.h},Point{rect.x,rect.y+rect.h}}){
      if(!segment(p,p,rect_matrices[k],true))return false;
     }
    }
   }
   for(size_t i=0;i<stroke_samples.size();++i){
    unsigned x=(unsigned)(i%width),y=(unsigned)(i/width);
    if(x<clip.x0||x>=clip.x1||y<clip.y0||y>=clip.y1||(clip_mask!=nullptr&&!(*clip_mask)[i]))continue;
    unsigned bits=stroke_samples[i],hits=(unsigned)((bits&1)!=0)+((bits&2)!=0)+((bits&4)!=0)+((bits&8)!=0);
    if(!hits)continue;
    unsigned alpha=(hits*255+2)/4;
    if(clip_mask!=nullptr)alpha=(alpha*(*clip_mask)[i]+127)/255;
    if(!alpha)continue;
    uint8_t* pixel=rgba+(size_t)y*stride+(size_t)x*4;
    pixel[0]=(uint8_t)((alpha*cr+(255-alpha)*pixel[0]+127)/255);
    pixel[1]=(uint8_t)((alpha*cg+(255-alpha)*pixel[1]+127)/255);
    pixel[2]=(uint8_t)((alpha*cb+(255-alpha)*pixel[2]+127)/255);
    pixel[3]=255;
   }
   path.clear();polygons.clear();polygon_matrices.clear();rect_matrices.clear();
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
    if(!coverage[i]||(clip_mask!=nullptr&&!(*clip_mask)[i])||x<clip.x0||x>=clip.x1||y<clip.y0||y>=clip.y1)continue;
    uint8_t* pixel=rgba+(size_t)y*stride+(size_t)x*4;
    unsigned alpha=antialias?coverage[i]:255;
    if(clip_mask!=nullptr)alpha=(alpha*(*clip_mask)[i]+127)/255;
    if(!alpha)continue;
    pixel[0]=(uint8_t)((alpha*cr+(255-alpha)*pixel[0]+127)/255);
    pixel[1]=(uint8_t)((alpha*cg+(255-alpha)*pixel[1]+127)/255);
    pixel[2]=(uint8_t)((alpha*cb+(255-alpha)*pixel[2]+127)/255);
    pixel[3]=255;
   }
   path.clear();polygons.clear();polygon_matrices.clear();rect_matrices.clear();
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
    if(clip_mask){
     if(!intersect_path_masks(mask,*clip_mask))return false;
    }
    clip_mask=std::make_shared<const std::vector<uint8_t>>(std::move(mask));
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
    if(clip_mask){
     if(!intersect_path_masks(mask,*clip_mask))return false;
    }
    clip_mask=std::make_shared<const std::vector<uint8_t>>(std::move(mask));
    pending_rect_clip=false;
   }
   path.clear();polygons.clear();polygon_matrices.clear();rect_matrices.clear();
  }else return false; // Do not silently ignore unknown operators.
 }
 return !reading_dash&&!dash_array_ready&&dash_values.empty()&&stack.empty()&&path.empty()&&polygons.empty()&&saved.empty()&&!pending_rect_clip&&!pending_path_clip;
}
bool render_rectangles(const uint8_t *contents,size_t size,unsigned width,unsigned height,uint8_t *rgba,size_t stride){
 return render_rectangles_box(contents,size,width,height,rgba,stride,{0,0,(double)width,(double)height});
}
}
