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
 std::list<uint16_t>::iterator lru_position{};
 bool lru_linked=false;
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
  glyphs_.clear();oldest_first_.clear();
  estimated_bytes_=0;
 }
 void touch(uint16_t id){
  auto it=glyphs_.find(id);
  if(it==glyphs_.end())return;
  auto& glyph=it->second;
  if(!glyph.lru_linked){
   oldest_first_.push_back(id);
   glyph.lru_position=std::prev(oldest_first_.end());
   glyph.lru_linked=true;
  }else oldest_first_.splice(oldest_first_.end(),oldest_first_,
                             glyph.lru_position);
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
    glyphs_.erase(victim);
    if(weight>estimated_bytes_)refresh_weight();
    else estimated_bytes_-=weight;
   }
   oldest_first_.erase(oldest);
  }
 }
 // Prepare bookkeeping before insertion so allocation failure never
 // evicts a valid entry or leaves a glyph without an LRU position.
 CachedFontGlyph* insert(uint16_t id,CachedFontGlyph glyph){
  auto existing=glyphs_.find(id);
  if(existing!=glyphs_.end()){
   touch(id);
   return &existing->second;
  }
  // Incoming glyphs may originate from another cache. Their list handles
  // must never be used with this cache's list.
  glyph.lru_linked=false;
  glyph.lru_position={};
  const size_t weight=glyph_weight(glyph);
  constexpr size_t budget=4*1024*1024;
  if(weight>budget)return nullptr;
  // Allocate the glyph node first. If LRU bookkeeping allocation fails,
  // erase only the new node; no resident accounting or eviction occurred.
  auto inserted=glyphs_.emplace(id,std::move(glyph));
  if(!inserted.second){
   touch(id);
   return &inserted.first->second;
  }
  try{
   touch(id);
  }catch(...){
   glyphs_.erase(inserted.first);
   throw;
  }
  estimated_bytes_+=weight;
  // The map and LRU indices are complete before any older glyph is evicted.
  evict_for(0,513,int(id));
  return &inserted.first->second;
 }
 void update_flattened(uint16_t id,
       std::vector<std::vector<MaskPoint>> replacement,
       double em,double tolerance){
  auto it=glyphs_.find(id);
  if(it==glyphs_.end())return;
  const size_t prior=path_weight(it->second.flattened);
  const size_t next=path_weight(replacement);
  // Swap is noexcept; make the new allocation resident before eviction.
  it->second.flattened.swap(replacement);
  it->second.has_flattened=true;
  it->second.flattened_em=em;
  it->second.flattened_tolerance=tolerance;
  // Apply the measured capacity delta; reconcile only on inconsistent state.
  if(prior>estimated_bytes_)refresh_weight();
  else{
   estimated_bytes_-=prior;
   if(next>SIZE_MAX-estimated_bytes_)refresh_weight();
   else estimated_bytes_+=next;
  }
  evict_for(0,513,int(id));
 }
 bool over_budget()const{return estimated_bytes_>4*1024*1024;}
 // Transfer the whole map node, including the allocated glyph payload.
 // No mapped-value move or second node allocation is needed.
 bool take_accounted(uint16_t id,std::map<uint16_t,CachedFontGlyph>& target){
  auto it=glyphs_.find(id);
  if(it==glyphs_.end()||target.find(id)!=target.end())return false;
  const size_t weight=glyph_weight(it->second);
  if(!it->second.lru_linked)return false;
  const auto lru_pos=it->second.lru_position;
  auto node=glyphs_.extract(it);
  // If the destination comparator throws, restore the extracted node.
  // LRU bookkeeping and byte accounting have not changed yet.
  try{
   auto inserted=target.insert(std::move(node));
   if(!inserted.inserted){
    glyphs_.insert(std::move(inserted.node));
    return false;
   }
   // The detached entry no longer owns this cache's list handle.
   inserted.position->second.lru_linked=false;
   inserted.position->second.lru_position={};
  }catch(...){
   if(!node.empty())glyphs_.insert(std::move(node));
   throw;
  }
  if(weight>estimated_bytes_)refresh_weight();
  else estimated_bytes_-=weight;
  oldest_first_.erase(lru_pos);
  return true;
 }
 void remove_accounted(uint16_t id){
  auto it=glyphs_.find(id);
  if(it==glyphs_.end())return;
  erase(id);
 }
 void erase(uint16_t id){
  if(auto it=glyphs_.find(id);it!=glyphs_.end()){
   if(it->second.lru_linked)oldest_first_.erase(it->second.lru_position);
   const size_t weight=glyph_weight(it->second);
   glyphs_.erase(it);
   if(weight>estimated_bytes_)refresh_weight();
   else estimated_bytes_-=weight;
  }
 }
private:
 void refresh_weight(){
  size_t total=0;
  for(const auto& entry:glyphs_){
   const size_t weight=glyph_weight(entry.second);
   if(weight>SIZE_MAX-total){total=SIZE_MAX;break;}
   total+=weight;
  }
  estimated_bytes_=total;
 }
 std::vector<uint8_t> bytes_;
 std::map<uint16_t,CachedFontGlyph> glyphs_;
 size_t estimated_bytes_=0;
 std::list<uint16_t> oldest_first_;
};
}
