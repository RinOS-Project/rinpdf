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
 bool matches(const std::vector<uint8_t>& source)const{return bytes_==source;}
 void reset_font(const std::vector<uint8_t>& source){
  std::vector<uint8_t> copy(source);
  bytes_.swap(copy);clear();
 }
 CachedFontGlyph* find(uint16_t id){
  auto it=glyphs_.find(id);
  return it==glyphs_.end()?nullptr:&it->second;
 }
 void clear(){
  glyphs_.clear();positions_.clear();oldest_first_.clear();
  estimated_bytes_=0;
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
  while(!glyphs_.empty()&&(glyphs_.size()>=max_count||
        extra>budget||estimated_bytes_>budget-extra)){
   auto oldest=oldest_first_.begin();
   if(oldest==oldest_first_.end())break;
   if(int(*oldest)==protected_id)++oldest;
   if(oldest==oldest_first_.end())break;
   const uint16_t id=*oldest;
   auto victim=glyphs_.find(id);
   if(victim!=glyphs_.end()){
    const size_t weight=glyph_weight(victim->second);
    estimated_bytes_-=std::min(estimated_bytes_,weight);
    glyphs_.erase(victim);
   }
   positions_.erase(id);
   oldest_first_.erase(oldest);
  }
 }
 // Insert a decoded glyph with LRU and byte-accounting in one operation.
 // Returns end() if the single glyph cannot fit the retained cache.
 CachedFontGlyph* insert(uint16_t id,
                                                     CachedFontGlyph glyph){
  const size_t weight=glyph_weight(glyph);
  if(weight>4*1024*1024)return nullptr;
  evict_for(weight,512,-1);
  auto inserted=glyphs_.emplace(id,std::move(glyph));
  if(inserted.second){
   estimated_bytes_+=weight;
  }
  touch(id);
  return &inserted.first->second;
 }
 void update_flattened(uint16_t id,
       std::vector<std::vector<MaskPoint>> replacement,
       double em,double tolerance){
  auto it=glyphs_.find(id);
  if(it==glyphs_.end())return;
  const size_t prior=path_weight(it->second.flattened);
  const size_t next=path_weight(replacement);
  if(next>prior)evict_for(next-prior,513,int(id));
  estimated_bytes_-=std::min(estimated_bytes_,prior);
  estimated_bytes_+=next;
  it->second.flattened=std::move(replacement);
  it->second.has_flattened=true;
  it->second.flattened_em=em;
  it->second.flattened_tolerance=tolerance;
 }
 bool over_budget()const{return estimated_bytes_>4*1024*1024;}
 // Transfer the whole map node, including the allocated glyph payload.
 // No mapped-value move or second node allocation is needed.
 bool take_accounted(uint16_t id,std::map<uint16_t,CachedFontGlyph>& target){
  auto it=glyphs_.find(id);
  if(it==glyphs_.end()||target.find(id)!=target.end())return false;
  const size_t weight=glyph_weight(it->second);
  auto node=glyphs_.extract(it);
  auto inserted=target.insert(std::move(node));
  if(!inserted.inserted){
   glyphs_.insert(std::move(inserted.node));
   return false;
  }
  estimated_bytes_-=std::min(estimated_bytes_,weight);
  auto pos=positions_.find(id);
  if(pos!=positions_.end()){
   oldest_first_.erase(pos->second);
   positions_.erase(pos);
  }
  return true;
 }
 void remove_accounted(uint16_t id){
  auto it=glyphs_.find(id);
  if(it==glyphs_.end())return;
  const size_t weight=glyph_weight(it->second);
  estimated_bytes_-=std::min(estimated_bytes_,weight);
  erase(id);
 }
 void erase(uint16_t id){
  auto pos=positions_.find(id);
  if(pos!=positions_.end()){
   oldest_first_.erase(pos->second);
   positions_.erase(pos);
  }
  glyphs_.erase(id);
 }
private:
 std::vector<uint8_t> bytes_;
 std::map<uint16_t,CachedFontGlyph> glyphs_;
 size_t estimated_bytes_=0;
 std::list<uint16_t> oldest_first_;
 std::map<uint16_t,std::list<uint16_t>::iterator> positions_;
};
}
