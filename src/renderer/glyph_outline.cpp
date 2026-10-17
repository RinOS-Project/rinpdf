#include "glyph_outline.h"
#include <cmath>
#include <utility>
namespace rinpdf::renderer {
static MaskPoint midpoint(MaskPoint a,MaskPoint b){
 return {(a.x+b.x)*0.5,(a.y+b.y)*0.5};
}
static bool append_quadratic(MaskPoint a,MaskPoint control,MaskPoint b,
 double tolerance,std::vector<MaskPoint>& path){
 struct Segment{MaskPoint a,c,b;unsigned depth;};
 std::vector<Segment> pending{{a,control,b,0}};
 while(!pending.empty()){
  const Segment s=pending.back();pending.pop_back();
  const double deviation=std::hypot(s.c.x-(s.a.x+s.b.x)*0.5,
                                     s.c.y-(s.a.y+s.b.y)*0.5);
  if(!std::isfinite(deviation))return false;
  if(deviation<=tolerance){
   if(path.size()>=16384)return false;
   path.push_back(s.b);
   continue;
  }
  if(s.depth>=16||pending.size()>1024)return false;
  const MaskPoint left=midpoint(s.a,s.c),right=midpoint(s.c,s.b);
  const MaskPoint center=midpoint(left,right);
  pending.push_back({center,right,s.b,s.depth+1});
  pending.push_back({s.a,left,center,s.depth+1});
 }
 return true;
}
bool flatten_simple_glyph(const SimpleGlyph& glyph,double scale,
 double ox,double oy,double tolerance,
 std::vector<std::vector<MaskPoint>>& paths){
 if(!std::isfinite(scale)||scale<=0||scale>4096||
    !std::isfinite(ox)||!std::isfinite(oy)||
    !std::isfinite(tolerance)||tolerance<=0||tolerance>8||
    glyph.points.size()>16384||glyph.contour_ends.size()>1024)return false;
 std::vector<std::vector<MaskPoint>> result;
 size_t first=0;
 for(uint16_t last:glyph.contour_ends){
  if(last<first||size_t(last)>=glyph.points.size())return false;
  const size_t count=size_t(last)-first+1;
  if(count<2)return false;
  auto point=[&](size_t i)->MaskPoint{
   const auto& p=glyph.points[first+(i%count)];
   return {ox+double(p.x)*scale,oy-double(p.y)*scale};
  };
  auto on=[&](size_t i){return glyph.points[first+(i%count)].on_curve;};
  MaskPoint start;
  size_t cursor=0;
  if(on(0)){start=point(0);cursor=1;}
  else if(on(count-1)){start=point(count-1);cursor=0;}
  else {start=midpoint(point(count-1),point(0));cursor=0;}
  std::vector<MaskPoint> path{start};
  MaskPoint current=start;
  size_t processed=0;
  while(processed<count){
   const size_t i=cursor%count;
   if(on(i)){
    const MaskPoint next=point(i);
    if(path.size()>=16384)return false;
    path.push_back(next);current=next;
    ++cursor;++processed;
   }else{
    const MaskPoint control=point(i);
    MaskPoint end;
    if(processed+1<count){
     if(on(i+1)){end=point(i+1);cursor+=2;processed+=2;}
     else{end=midpoint(control,point(i+1));++cursor;++processed;}
    }else{end=start;++cursor;++processed;}
    if(!append_quadratic(current,control,end,tolerance,path))return false;
    current=end;
   }
  }
  if(!append_quadratic(current,midpoint(current,start),start,tolerance,path))
   return false;
  result.push_back(std::move(path));
  first=size_t(last)+1;
 }
 if(first!=glyph.points.size())return false;
 paths=std::move(result);
 return true;
}
}
