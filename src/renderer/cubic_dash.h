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
 const std::vector<bool>& segment_last_painted,
 const std::vector<bool>& segment_has_spans,
 CubicDashSeam& result){
 if(pattern.empty()||segment_last_painted.size()!=segment_has_spans.size())return false;
 if(initial.index>=pattern.size()*(pattern.size()%2?2:1)||
    !std::isfinite(initial.remaining)||initial.remaining<0)return false;
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
 next.joined=saw&&next.first_painted&&next.last_painted;
 result=next;
 return true;
}

}
