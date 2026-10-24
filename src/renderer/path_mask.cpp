#include "path_mask.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace rinpdf::renderer {
bool rasterize_path_mask(const std::vector<std::vector<MaskPoint>>& subpaths,
 unsigned width,unsigned height,FillRule rule,std::vector<uint8_t>& mask){
 mask.clear();
 if(!width||!height||(size_t)width*height>1048576)return false;
 if(subpaths.empty()){mask.assign((size_t)width*height,0);return true;}
 struct Edge {double x0,y0,x1,y1;int direction;};
 std::vector<Edge> edges;
 if(subpaths.size()>4096)return false;
 size_t total_vertices=0;
 for(const auto& path:subpaths){
  if(path.size()>65536-total_vertices)return false;
  total_vertices+=path.size();
  if(path.size()>4096)return false;
  // Validate even move-only points: degenerate geometry is still untrusted input.
  for(const auto& point:path)
   if(!std::isfinite(point.x)||!std::isfinite(point.y))return false;
  if(path.size()<2)continue;
  for(size_t i=0;i<path.size();++i){
   const auto& p=path[i];const auto& q=path[(i+1)%path.size()];
   if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(q.x)||!std::isfinite(q.y))return false;
   if(p.y==q.y)continue;
   if(edges.size()>=65536)return false;
   edges.push_back({p.x,p.y,q.x,q.y,q.y>p.y?1:-1});
  }
 }
 // No scanline outside the vertical extent of non-horizontal edges can
 // contribute coverage. The output remains initialized to transparent.
 double min_y=std::numeric_limits<double>::infinity();
 double max_y=-std::numeric_limits<double>::infinity();
 for(const auto& edge:edges){
  min_y=std::min(min_y,std::min(edge.y0,edge.y1));
  max_y=std::max(max_y,std::max(edge.y0,edge.y1));
 }
 const unsigned first_y=edges.empty()?0u:
  (unsigned)std::clamp(std::ceil(min_y-0.5),0.0,double(height));
 const unsigned last_y=edges.empty()?0u:
  (unsigned)std::clamp(std::ceil(max_y-0.5),0.0,double(height));
 std::vector<uint8_t> result((size_t)width*height,0);
 struct Crossing {double x;int direction;};
 size_t work=0;
 // Reuse crossing storage across scanlines; retain its capacity between
 // rows rather than reallocating on every nonempty row.
 std::vector<Crossing> hits;
 for(unsigned y=first_y;y<last_y;++y){
  const double scan=y+0.5;
  hits.clear();
  for(const auto& e:edges){
   if(++work>16777216)return false;
   if((e.y0<=scan&&scan<e.y1)||(e.y1<=scan&&scan<e.y0)){
    const double x=e.x0+(scan-e.y0)*(e.x1-e.x0)/(e.y1-e.y0);
    if(!std::isfinite(x))return false;
    hits.push_back({x,e.direction});
   }
  }
  std::sort(hits.begin(),hits.end(),[](const Crossing& a,const Crossing& b){return a.x<b.x;});
  int winding=0;
  for(size_t i=0;i+1<hits.size();++i){
   winding+=hits[i].direction;
   const bool inside=rule==FillRule::EvenOdd?(winding%2!=0):(winding!=0);
   if(!inside)continue;
   double left=std::clamp(hits[i].x,0.0,(double)width);
   double right=std::clamp(hits[i+1].x,0.0,(double)width);
   unsigned begin=(unsigned)std::clamp(std::ceil(left-0.5),0.0,(double)width);
   unsigned end=(unsigned)std::clamp(std::ceil(right-0.5),0.0,(double)width);
   for(unsigned x=begin;x<end;++x)result[(size_t)y*width+x]=1;
  }
 }
 mask=std::move(result);
 return true;
}
bool rasterize_path_coverage_2x(const std::vector<std::vector<MaskPoint>>& subpaths,
 unsigned width,unsigned height,FillRule rule,std::vector<uint8_t>& coverage){
 coverage.clear();
 if(!width||!height||(size_t)width*height>262144)return false;
 // Validate aggregate geometry before allocating a scaled copy.
 if(subpaths.size()>4096)return false;
 size_t total_vertices=0;
 for(const auto& path:subpaths){
  if(path.size()>4096||path.size()>65536-total_vertices)return false;
  total_vertices+=path.size();
 }
 std::vector<std::vector<MaskPoint>> scaled;
 scaled.reserve(subpaths.size());
 for(const auto& path:subpaths){
  auto& dest=scaled.emplace_back();
  dest.reserve(path.size());
  for(const auto& point:path){
   if(!std::isfinite(point.x)||!std::isfinite(point.y))return false;
   dest.push_back({point.x*2,point.y*2});
  }
 }
 std::vector<uint8_t> samples;
 if(!rasterize_path_mask(scaled,width*2,height*2,rule,samples))return false;
 std::vector<uint8_t> result((size_t)width*height,0);
 // Only the horizontal rows covered by validated path vertices can
 // contain samples; rasterize_path_mask has already validated all edges.
 double min_y=std::numeric_limits<double>::infinity();
 double max_y=-std::numeric_limits<double>::infinity();
 for(const auto& path:subpaths)for(const auto& point:path){
  min_y=std::min(min_y,point.y);
  max_y=std::max(max_y,point.y);
 }
 const unsigned first_y=std::isfinite(min_y)?
  unsigned(std::clamp(std::floor(min_y)-1.0,0.0,double(height))):0u;
 const unsigned last_y=std::isfinite(max_y)?
  unsigned(std::clamp(std::ceil(max_y)+1.0,0.0,double(height))):0u;
 for(unsigned y=first_y;y<last_y;++y)for(unsigned x=0;x<width;++x){
  size_t i=(size_t)(y*2)*(width*2)+x*2;
  unsigned sum=samples[i]+samples[i+1]+samples[i+width*2]+samples[i+width*2+1];
  result[(size_t)y*width+x]=(uint8_t)((sum*255+2)/4);
 }
 coverage=std::move(result);
 return true;
}
bool intersect_path_masks(std::vector<uint8_t>& current,const std::vector<uint8_t>& incoming){
 if(current.size()!=incoming.size())return false;
 for(size_t i=0;i<current.size();++i)current[i]=(uint8_t)((unsigned(current[i])*unsigned(incoming[i])+127)/255);
 return true;
}
}
