#pragma once
#include "sfnt.h"
#include "path_mask.h"
#include <algorithm>
#include <cstdint>
#include <list>
#include <iterator>
#include <map>
#include <vector>
namespace rinpdf::renderer {
struct CachedFontGlyph {
 uint16_t units=0,advance=0;
 SimpleGlyph outline;
 CffCommands cff_outline;
 std::vector<std::vector<MaskPoint>> flattened;
 bool has_flattened=false;
 double flattened_em=0,flattened_tolerance=0;
};
class GlyphLruCache {
public:
 std::vector<uint8_t> bytes;
 std::map<uint16_t,CachedFontGlyph> glyphs;
 size_t estimated_bytes=0;
 void clear(){
  glyphs.clear();positions_.clear();oldest_first_.clear();
  estimated_bytes=0;
 }
 void touch(uint16_t id){
  auto it=positions_.find(id);
  if(it==positions_.end()){
   oldest_first_.push_back(id);
   positions_.emplace(id,std::prev(oldest_first_.end()));
  }else oldest_first_.splice(oldest_first_.end(),oldest_first_,it->second);
 }
 static size_t path_weight(const std::vector<std::vector<MaskPoint>>& paths){
  size_t n=paths.capacity()*sizeof(std::vector<MaskPoint>);
  for(const auto& p:paths)n+=p.capacity()*sizeof(MaskPoint);
  return n;
 }
 static size_t glyph_weight(const CachedFontGlyph& g){
  return sizeof(CachedFontGlyph)+128+
   g.outline.points.capacity()*sizeof(GlyphPoint)+
   g.outline.contour_ends.capacity()*sizeof(uint16_t)+
   g.cff_outline.capacity()*sizeof(CffSegment)+path_weight(g.flattened);
 }
 void evict_for(size_t extra,size_t max_count,int protected_id){
  constexpr size_t budget=4*1024*1024;
  while(!glyphs.empty()&&(glyphs.size()>=max_count||
        extra>budget||estimated_bytes>budget-extra)){
   auto oldest=oldest_first_.begin();
   if(oldest==oldest_first_.end())break;
   if(int(*oldest)==protected_id)++oldest;
   if(oldest==oldest_first_.end())break;
   const uint16_t id=*oldest;
   auto victim=glyphs.find(id);
   if(victim!=glyphs.end()){
    const size_t weight=glyph_weight(victim->second);
    estimated_bytes-=std::min(estimated_bytes,weight);
    glyphs.erase(victim);
   }
   positions_.erase(id);
   oldest_first_.erase(oldest);
  }
 }
 void erase(uint16_t id){
  auto pos=positions_.find(id);
  if(pos!=positions_.end()){
   oldest_first_.erase(pos->second);
   positions_.erase(pos);
  }
  glyphs.erase(id);
 }
private:
 std::list<uint16_t> oldest_first_;
 std::map<uint16_t,std::list<uint16_t>::iterator> positions_;
};
}
