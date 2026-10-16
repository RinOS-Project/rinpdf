#pragma once
#include "cubic_arc.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
#include <utility>
namespace rinpdf::renderer {
// An exact-arc-length dash span expressed in cubic parameter space.
// Carries the consumed dash phase back to the caller for connected segments.
struct CubicDashSpan {double t0,t1; bool painted;};
struct CubicDashCursor {size_t index=0;double remaining=0;};
inline bool cubic_dash_advance(const std::vector<double>& pattern,CubicDashCursor& state){
 const size_t n=pattern.size(),cycle=n*(n%2?2:1);
 if(!n)return false;
 for(size_t k=0;k<cycle;++k){
  state.index=(state.index+1)%cycle;
  state.remaining=pattern[state.index%n];
  if(state.remaining>0)return true;
 }
 return false;
}
inline bool cubic_dash_start(const std::vector<double>& pattern,double phase,CubicDashCursor& state){
 if(pattern.empty()||pattern.size()>32||!std::isfinite(phase)||phase<0)return false;
 double cycle_length=0;
 for(double v:pattern){
  if(!std::isfinite(v)||v<0||v>1000000)return false;
  cycle_length+=v;
 }
 if(!std::isfinite(cycle_length)||cycle_length<=0)return false;
 if(pattern.size()%2)cycle_length*=2;
 phase=std::fmod(phase,cycle_length);
 state={0,pattern[0]};
 const size_t cycle=pattern.size()*(pattern.size()%2?2:1);
 for(size_t i=0;i<=cycle&&phase>0;++i){
  if(state.remaining==0){if(!cubic_dash_advance(pattern,state))return false;continue;}
  const double step=std::min(state.remaining,phase);
  phase-=step;state.remaining-=step;
 }
 if(phase>0)return false;
 if(state.remaining<=0&&!cubic_dash_advance(pattern,state))return false;
 return true;
}
inline bool cubic_dash_spans(Point a,Point b,Point c,Point d,
 const std::vector<double>& pattern,CubicDashCursor& cursor,
 std::vector<CubicDashSpan>& spans){
 if(pattern.empty())return false;
 double length=0;
 if(!cubic_arc_length(a,b,c,d,length))return false;
 std::vector<CubicDashSpan> next;
 if(length==0){spans.clear();return true;}
 double distance=0,last_t=0;
 for(size_t operations=0;distance<length&&operations<65536;++operations){
  if(cursor.remaining<=0&&!cubic_dash_advance(pattern,cursor))return false;
  const double step=std::min(cursor.remaining,length-distance);
  if(!std::isfinite(step)||step<=0||distance+step==distance)return false;
  const double next_distance=distance+step;
  double t=1;
  if(next_distance<length&&!cubic_inverse_arc_length(a,b,c,d,next_distance,t))return false;
  if(t<last_t||t>1||!std::isfinite(t))return false;
  next.push_back({last_t,t,cursor.index%2==0});
  distance=next_distance;
  cursor.remaining-=step;
  last_t=t;
 }
 if(distance<length)return false;
 spans=std::move(next);
 return true;
}
struct LineDashSpan {double t0,t1;bool painted;};
// Share the same cursor for straight and cubic segments in one subpath.
inline bool line_dash_spans(Point a,Point b,const std::vector<double>& pattern,
 CubicDashCursor& cursor,std::vector<LineDashSpan>& spans){
 if(pattern.empty())return false;
 const double length=std::hypot(b.x-a.x,b.y-a.y);
 if(!std::isfinite(length))return false;
 std::vector<LineDashSpan> result;
 if(length==0){spans.clear();return true;}
 double distance=0;
 for(size_t n=0;n<65536&&distance<length;++n){
  if(cursor.remaining<=0&&!cubic_dash_advance(pattern,cursor))return false;
  const double step=std::min(cursor.remaining,length-distance);
  if(!std::isfinite(step)||step<=0||distance+step<=distance)return false;
  const double next=distance+step;
  result.push_back({distance/length,next/length,cursor.index%2==0});
  cursor.remaining-=step;
  distance=next;
 }
 if(distance<length)return false;
 spans=std::move(result);
 return true;
}

struct CubicDashSeam {
 bool first_painted=false;
 bool last_painted=false;
 bool joined=false;
};
// The terminal span (not the cursor's next phase) determines whether
// paint reaches the closing vertex. Exact dash boundaries leave a cap.
inline bool cubic_dash_closed_seam(
 const std::vector<double>& pattern, const CubicDashCursor& initial,
 const CubicDashCursor& terminal,
 const std::vector<bool>& segment_last_painted,
 const std::vector<bool>& segment_has_spans,
 CubicDashSeam& result){
 if(pattern.empty()||segment_last_painted.size()!=segment_has_spans.size())return false;
 if(initial.index>=pattern.size()*(pattern.size()%2?2:1)||
    terminal.index>=pattern.size()*(pattern.size()%2?2:1)||
    !std::isfinite(initial.remaining)||initial.remaining<0||
    !std::isfinite(terminal.remaining)||terminal.remaining<0)return false;
 CubicDashCursor probe=initial;
 if(probe.remaining==0&&!cubic_dash_advance(pattern,probe))return false;
 CubicDashSeam next;
 next.first_painted=probe.index%2==0;
 bool saw=false;
 for(size_t i=0;i<segment_has_spans.size();++i){
  if(segment_has_spans[i]){
   saw=true;
   next.last_painted=segment_last_painted[i];
  }
 }
 // A zero-perimeter contour has no incoming tangent and no seam join.
 next.joined=saw&&next.first_painted&&next.last_painted&&terminal.remaining>0;
 result=next;
 return true;
}

