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
bool rasterize_path_coverage_region_2x(const std::vector<std::vector<MaskPoint>>& subpaths,
 unsigned width,unsigned height,FillRule rule,CoverageRegion& region){
 region={};
 if(!width||!height||(size_t)width*height>262144)return false;
 // Validate aggregate geometry before allocating a scaled copy.
 if(subpaths.size()>4096)return false;
 size_t total_vertices=0;
 for(const auto& path:subpaths){
  if(path.size()>4096||path.size()>65536-total_vertices)return false;
  total_vertices+=path.size();
 }
 // Restrict the 2x coverage surface to a conservative pixel-aligned
 // bounding rectangle. Keep the final output layout unchanged for callers.
 double min_x=std::numeric_limits<double>::infinity();
 double min_y=min_x,max_x=-min_x,max_y=-min_y;
 for(const auto& path:subpaths)for(const auto& point:path){
  if(!std::isfinite(point.x)||!std::isfinite(point.y))return false;
  min_x=std::min(min_x,point.x);max_x=std::max(max_x,point.x);
  min_y=std::min(min_y,point.y);max_y=std::max(max_y,point.y);
 }
 if(min_x!=std::numeric_limits<double>::infinity()){
  const unsigned x0=unsigned(std::clamp(std::floor(min_x)-1.0,0.0,double(width)));
  const unsigned y0=unsigned(std::clamp(std::floor(min_y)-1.0,0.0,double(height)));
  const unsigned x1=unsigned(std::clamp(std::ceil(max_x)+1.0,0.0,double(width)));
  const unsigned y1=unsigned(std::clamp(std::ceil(max_y)+1.0,0.0,double(height)));
  if(x0<x1&&y0<y1){
   const unsigned local_width=x1-x0,local_height=y1-y0;
   std::vector<uint8_t> result(size_t(local_width)*local_height,0);
   std::vector<std::vector<MaskPoint>> scaled;
   scaled.reserve(subpaths.size());
   for(const auto& path:subpaths){
    auto& dest=scaled.emplace_back();
    dest.reserve(path.size());
    for(const auto& point:path)
     dest.push_back({(point.x-double(x0))*2,(point.y-double(y0))*2});
   }
   std::vector<uint8_t> samples;
   if(!rasterize_path_mask(scaled,local_width*2,local_height*2,rule,samples))
    return false;
   const size_t sample_stride=size_t(local_width)*2;
   for(unsigned y=0;y<local_height;++y)
    for(unsigned x=0;x<local_width;++x){
     const size_t i=size_t(y*2)*sample_stride+x*2;
     const unsigned sum=samples[i]+samples[i+1]+
      samples[i+sample_stride]+samples[i+sample_stride+1];
     result[size_t(y)*local_width+x]=uint8_t((sum*255+2)/4);
    }
   region.x=x0;region.y=y0;region.width=local_width;
   region.height=local_height;region.pixels=std::move(result);
  }
 }
 return true;
}
bool rasterize_path_coverage_2x(const std::vector<std::vector<MaskPoint>>& paths,
 unsigned width,unsigned height,FillRule rule,std::vector<uint8_t>& coverage){
 coverage.clear();
 CoverageRegion region;
 if(!rasterize_path_coverage_region_2x(paths,width,height,rule,region))return false;
 std::vector<uint8_t> full(size_t(width)*height,0);
 for(unsigned y=0;y<region.height;++y)
  std::copy_n(region.pixels.data()+size_t(y)*region.width,region.width,
   full.data()+size_t(y+region.y)*width+region.x);
 coverage=std::move(full);
 return true;
}
bool intersect_path_masks(std::vector<uint8_t>& current,const std::vector<uint8_t>& incoming){
 if(current.size()!=incoming.size())return false;
 for(size_t i=0;i<current.size();++i)current[i]=(uint8_t)((unsigned(current[i])*unsigned(incoming[i])+127)/255);
 return true;
}
}