 // A painted cubic dash span is flattened adaptively in device space.
 // The first and last points remain exact inverse-arc-length boundaries.
inline bool cubic_dash_flatten_span(Point a,Point b,Point c,Point d,
 double t0,double t1,double scale_x,double scale_y,
 std::vector<Point>& output){
 if(!std::isfinite(t0)||!std::isfinite(t1)||t0<0||t1>1||t1<=t0||
    !std::isfinite(scale_x)||!std::isfinite(scale_y)||
    scale_x<=0||scale_y<=0)return false;
 for(Point point:{a,b,c,d})
  if(!std::isfinite(point.x)||!std::isfinite(point.y))return false;
 auto lerp=[](Point p,Point q,double t)->Point{
  return {p.x+(q.x-p.x)*t,p.y+(q.y-p.y)*t};
 };
 // Restrict the cubic to [t0,t1] via two de Casteljau splits.
 auto split=[&](Point p0,Point p1,Point p2,Point p3,double t,
                Point (&left)[4],Point (&right)[4]){
  const Point p01=lerp(p0,p1,t),p12=lerp(p1,p2,t),p23=lerp(p2,p3,t);
  const Point p012=lerp(p01,p12,t),p123=lerp(p12,p23,t);
  const Point mid=lerp(p012,p123,t);
  left[0]=p0;left[1]=p01;left[2]=p012;left[3]=mid;
  right[0]=mid;right[1]=p123;right[2]=p23;right[3]=p3;
 };
 Point left[4],right[4],range[4],unused[4];
 split(a,b,c,d,t0,left,right);
 const double relative=(t1-t0)/(1-t0);
 split(right[0],right[1],right[2],right[3],relative,range,unused);
 struct Node{Point p[4];unsigned depth;};
 std::vector<Node> pending;
 pending.push_back({{range[0],range[1],range[2],range[3]},0});
 std::vector<Point> result{range[0]};
 while(!pending.empty()){
  Node n=pending.back();pending.pop_back();
  const double dx=(n.p[3].x-n.p[0].x)*scale_x;
  const double dy=(n.p[3].y-n.p[0].y)*scale_y;
  const double len=std::hypot(dx,dy);
  const double ux=len>0?dx/len:0,uy=len>0?dy/len:0;
  double max_error=0;
  double previous_projection=0;
  for(unsigned j=1;j<=2;++j){
   const double vx=(n.p[j].x-n.p[0].x)*scale_x;
   const double vy=(n.p[j].y-n.p[0].y)*scale_y;
   const double projected=vx*ux+vy*uy;
   const double deviation=len>0?std::abs(vx*uy-vy*ux):std::hypot(vx,vy);
   if(!std::isfinite(projected)||!std::isfinite(deviation))return false;
   // Collinear controls may reverse order *inside* the chord without
   // exceeding its endpoints. Such a curve is not a straight segment.
   const double reversal=std::max(0.0,previous_projection-projected);
   max_error=std::max({max_error,deviation,reversal,
                       std::max(0.0,std::max(-projected,projected-len))});
   previous_projection=projected;
  }
  if(!std::isfinite(max_error))return false;
  if(max_error<=0.125){
   if(result.size()>=4096)return false;
   result.push_back(n.p[3]);continue;
  }
  if(n.depth>=12||pending.size()>=4094)return false;
  Point first[4],second[4];
  split(n.p[0],n.p[1],n.p[2],n.p[3],0.5,first,second);
  pending.push_back({{second[0],second[1],second[2],second[3]},n.depth+1});
  pending.push_back({{first[0],first[1],first[2],first[3]},n.depth+1});
 }
 // Force exact endpoints despite floating-point subdivision roundoff.
 result.front()=cubic_point(a,b,c,d,t0);
 result.back()=cubic_point(a,b,c,d,t1);
 // Even finite controls can overflow during interpolation or
 // device-space scaling. Never publish a non-finite raster path.
 for(const Point& point:result)
  if(!std::isfinite(point.x)||!std::isfinite(point.y))return false;
 output=std::move(result);
 return true;
}

}
