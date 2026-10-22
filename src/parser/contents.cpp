#include "contents.h"
#include "xref.h"
#include "xref_stream.h"
#include "object.h"
#include "object_resolver.h"
#include "stream_object.h"
#include "stream.h"
#include "function.h"
#include "dct_image.h"
#include "ccitt_image.h"
#include "value.h"
#include "font_encoding.h"
#include "tounicode.h"
#include "../renderer/sfnt.h"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <cmath>
#include <tuple>
#include <array>
#include <sstream>
#include <charconv>
namespace rinpdf::parser {
namespace {
thread_local const ScopedPageXref* active_xref=nullptr;
}
ScopedPageXref::ScopedPageXref(const uint8_t* data,size_t size,
 const std::vector<XrefEntry>& entries,
 const std::vector<XrefStreamEntry>& compressed,const Value& trailer,
 VerifiedPageCounts* shared_counts)
 :previous_(active_xref),data_(data),size_(size),entries_(&entries),
  compressed_(&compressed),trailer_(&trailer),shared_counts_(shared_counts){
 active_xref=this;
}
ScopedPageXref::~ScopedPageXref(){active_xref=previous_;}
bool ScopedPageXref::read(const uint8_t* data,size_t size,
 std::vector<XrefEntry>& entries,std::vector<XrefStreamEntry>& compressed,
 Value& trailer){
 if(active_xref && active_xref->data_==data && active_xref->size_==size){
  entries=*active_xref->entries_;
  compressed=*active_xref->compressed_;
  trailer=*active_xref->trailer_;
  return true;
 }
 return read_object_xref(data,size,entries,compressed,trailer);
}

bool ScopedPageXref::cached_count(const uint8_t* data,size_t size,
 const Value& ref,size_t& count){
 if(!has_scope(data,size)||ref.kind!=Value::Kind::Reference)return false;
 const auto key=std::make_pair(ref.integer,int64_t(ref.generation));
 const auto local=active_xref->verified_counts_.find(key);
 if(local!=active_xref->verified_counts_.end()){count=local->second;return true;}
 if(!active_xref->shared_counts_)return false;
 std::lock_guard<std::mutex> lock(active_xref->shared_counts_->mutex);
 const auto it=active_xref->shared_counts_->counts.find(key);
 if(it==active_xref->shared_counts_->counts.end())return false;
 count=it->second;return true;
}
void ScopedPageXref::store_count(const uint8_t* data,size_t size,
 const Value& ref,size_t count){
 if(!has_scope(data,size)||ref.kind!=Value::Kind::Reference)return;
 const auto key=std::make_pair(ref.integer,int64_t(ref.generation));
 if(active_xref->verified_counts_.size()<100000)
  active_xref->verified_counts_.emplace(key,count);
 if(active_xref->shared_counts_){
  std::lock_guard<std::mutex> lock(active_xref->shared_counts_->mutex);
  if(active_xref->shared_counts_->counts.size()<100000)
   if(active_xref->shared_counts_->counts.emplace(key,count).second)
    active_xref->shared_counts_->resident_bytes+=VerifiedPageCounts::count_weight;
 }
}
bool ScopedPageXref::has_scope(const uint8_t* data,size_t size){
 return active_xref&&active_xref->data_==data&&active_xref->size_==size;
}
bool ScopedPageXref::get_cached_path(const uint8_t* data,size_t size,size_t target){
 if(!has_scope(data,size))return false;
 if(active_xref->path_attempted_&&active_xref->path_target_==target)
  return !active_xref->page_path_.empty();
 if(!active_xref->shared_counts_)return false;
 std::lock_guard<std::mutex> lock(active_xref->shared_counts_->mutex);
 const auto it=active_xref->shared_counts_->paths.find(target);
 if(it==active_xref->shared_counts_->paths.end())return false;
 active_xref->page_path_=it->second;
 active_xref->path_target_=target;
 active_xref->path_attempted_=true;
 active_xref->shared_counts_->path_ages[target]=
  ++active_xref->shared_counts_->path_clock;
 return !active_xref->page_path_.empty();
}
void ScopedPageXref::store_path(const uint8_t* data,size_t size,size_t target,
 const std::set<std::pair<int64_t,int64_t>>& path){
 if(!has_scope(data,size)||path.empty())return;
 active_xref->path_attempted_=true;
 active_xref->path_target_=target;
 active_xref->page_path_=path;
 if(!active_xref->shared_counts_)return;
 std::lock_guard<std::mutex> lock(active_xref->shared_counts_->mutex);
 auto& cache=*active_xref->shared_counts_;
 // Keep at most 128 paths and 64 reference nodes per path.
 if(path.size()>64)return;
 if(cache.paths.find(target)==cache.paths.end()&&cache.paths.size()>=128){
  const auto oldest=std::min_element(cache.path_ages.begin(),cache.path_ages.end(),
   [](const auto& a,const auto& b){return a.second<b.second;});
  if(oldest!=cache.path_ages.end()){
   const auto old_path=cache.paths.find(oldest->first);
   if(old_path!=cache.paths.end()){
    cache.resident_bytes-=VerifiedPageCounts::path_weight(old_path->second.size());
    cache.paths.erase(old_path);
   }
   cache.path_ages.erase(oldest);
  }
 }
 auto existing=cache.paths.find(target);
 if(existing!=cache.paths.end())
  cache.resident_bytes-=VerifiedPageCounts::path_weight(existing->second.size());
 cache.paths[target]=path;
 cache.resident_bytes+=VerifiedPageCounts::path_weight(path.size());
 cache.path_ages[target]=++cache.path_clock;
}
bool ScopedPageXref::path_contains(const uint8_t* data,size_t size,size_t target,
 const Value& ref){
 if(!get_cached_path(data,size,target))return true;
 if(ref.kind!=Value::Kind::Reference)return false;
 return active_xref->page_path_.count({ref.integer,int64_t(ref.generation)})!=0;
}
bool ScopedPageXref::resolve(const uint8_t* data,size_t size,
 const std::vector<XrefEntry>& entries,
 const std::vector<XrefStreamEntry>& compressed,const Value& ref,
 Value& out,std::map<size_t,Value>* compressed_cache){
 if(active_xref&&active_xref->data_==data&&active_xref->size_==size&&
    active_xref->entries_==&entries&&active_xref->compressed_==&compressed&&
    ref.kind==Value::Kind::Reference&&ref.integer>0){
  const auto key=std::make_pair(ref.integer,int64_t(ref.generation));
  const auto it=active_xref->objects_.find(key);
  if(it!=active_xref->objects_.end()){out=it->second;return true;}
  if(!resolve_reference_value(data,size,entries,compressed,ref,out,compressed_cache))
   return false;
  // Bound per-render memoization; unbounded dictionaries are not retained.
  if(active_xref->objects_.size()<2048)
   active_xref->objects_.emplace(key,out);
  return true;
 }
 return resolve_reference_value(data,size,entries,compressed,ref,out,compressed_cache);
}
bool ScopedPageXref::View::open(const uint8_t* data,size_t size){
 if(active_xref&&active_xref->data_==data&&active_xref->size_==size){
  entries=active_xref->entries_;
  compressed=active_xref->compressed_;
  trailer=active_xref->trailer_;
  return true;
 }
 if(!read_object_xref(data,size,owned_entries,owned_compressed,owned_trailer))return false;
 entries=&owned_entries;compressed=&owned_compressed;trailer=&owned_trailer;
 return true;
}
static const Value* get(const Value& v,const char* name){
 if(v.kind!=Value::Kind::Dictionary)return nullptr;
 auto it=v.dictionary.find(name);
 return it==v.dictionary.end()?nullptr:&it->second;
}

static bool box_value(const Value& value,MediaBox& box){
 if(value.kind!=Value::Kind::Array||value.array.size()!=4)return false;
 double v[4];
 for(size_t i=0;i<4;++i){
  if(value.array[i].kind!=Value::Kind::Integer)return false;
  v[i]=(double)value.array[i].integer;
  if(!std::isfinite(v[i])||std::abs(v[i])>10000000)return false;
 }
 if(v[2]<=v[0]||v[3]<=v[1])return false;
 box={v[0],v[1],v[2],v[3]};return true;
}
struct Reader {
 const uint8_t *data;size_t size;const std::vector<XrefEntry>& xref;const std::vector<XrefStreamEntry>& compressed;
 bool verified_subtree_count(const Value& ref,size_t& total) const {
  if(ScopedPageXref::cached_count(data,size,ref,total))return true;
  std::set<std::pair<int64_t,int64_t>> visiting;
  size_t visited=0;
  std::function<bool(const Value&,unsigned,size_t&)> walk_count=
   [&](const Value& item,unsigned depth,size_t& out)->bool{
    if(depth>64||++visited>100000||item.kind!=Value::Kind::Reference||
       item.integer<=0)return false;
    const auto key=std::make_pair(item.integer,int64_t(item.generation));
    if(!visiting.insert(key).second)return false;
    if(ScopedPageXref::cached_count(data,size,item,out)){
     visiting.erase(key);return true;
    }
    Value node;
    if(!object(item,node))return false;
    const Value* kind=get(node,"Type");
    if(!kind||kind->kind!=Value::Kind::Name)return false;
    if(kind->text=="Page"){
     out=1;visiting.erase(key);
     ScopedPageXref::store_count(data,size,item,out);
     return true;
    }
    if(kind->text!="Pages")return false;
    const Value* children=get(node,"Kids");
    const Value* declared=get(node,"Count");
    if(!children||children->kind!=Value::Kind::Array||
       !declared||declared->kind!=Value::Kind::Integer||
       declared->integer<=0)return false;
    size_t sum=0;
    for(const Value& child:children->array){
     size_t n=0;
     if(!walk_count(child,depth+1,n)||n>SIZE_MAX-sum)return false;
     sum+=n;
    }
    if(uint64_t(declared->integer)!=uint64_t(sum))return false;
    out=sum;visiting.erase(key);
    ScopedPageXref::store_count(data,size,item,out);
    return true;
   };
  return walk_count(ref,0,total);
 }
 bool skip_preceding_subtree(const Value& ref,size_t& current,size_t target) const {
  if(ScopedPageXref::get_cached_path(data,size,target)||current>target||
     ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  Value node;
  if(!object(ref,node))return false;
  const Value* kind=get(node,"Type");
  const Value* count=get(node,"Count");
  if(!kind||kind->kind!=Value::Kind::Name||kind->text!="Pages"||
     !count||count->kind!=Value::Kind::Integer||count->integer<=0)return false;
  const uint64_t pages=uint64_t(count->integer);
  if(pages>uint64_t(target-current))return false;
  size_t verified=0;
  if(!verified_subtree_count(ref,verified)||verified!=size_t(pages))return false;
  current+=verified;
  return true;
 }
 void prepare_path(const Value& root,size_t target,size_t& current) const {
  if(!ScopedPageXref::has_scope(data,size))return;
  if(ScopedPageXref::get_cached_path(data,size,target)){current=target;return;}
  std::set<std::pair<int64_t,int64_t>> active,path;
  size_t count=0,visited=0;
  std::function<bool(const Value&,unsigned)> visit=[&](const Value& ref,unsigned depth){
   if(depth>64||++visited>100000||ref.kind!=Value::Kind::Reference||ref.integer<=0)
    return false;
   const auto key=std::make_pair(ref.integer,int64_t(ref.generation));
   if(!active.insert(key).second)return false;
   Value node;
   if(!object(ref,node))return false;
   const Value* kind=get(node,"Type");
   if(!kind||kind->kind!=Value::Kind::Name)return false;
   bool selected=false;
   if(kind->text=="Page"){
    selected=(count==target);
    ++count;
   }else if(kind->text=="Pages"){
    const Value* kids=get(node,"Kids");
    if(!kids||kids->kind!=Value::Kind::Array)return false;
    const size_t path_before=path.size();
    for(const Value& child:kids->array){
     size_t verified=0;
     if(count<=target&&verified_subtree_count(child,verified)&&
        verified<=target-count){
      count+=verified;
      continue;
     }
     if(!visit(child,depth+1))return false;
     // Once the target leaf has been found, no later sibling can
     // contribute to its ancestry.
     if(path.size()>path_before)break;
    }
    selected=path.size()>path_before;
   }else return false;
   if(selected)path.insert(key);
   active.erase(key);
   return true;
  };
  if(visit(root,0)&&count>target&&!path.empty()){
   ScopedPageXref::store_path(data,size,target,path);
   current=target;
  }
 }

 mutable std::map<size_t,Value> compressed_cache;
 mutable std::map<std::tuple<int64_t,int64_t,unsigned>,PdfFunction> compiled_function_cache;
 mutable std::map<std::tuple<int64_t,int64_t,unsigned,unsigned>,std::array<double,4>> calculator_result_cache;
 bool entry(const Value& ref,const XrefEntry*& out) const {
  if(ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  auto it=std::find_if(xref.begin(),xref.end(),[&](const XrefEntry& e){
   return e.in_use&&e.object==(size_t)ref.integer&&e.generation==ref.generation;
  });
  if(it==xref.end())return false;
  out=&*it;
  return true;
 }
 bool object(const Value& ref,Value& out) const {
  return ScopedPageXref::resolve(data,size,xref,compressed,ref,out,&compressed_cache);
 }
 bool stream(const Value& ref,std::vector<uint8_t>& bytes,size_t limit) const {
  const XrefEntry *e=nullptr;if(!entry(ref,e))return false;
  StreamRange range;
  if(!locate_indirect_stream(data,size,*e,xref,range))return false;
  return decode_stream(data,size,range,bytes,limit);
 }
 bool resolve_unicode_cmap(const Value& ref,unsigned depth,
  std::set<size_t>& visiting,std::map<CMapKey,std::u32string>& output,
  std::vector<std::pair<CMapKey,CMapKey>>& spaces,
  std::string& cmap_name,const Value* page_resources=nullptr) const {
  if(depth>=8||ref.kind!=Value::Kind::Reference||ref.integer<=0||
     !visiting.insert(size_t(ref.integer)).second)return false;
  // Ordinary object resolution intentionally rejects stream objects.
  // Resolve the indirect stream header to obtain its dictionary and data.
  const XrefEntry* entry_ptr=nullptr;
  if(!entry(ref,entry_ptr))return false;
  StreamRange range;
  if(!locate_indirect_stream(data,size,*entry_ptr,xref,range))return false;
  Value dictionary=range.dictionary;
  std::vector<uint8_t> decoded;
  if(!decode_stream(data,size,range,decoded,1024*1024))return false;
  if(const Value* name=get(dictionary,"CMapName")){
   if(name->kind!=Value::Kind::Name||name->text.size()>128)return false;
   cmap_name=name->text;
  }
  const Value* use=get(dictionary,"UseCMap");
  std::map<CMapKey,std::u32string> parent;
  std::vector<std::pair<CMapKey,CMapKey>> parent_spaces;
  std::string parent_name;
  if(use){
   Value parent_ref=*use;
   if(use->kind==Value::Kind::Name){
    // Only an explicitly declared CMap resource can satisfy a name;
    // never guess a built-in CMap from the requested string.
    const Value* resources=get(dictionary,"Resources");
    Value resolved_resources;
    if(!resources)resources=page_resources;
    if(!resources)return false;
    if(resources->kind==Value::Kind::Reference){
     if(!object(*resources,resolved_resources))return false;
    }else resolved_resources=*resources;
    const Value* cmap_resources=get(resolved_resources,"CMap");
    Value cmap_dictionary;
    if(!cmap_resources)return false;
    if(cmap_resources->kind==Value::Kind::Reference){
     if(!object(*cmap_resources,cmap_dictionary))return false;
    }else cmap_dictionary=*cmap_resources;
    if(cmap_dictionary.kind!=Value::Kind::Dictionary)return false;
    const auto it=cmap_dictionary.dictionary.find(use->text);
    if(it==cmap_dictionary.dictionary.end()||
       it->second.kind!=Value::Kind::Reference)return false;
    parent_ref=it->second;
   }
   if(!resolve_unicode_cmap(parent_ref,depth+1,visiting,parent,
        parent_spaces,parent_name,page_resources))return false;
   if(parent_name.empty()||
      (use->kind==Value::Kind::Name&&parent_name!=use->text))return false;
  }
  std::map<CMapKey,std::u32string> result;
  std::vector<std::pair<CMapKey,CMapKey>> ranges;
  if(!parse_tounicode_sequences(decoded.data(),decoded.size(),result,&ranges,
       parent_name,use?&parent:nullptr,use?&parent_spaces:nullptr))return false;
  // A stream dictionary UseCMap may identify a parent even if the stream
  // does not contain an inline usecmap operator.
  if(use){
   for(const auto& [key,value]:parent){
    if(result.size()>=4096&&!result.count(key))return false;
    result.emplace(key,value);
   }
   if(ranges.empty())ranges=parent_spaces;
   else if(!parent_spaces.empty()){
    // A child's codespace must not change its inherited byte boundaries.
    if(ranges.size()!=parent_spaces.size())return false;
    for(size_t i=0;i<ranges.size();++i){
     const auto& a=ranges[i];const auto& b=parent_spaces[i];
     if(a.first.width!=b.first.width||a.first.code!=b.first.code||
        a.second.width!=b.second.width||a.second.code!=b.second.code)
      return false;
    }
   }
   if(!ranges.empty())for(const auto& [key,value]:result){
    bool covered=false;
    for(const auto& range:ranges)
     if(key.width==range.first.width&&key.code>=range.first.code&&
        key.code<=range.second.code){covered=true;break;}
    if(!covered)return false;
   }
  }
  output=std::move(result);spaces=std::move(ranges);
  visiting.erase(size_t(ref.integer));
  return true;
 }
 bool decode_page_image(const StreamRange& range,PageImage& image) const {
      const Value* w=get(range.dictionary,"Width");
      const Value* h=get(range.dictionary,"Height");
      const Value* bits=get(range.dictionary,"BitsPerComponent");
      const Value* cs=get(range.dictionary,"ColorSpace");
      const Value* mask_flag=get(range.dictionary,"ImageMask");
      if(mask_flag){
       if(mask_flag->kind!=Value::Kind::Boolean)return false;
       if(mask_flag->boolean){
        if(!w||!h||w->kind!=Value::Kind::Integer||
           h->kind!=Value::Kind::Integer||
           w->integer<=0||h->integer<=0||
           w->integer>4096||h->integer>4096||cs||
           get(range.dictionary,"SMask")||get(range.dictionary,"Mask")||
           get(range.dictionary,"DecodeParms")||
           (bits&&(bits->kind!=Value::Kind::Integer||bits->integer!=1)))
         return false;
        bool invert=false;
        if(const Value* decode=get(range.dictionary,"Decode")){
         if(decode->kind!=Value::Kind::Array||decode->array.size()!=2||
            decode->array[0].kind!=Value::Kind::Integer||
            decode->array[1].kind!=Value::Kind::Integer)return false;
         if(decode->array[0].integer==1&&decode->array[1].integer==0)
          invert=true;
         else if(decode->array[0].integer!=0||decode->array[1].integer!=1)
          return false;
        }
        const size_t width=size_t(w->integer),height=size_t(h->integer);
        const size_t pitch=(width+7)/8;
        std::vector<uint8_t> packed;
        if(!decode_stream(data,size,range,packed,pitch*height)||
           packed.size()!=pitch*height)return false;
        image=PageImage{};
        image.width=unsigned(width);image.height=unsigned(height);
        image.channels=1;image.stencil=true;
        image.pixels.assign(width*height,0);
        for(size_t y=0;y<height;++y)
         for(size_t x=0;x<width;++x){
          const bool bit=(packed[y*pitch+x/8]>>(7-(x%8)))&1;
          image.pixels[y*width+x]=uint8_t(bit!=invert?255:0);
         }
        return true;
       }
      }
      if(!w||!h||!bits||!cs||
         w->kind!=Value::Kind::Integer||h->kind!=Value::Kind::Integer||
         bits->kind!=Value::Kind::Integer||
         (bits->integer!=8&&bits->integer!=4&&
          bits->integer!=2&&bits->integer!=1)||
         w->integer<=0||h->integer<=0||
         w->integer>4096||h->integer>4096||
         (cs->kind!=Value::Kind::Name&&cs->kind!=Value::Kind::Array)){return false;}
      bool indexed=false;
      unsigned icc_channels=0;
      bool separation=false;
      unsigned device_inputs=1;
      bool sampled_tint=false;
      PdfFunction compiled_tint;
      bool recursive_stitch=false;
      Value stitch_function;
      std::vector<uint8_t> tint_samples;
      std::vector<unsigned> tint_sizes;
      std::vector<double> tint_encode,tint_decode,tint_domain,tint_range;
      unsigned tint_sample_bits=8;
      unsigned tint_channels=0;
      double tint_c0[4]{0,0,0,0},tint_c1[4]{1,1,1,1},tint_n=1;
      unsigned calibrated=0;
      bool lab=false;
      double lab_range[4]{-100,100,-100,100};
      double cal_gamma[3]{1,1,1};
      double cal_matrix[9]{1,0,0,0,1,0,0,0,1};
      // Initially support D65-calibrated spaces with the default zero
      // BlackPoint. Other illuminants require chromatic adaptation.
      if(cs->kind==Value::Kind::Array&&cs->array.size()==2&&
         cs->array[0].kind==Value::Kind::Name&&
         (cs->array[0].text=="CalGray"||cs->array[0].text=="CalRGB")){
       calibrated=cs->array[0].text=="CalGray"?1:3;
       if(cs->array[1].kind!=Value::Kind::Dictionary)return false;
       const Value& dict=cs->array[1];
       auto read_number=[](const Value& v,double& x){
        if(v.kind==Value::Kind::Integer)x=double(v.integer);
        else if(v.kind==Value::Kind::Real)x=v.real;
        else return false;
        return std::isfinite(x);
       };
       const Value* white=get(dict,"WhitePoint");
       if(!white||white->kind!=Value::Kind::Array||
          white->array.size()!=3)return false;
       double wp[3]{};
       for(unsigned i=0;i<3;++i)
        if(!read_number(white->array[i],wp[i]))return false;
       if(std::abs(wp[0]-0.95047)>0.002||
          std::abs(wp[1]-1)>0.002||
          std::abs(wp[2]-1.08883)>0.002)return false;
       if(const Value* black=get(dict,"BlackPoint")){
        if(black->kind!=Value::Kind::Array||black->array.size()!=3)
         return false;
        for(const auto& val:black->array){
         double v=0;
         if(!read_number(val,v)||std::abs(v)>1e-8)return false;
        }
       }
       if(const Value* gamma=get(dict,"Gamma")){
        if(calibrated==1){
         if(!read_number(*gamma,cal_gamma[0]))return false;
        }else{
         if(gamma->kind!=Value::Kind::Array||gamma->array.size()!=3)
          return false;
         for(unsigned i=0;i<3;++i)
          if(!read_number(gamma->array[i],cal_gamma[i]))return false;
        }
       }
       for(unsigned i=0;i<calibrated;++i)
        if(cal_gamma[i]<=0||cal_gamma[i]>16)return false;
       if(const Value* matrix=get(dict,"Matrix")){
        if(calibrated!=3||matrix->kind!=Value::Kind::Array||
           matrix->array.size()!=9)return false;
        for(unsigned i=0;i<9;++i)
         if(!read_number(matrix->array[i],cal_matrix[i])||
            std::abs(cal_matrix[i])>16)return false;
       }else if(calibrated==3){
        // CalRGB identity is XYZ, not device RGB. It is converted below.
       }
      }
      if(cs->kind==Value::Kind::Array&&cs->array.size()==2&&
         cs->array[0].kind==Value::Kind::Name&&cs->array[0].text=="Lab"){
       if(cs->array[1].kind!=Value::Kind::Dictionary)return false;
       const Value& info=cs->array[1];
       auto number=[](const Value& v,double& out){
        if(v.kind==Value::Kind::Integer)out=double(v.integer);
        else if(v.kind==Value::Kind::Real)out=v.real;
        else return false;
        return std::isfinite(out);
       };
       const Value* wp=get(info,"WhitePoint");
       if(!wp||wp->kind!=Value::Kind::Array||wp->array.size()!=3)
        return false;
       const double d65[3]{0.95047,1,1.08883};
       for(unsigned i=0;i<3;++i){
        double v=0;
        if(!number(wp->array[i],v)||std::abs(v-d65[i])>0.002)
         return false;
       }
       if(const Value* bp=get(info,"BlackPoint")){
        if(bp->kind!=Value::Kind::Array||bp->array.size()!=3)
         return false;
        for(const auto& v:bp->array){
         double value=0;
         if(!number(v,value)||std::abs(value)>1e-8)return false;
        }
       }
       if(const Value* range=get(info,"Range")){
        if(range->kind!=Value::Kind::Array||range->array.size()!=4)
         return false;
        for(unsigned i=0;i<4;++i)
         if(!number(range->array[i],lab_range[i])||
            std::abs(lab_range[i])>1000)return false;
        if(lab_range[0]>=lab_range[1]||lab_range[2]>=lab_range[3])
         return false;
       }
       lab=true;
      }
      if(cs->kind==Value::Kind::Array&&
         (cs->array.size()==4||cs->array.size()==5)&&
         cs->array[0].kind==Value::Kind::Name&&
         (cs->array[0].text=="Separation"||
          cs->array[0].text=="DeviceN")){
       const bool device_n=cs->array[0].text=="DeviceN";
       if(device_n){
        if(cs->array[1].kind!=Value::Kind::Array||
           cs->array[1].array.empty()||
           cs->array[1].array.size()>4)return false;
        device_inputs=unsigned(cs->array[1].array.size());
        for(const Value& name:cs->array[1].array)
         if(name.kind!=Value::Kind::Name)return false;
        if(cs->array.size()==5&&
           cs->array[4].kind!=Value::Kind::Dictionary)return false;
       }else if(cs->array.size()!=4||
                cs->array[1].kind!=Value::Kind::Name)return false;
       if(cs->array[2].kind!=Value::Kind::Name)return false;
       const std::string& alternate=cs->array[2].text;
       tint_channels=alternate=="DeviceGray"?1:
                     alternate=="DeviceRGB"?3:
                     alternate=="DeviceCMYK"?4:0;
       if(!tint_channels)return false;
       Value fn;
       if(cs->array[3].kind==Value::Kind::Reference){
        const XrefEntry* function_entry=nullptr;
        if(!entry(cs->array[3],function_entry))return false;
        StreamRange function_stream;
        if(locate_indirect_stream(data,size,*function_entry,xref,
                                  function_stream))
         fn=function_stream.dictionary;
        else if(!object(cs->array[3],fn))return false;
       }else fn=cs->array[3];
       const Value* type=get(fn,"FunctionType");
       const Value* domain=get(fn,"Domain");
       const Value* exponent=get(fn,"N");
       const Value* c0=get(fn,"C0");
       const Value* c1=get(fn,"C1");
       auto number=[](const Value& v,double& out){
        if(v.kind==Value::Kind::Integer)out=double(v.integer);
        else if(v.kind==Value::Kind::Real)out=v.real;
        else return false;
        return std::isfinite(out)&&std::abs(out)<=100000;
       };
       if(!type||type->kind!=Value::Kind::Integer)return false;
       if(type->integer==0){
        // Bounded sampled functions with multilinear interpolation.
        if(cs->array[3].kind!=Value::Kind::Reference)return false;
        const XrefEntry* entry_ptr=nullptr;
        if(!entry(cs->array[3],entry_ptr))return false;
        StreamRange function_range;
        if(!locate_indirect_stream(data,size,*entry_ptr,xref,function_range))
         return false;
        const Value* sizes=get(function_range.dictionary,"Size");
        const Value* bps=get(function_range.dictionary,"BitsPerSample");
        const Value* order=get(function_range.dictionary,"Order");
        // Reject interpolation orders outside the supported 1 and 3.
        if(order&&(order->kind!=Value::Kind::Integer||
                   (order->integer!=1&&order->integer!=3)))
         return false;
        const Value* range=get(function_range.dictionary,"Range");
        const Value* encode=get(function_range.dictionary,"Encode");
        const Value* decode=get(function_range.dictionary,"Decode");
        if(!sizes||sizes->kind!=Value::Kind::Array||
           sizes->array.size()!=device_inputs||
           !bps||bps->kind!=Value::Kind::Integer||
           (bps->integer!=1&&bps->integer!=2&&bps->integer!=4&&
            bps->integer!=8&&bps->integer!=12&&bps->integer!=16&&
            bps->integer!=24&&bps->integer!=32)||
           !range||range->kind!=Value::Kind::Array||
           range->array.size()!=tint_channels*2||
           !domain||domain->kind!=Value::Kind::Array||
           domain->array.size()!=device_inputs*2)return false;
        tint_sample_bits=unsigned(bps->integer);
        size_t sample_count=tint_channels;
        for(unsigned i=0;i<device_inputs;++i){
         const Value& v=sizes->array[i];
         if(v.kind!=Value::Kind::Integer||v.integer<2||v.integer>64)
          return false;
         tint_sizes.push_back(unsigned(v.integer));
         if(sample_count>4*1024*1024/unsigned(v.integer))return false;
         sample_count*=unsigned(v.integer);
         double d0=0,d1=0;
         if(!number(domain->array[2*i],d0)||
            !number(domain->array[2*i+1],d1)||d0>=d1||
            std::abs(d0)>100000||std::abs(d1)>100000)return false;
         tint_domain.push_back(d0);
         tint_domain.push_back(d1);
        }
        for(unsigned c=0;c<tint_channels;++c){
         double a=0,b=0;
         if(!number(range->array[2*c],a)||
            !number(range->array[2*c+1],b)||a>=b||
            std::abs(a)>100000||std::abs(b)>100000)return false;
         tint_range.push_back(a);
         tint_range.push_back(b);
        }
        tint_encode.resize(device_inputs*2);
        tint_decode.resize(tint_channels*2);
        if(encode&&(encode->kind!=Value::Kind::Array||
                    encode->array.size()!=device_inputs*2))return false;
        if(decode&&(decode->kind!=Value::Kind::Array||
                    decode->array.size()!=tint_channels*2))return false;
        for(unsigned i=0;i<device_inputs;++i){
         tint_encode[i*2]=0;
         tint_encode[i*2+1]=double(tint_sizes[i]-1);
         if(encode&&(!number(encode->array[2*i],tint_encode[2*i])||
                     !number(encode->array[2*i+1],tint_encode[2*i+1])))
          return false;
        }
        for(unsigned c=0;c<tint_channels;++c){
         tint_decode[2*c]=tint_range[2*c];
         tint_decode[2*c+1]=tint_range[2*c+1];
         if(decode&&(!number(decode->array[2*c],tint_decode[2*c])||
                     !number(decode->array[2*c+1],tint_decode[2*c+1])))
          return false;
        }
        const uint64_t bit_count=uint64_t(sample_count)*tint_sample_bits;
        const uint64_t byte_count=(bit_count+7)/8;
        if(byte_count>4*1024*1024)return false;
        if(!decode_stream(data,size,function_range,tint_samples,
                          size_t(byte_count))||
           tint_samples.size()!=size_t(byte_count))return false;
        MultidimensionalSampledFunction sampled_parameters;
       sampled_parameters.inputs=device_inputs;
       sampled_parameters.outputs=tint_channels;
       sampled_parameters.bits=tint_sample_bits;
       sampled_parameters.order=order?unsigned(order->integer):1;
       for(unsigned d=0;d<device_inputs;++d){
        sampled_parameters.sizes[d]=tint_sizes[d];
        sampled_parameters.domain[2*d]=tint_domain[2*d];
        sampled_parameters.domain[2*d+1]=tint_domain[2*d+1];
        sampled_parameters.encode[2*d]=tint_encode[2*d];
        sampled_parameters.encode[2*d+1]=tint_encode[2*d+1];
       }
       for(unsigned c=0;c<tint_channels;++c){
        sampled_parameters.decode[2*c]=tint_decode[2*c];
        sampled_parameters.decode[2*c+1]=tint_decode[2*c+1];
        sampled_parameters.range[2*c]=tint_range[2*c];
        sampled_parameters.range[2*c+1]=tint_range[2*c+1];
       }
       if(!compile_multidimensional_pdf_function(
            sampled_parameters,tint_samples,compiled_tint))return false;
       sampled_tint=true;
       }else if(type->integer==4){
        if(device_inputs!=1)return false;
        double probe[4]{};
        if(!eval_tint_function(cs->array[3],0,tint_channels,probe)||
           !eval_tint_function(cs->array[3],1,tint_channels,probe))
         return false;
        stitch_function=cs->array[3];
        recursive_stitch=true;
      }else if(type->integer==3){
        if(device_inputs!=1)return false;
        double boundary[4]{};
        // Probe all four tint endpoints to validate the function hierarchy.
        if(!eval_tint_function(cs->array[3],0,tint_channels,boundary)||
           !eval_tint_function(cs->array[3],1,tint_channels,boundary))
         return false;
        stitch_function=cs->array[3];
        recursive_stitch=true;
       }else if(type->integer==2){
        if(device_inputs!=1||!domain||domain->kind!=Value::Kind::Array||
           domain->array.size()!=2||!exponent||
           !number(*exponent,tint_n)||tint_n<=0||tint_n>100)
         return false;
        double d0=0,d1=0;
        if(!number(domain->array[0],d0)||!number(domain->array[1],d1)||
           d0!=0||d1!=1)return false;
        for(const auto& setting:std::initializer_list<std::pair<const Value*,double*>>{
              {c0,tint_c0},{c1,tint_c1}}){
         if(!setting.first)continue;
         if(setting.first->kind!=Value::Kind::Array||
            setting.first->array.size()!=tint_channels)return false;
         for(unsigned c=0;c<tint_channels;++c)
          if(!number(setting.first->array[c],setting.second[c])||
             setting.second[c]<0||setting.second[c]>1)return false;
        }
       }else return false;
       // Both Separation and one-colorant DeviceN share the same
       // one-input Type 2 tint evaluation into the alternate space.
       separation=true;
      }
      std::string palette;
      unsigned maximum_index=0;
      unsigned palette_channels=0;
      if(cs->kind==Value::Kind::Array&&cs->array.size()==2&&
         cs->array[0].kind==Value::Kind::Name&&
         cs->array[0].text=="ICCBased"){
       // Without an ICC color engine, use only an explicitly specified
       // PDF Alternate. Never silently treat unknown profiles as sRGB.
       if(cs->array[1].kind!=Value::Kind::Reference)return false;
       const XrefEntry* icc_entry=nullptr;
       if(!entry(cs->array[1],icc_entry))return false;
       StreamRange icc_range;
       if(!locate_indirect_stream(data,size,*icc_entry,xref,icc_range))
        return false;
       const Value* n=get(icc_range.dictionary,"N");
       const Value* alternate=get(icc_range.dictionary,"Alternate");
       if(!n||n->kind!=Value::Kind::Integer||
          !alternate||alternate->kind!=Value::Kind::Name)return false;
       icc_channels=alternate->text=="DeviceGray"?1:
                    alternate->text=="DeviceRGB"?3:
                    alternate->text=="DeviceCMYK"?4:0;
       if(!icc_channels||n->integer!=icc_channels)return false;
       // Validate the profile stream reference, but do not claim ICC-based
       // color accuracy: the fallback uses the explicit Alternate only.
       std::vector<uint8_t> profile;
       if(!decode_stream(data,size,icc_range,profile,4*1024*1024)||
          profile.size()<128)return false;
      }else if(cs->kind==Value::Kind::Array&&!calibrated&&!lab&&!separation){
       if(cs->array.size()!=4||cs->array[0].kind!=Value::Kind::Name||
          cs->array[0].text!="Indexed"||
          cs->array[1].kind!=Value::Kind::Name||
          cs->array[2].kind!=Value::Kind::Integer||
          cs->array[2].integer<0||cs->array[2].integer>255||
          (cs->array[3].kind!=Value::Kind::String&&
           cs->array[3].kind!=Value::Kind::Reference))return false;
       palette_channels=cs->array[1].text=="DeviceRGB"?3:
                        cs->array[1].text=="DeviceGray"?1:0;
       if(!palette_channels)return false;
       maximum_index=unsigned(cs->array[2].integer);
       const size_t palette_size=size_t(maximum_index+1)*palette_channels;
       if(cs->array[3].kind==Value::Kind::String)
        palette=cs->array[3].text;
       else{
        std::vector<uint8_t> decoded_palette;
        if(!stream(cs->array[3],decoded_palette,palette_size)||
           decoded_palette.size()!=palette_size)return false;
        palette.assign(decoded_palette.begin(),decoded_palette.end());
       }
       if(palette.size()!=palette_size)return false;
       indexed=true;
      }
      if(!indexed&&bits->integer!=8&&bits->integer!=1&&
         bits->integer!=2&&bits->integer!=4)return false;
      if(indexed&&unsigned(maximum_index)>=(1u<<unsigned(bits->integer)))
       return false;
      const unsigned channels=indexed?1:separation?device_inputs:lab?3:calibrated?calibrated:icc_channels?icc_channels:
        cs->text=="DeviceGray"?1:cs->text=="DeviceRGB"?3:
        cs->text=="DeviceCMYK"?4:0;
      if(!channels||(!indexed&&bits->integer!=8&&channels!=1))
       return false;
      // Color-key masking compares original integer samples, before
      // Decode and color-space conversion. Restrict it to 8-bit devices.
      std::array<unsigned,8> mask_bounds{};
      bool has_color_key=false;
      if(const Value* mask=get(range.dictionary,"Mask")){
       if(bits->integer!=8||indexed||separation||calibrated||lab||
          icc_channels||cs->kind!=Value::Kind::Name||
          (cs->text!="DeviceGray"&&cs->text!="DeviceRGB"&&
           cs->text!="DeviceCMYK")||
          get(range.dictionary,"SMask")||
          mask->kind!=Value::Kind::Array||
          mask->array.size()!=size_t(channels)*2)return false;
       for(unsigned c=0;c<channels;++c){
        for(unsigned j=0;j<2;++j){
         const Value& v=mask->array[2*c+j];
         if(v.kind!=Value::Kind::Integer||
            v.integer<0||v.integer>255)return false;
         mask_bounds[2*c+j]=unsigned(v.integer);
        }
        if(mask_bounds[2*c]>mask_bounds[2*c+1])return false;
       }
       has_color_key=true;
      }
      // Apply DeviceGray/RGB/CMYK Decode independently by component.
      // Packed DeviceGray only admits normal or reversed endpoints.
      std::array<double,8> decode_endpoints{};
      bool has_device_decode=false;
      if(const Value* decode=get(range.dictionary,"Decode")){
       if(cs->kind!=Value::Kind::Name||
          (cs->text!="DeviceGray"&&cs->text!="DeviceRGB"&&
           cs->text!="DeviceCMYK")||
          decode->kind!=Value::Kind::Array||
          decode->array.size()!=2*channels)return false;
       if(bits->integer!=8&&channels!=1)return false;
       for(size_t i=0;i<2*channels;++i){
        const Value& v=decode->array[i];
        if(v.kind==Value::Kind::Integer)decode_endpoints[i]=double(v.integer);
        else if(v.kind==Value::Kind::Real)decode_endpoints[i]=v.real;
        else return false;
        if(!std::isfinite(decode_endpoints[i])||
           decode_endpoints[i]<0||decode_endpoints[i]>1)return false;
       }
       if(bits->integer!=8&&
          !((decode_endpoints[0]==0&&decode_endpoints[1]==1)||
            (decode_endpoints[0]==1&&decode_endpoints[1]==0)))return false;
       has_device_decode=true;
      }
      auto apply_device_decode=[&](){
       if(!has_device_decode)return;
       for(size_t i=0;i<image.pixels.size();++i){
        const unsigned channel=unsigned(i%channels);
        const double lo=decode_endpoints[2*channel];
        const double hi=decode_endpoints[2*channel+1];
        const double normalized=double(image.pixels[i])/255;
        image.pixels[i]=uint8_t(std::lround(
          255*std::clamp(lo+normalized*(hi-lo),0.0,1.0)));
       }
      };
      auto apply_color_key=[&](){
       if(!has_color_key)return;
       const size_t pixels=size_t(image.width)*image.height;
       image.alpha.assign(pixels,255);
       for(size_t i=0;i<pixels;++i){
        bool match=true;
        for(unsigned c=0;c<channels;++c){
         const unsigned value=image.pixels[i*channels+c];
         if(value<mask_bounds[2*c]||value>mask_bounds[2*c+1]){
          match=false;break;
         }
        }
        if(match)image.alpha[i]=0;
       }
      };
      const size_t expected=size_t(w->integer)*size_t(h->integer)*channels;
      if(expected>32*1024*1024){return false;}
      image=PageImage{};
      image.width=unsigned(w->integer);image.height=unsigned(h->integer);
      image.channels=channels;
      const Value* filter=get(range.dictionary,"Filter");
      const bool fax=filter&&filter->kind==Value::Kind::Name&&
        (filter->text=="CCITTFaxDecode"||filter->text=="CCF");
      if(fax){
       if(indexed||bits->integer!=1||channels!=1||
          get(range.dictionary,"SMask")||get(range.dictionary,"Mask")||
          get(range.dictionary,"Decode"))return false;
       const Value* parms=get(range.dictionary,"DecodeParms");
       if(parms&&parms->kind!=Value::Kind::Dictionary)return false;
       auto number=[&](const char* key,long long fallback,long long& out){
        const Value* v=parms?get(*parms,key):nullptr;
        if(!v){out=fallback;return true;}
        if(v->kind!=Value::Kind::Integer)return false;
        out=v->integer;return true;
       };
       auto boolean=[&](const char* key,bool fallback,bool& out){
        const Value* v=parms?get(*parms,key):nullptr;
        if(!v){out=fallback;return true;}
        if(v->kind!=Value::Kind::Boolean)return false;
        out=v->boolean;return true;
       };
       long long k=0,columns=image.width,rows=image.height,damaged=0;
       bool byte_align=false,black_is_one=false;
       if(!number("K",0,k)||!number("Columns",image.width,columns)||
          !number("Rows",image.height,rows)||
          !number("DamagedRowsBeforeError",0,damaged)||
          !boolean("EncodedByteAlign",false,byte_align)||
          !boolean("BlackIs1",false,black_is_one)||
          columns!=image.width||rows!=image.height||damaged!=0||
          (k!=-1&&k!=0)||
          range.offset>size||range.length>size-range.offset||
          !decode_ccitt_image(data+range.offset,range.length,
             image.width,image.height,int(k),byte_align,black_is_one,
             image.pixels))return false;
       return true;
      }
      if(!indexed&&bits->integer!=8&&
         !(channels==1&&(bits->integer==1||bits->integer==2||
                         bits->integer==4)))return false;
      // Support a same-size, 8-bit grayscale SMask image. Restrict the
      // initial implementation to raw/Flate data without predictors.
      if(const Value* smask=get(range.dictionary,"SMask")){
       if(smask->kind!=Value::Kind::Reference){return false;}
       const XrefEntry* mask_entry=nullptr;
       if(!entry(*smask,mask_entry)){return false;}
       StreamRange mask_range;
       if(!locate_indirect_stream(data,size,*mask_entry,xref,mask_range)){
        return false;
       }
       const Value* ms=get(mask_range.dictionary,"Subtype");
       const Value* mw=get(mask_range.dictionary,"Width");
       const Value* mh=get(mask_range.dictionary,"Height");
       const Value* mb=get(mask_range.dictionary,"BitsPerComponent");
       const Value* mc=get(mask_range.dictionary,"ColorSpace");
       if(!ms||ms->kind!=Value::Kind::Name||ms->text!="Image"||
          !mw||mw->kind!=Value::Kind::Integer||mw->integer!=w->integer||
          !mh||mh->kind!=Value::Kind::Integer||mh->integer!=h->integer||
          !mb||mb->kind!=Value::Kind::Integer||mb->integer!=8||
          !mc||mc->kind!=Value::Kind::Name||mc->text!="DeviceGray"||
          get(mask_range.dictionary,"SMask")||
          get(mask_range.dictionary,"Mask")||
          get(mask_range.dictionary,"DecodeParms"))return false;
       // Soft-mask grayscale may reverse its opacity mapping via Decode.
       // Do not accept arbitrary ranges until the full mask color model
       // and matte processing are implemented.
       bool invert_alpha=false;
       if(const Value* mask_decode=get(mask_range.dictionary,"Decode")){
        if(mask_decode->kind!=Value::Kind::Array||
           mask_decode->array.size()!=2)return false;
        double bounds[2]{};
        for(unsigned j=0;j<2;++j){
         const Value& value=mask_decode->array[j];
         if(value.kind==Value::Kind::Integer)bounds[j]=double(value.integer);
         else if(value.kind==Value::Kind::Real)bounds[j]=value.real;
         else return false;
        }
        if(bounds[0]==1&&bounds[1]==0)invert_alpha=true;
        else if(bounds[0]!=0||bounds[1]!=1)return false;
       }
       const Value* mask_filter=get(mask_range.dictionary,"Filter");
       const bool mask_jpeg=mask_filter&&
          mask_filter->kind==Value::Kind::Name&&
          (mask_filter->text=="DCTDecode"||mask_filter->text=="DCT");
       if(mask_jpeg){
        if(mask_range.offset>size||
           mask_range.length>size-mask_range.offset||
           !decode_dct_image(data+mask_range.offset,mask_range.length,
                             image.width,image.height,1,image.alpha))
         return false;
       }else if(!decode_stream(data,size,mask_range,image.alpha,
                               size_t(w->integer)*size_t(h->integer)))
        return false;
       if(image.alpha.size()!=size_t(w->integer)*size_t(h->integer))
        return false;
       if(invert_alpha)
        for(auto& value:image.alpha)value=uint8_t(255u-value);
      }
      const bool dct=filter&&filter->kind==Value::Kind::Name&&
        (filter->text=="DCTDecode"||filter->text=="DCT");
      if(dct){
       // The JPEG backend only decodes native Gray/RGB samples.
       if(indexed||separation||calibrated||lab||channels==4||
          bits->integer!=8)return false;
       // JPEG image data is compressed independently of PDF Flate filters.
       // DecodeParms predictors are not valid for this path.
       if(get(range.dictionary,"DecodeParms")||
          range.offset>size||range.length>size-range.offset||
          !decode_dct_image(data+range.offset,range.length,
             unsigned(w->integer),unsigned(h->integer),channels,
             image.pixels)){return false;}
       apply_color_key();
       apply_device_decode();
       return true;
      }
      // Predictor handling is centralized in decode_stream for both
      // Flate and LZW images. Validate the resulting raster dimensions.
      if(const Value* parms=get(range.dictionary,"DecodeParms")){
       if(parms->kind!=Value::Kind::Dictionary)return false;
       auto check=[&](const char* key,unsigned expected_value){
        const Value* value=get(*parms,key);
        return !value||(value->kind==Value::Kind::Integer&&
                        value->integer==expected_value);
       };
       if(!check("Columns",image.width)||!check("Colors",channels)||
          !check("BitsPerComponent",unsigned(bits->integer)))return false;
      }
      const unsigned sample_bits=unsigned(bits->integer);
      const size_t packed_stride=(size_t(image.width)*sample_bits+7)/8;
      const bool packed_gray=!indexed&&channels==1&&sample_bits<8;
      const size_t decoded_size=(indexed&&sample_bits<8)||packed_gray?
         packed_stride*image.height:expected;
      std::vector<uint8_t> decoded;
      if(!decode_stream(data,size,range,decoded,decoded_size)||
         decoded.size()!=decoded_size)return false;
      if(indexed&&sample_bits<8){
       image.pixels.resize(size_t(image.width)*image.height);
       const unsigned sample_mask=(1u<<sample_bits)-1;
       for(size_t y=0;y<image.height;++y)
        for(size_t x=0;x<image.width;++x){
         const size_t bit=x*sample_bits;
         image.pixels[y*image.width+x]=uint8_t(
          (decoded[y*packed_stride+bit/8]>>(8-sample_bits-(bit%8)))&
          sample_mask);
        }
      }else if(packed_gray){
       // DeviceGray samples of 1, 2, or 4 bits are MSB-first and
       // normalized to 8-bit grayscale. Each row has separate padding.
       image.pixels.resize(size_t(image.width)*image.height);
       const unsigned max_sample=(1u<<sample_bits)-1u;
       for(size_t y=0;y<image.height;++y)
        for(size_t x=0;x<image.width;++x){
         const size_t bit=x*sample_bits;
         const unsigned sample=(decoded[y*packed_stride+bit/8]>>
                    (8-sample_bits-(bit%8)))&max_sample;
         image.pixels[y*image.width+x]=uint8_t(
                    (sample*255u+max_sample/2u)/max_sample);
        }
      }else image.pixels=std::move(decoded);
      apply_color_key();
      apply_device_decode();
      if(indexed){
       std::vector<uint8_t> rgb(size_t(image.width)*image.height*3);
       for(size_t i=0;i<size_t(image.width)*image.height;++i){
        const unsigned index=image.pixels[i];
        if(index>maximum_index)return false;
        for(unsigned c=0;c<3;++c)
         rgb[3*i+c]=uint8_t(palette[index*palette_channels+
                                     (palette_channels==1?0:c)]);
       }
       image.pixels=std::move(rgb);image.channels=3;
      }else if(separation){
       std::vector<uint8_t> rgb(size_t(image.width)*image.height*3);
       for(size_t i=0;i<size_t(image.width)*image.height;++i){
        double values[4]{};
        if(sampled_tint){
         double inputs[4]{};
         for(unsigned d=0;d<device_inputs;++d)
          inputs[d]=double(image.pixels[i*device_inputs+d])/255;
         if(!evaluate_pdf_function_inputs(
                compiled_tint,inputs,device_inputs,values))return false;
        }else if(recursive_stitch){
         if(!eval_tint_function(stitch_function,
              double(image.pixels[i])/255,tint_channels,values))
          return false;
        }else{
         const double tint=std::pow(double(image.pixels[i])/255,tint_n);
         for(unsigned c=0;c<tint_channels;++c)
          values[c]=tint_c0[c]+tint*(tint_c1[c]-tint_c0[c]);
        }
        for(unsigned c=0;c<3;++c){
         double v=tint_channels==1?values[0]:
                  tint_channels==3?values[c]:
                  (1-values[c])*(1-values[3]);
         rgb[3*i+c]=uint8_t(std::lround(255*std::clamp(v,0.0,1.0)));
        }
       }
       image.pixels=std::move(rgb);image.channels=3;
      }else if(lab){
       std::vector<uint8_t> rgb(size_t(image.width)*image.height*3);
       auto convert=[](double linear){
        linear=std::clamp(linear,0.0,1.0);
        const double value=linear<=0.0031308?12.92*linear:
          1.055*std::pow(linear,1.0/2.4)-0.055;
        return uint8_t(std::lround(255*std::clamp(value,0.0,1.0)));
       };
       auto inverse_lab=[](double t){
        const double delta=6.0/29.0;
        return t>delta?t*t*t:3*delta*delta*(t-4.0/29.0);
       };
       for(size_t i=0;i<size_t(image.width)*image.height;++i){
        const double L=100*double(image.pixels[3*i])/255;
        const double aa=lab_range[0]+
          (lab_range[1]-lab_range[0])*double(image.pixels[3*i+1])/255;
        const double bb=lab_range[2]+
          (lab_range[3]-lab_range[2])*double(image.pixels[3*i+2])/255;
        const double fy=(L+16)/116;
        const double X=0.95047*inverse_lab(fy+aa/500);
        const double Y=inverse_lab(fy);
        const double Z=1.08883*inverse_lab(fy-bb/200);
        rgb[3*i]=convert(3.2406*X-1.5372*Y-0.4986*Z);
        rgb[3*i+1]=convert(-0.9689*X+1.8758*Y+0.0415*Z);
        rgb[3*i+2]=convert(0.0557*X-0.2040*Y+1.0570*Z);
       }
       image.pixels=std::move(rgb);image.channels=3;
      }else if(calibrated){
       std::vector<uint8_t> rgb(size_t(image.width)*image.height*3);
       auto srgb=[](double linear){
        linear=std::clamp(linear,0.0,1.0);
        const double encoded=linear<=0.0031308?
          12.92*linear:1.055*std::pow(linear,1.0/2.4)-0.055;
        return uint8_t(std::lround(std::clamp(encoded,0.0,1.0)*255));
       };
       for(size_t i=0;i<size_t(image.width)*image.height;++i){
        double X=0,Y=0,Z=0;
        if(calibrated==1){
         Y=std::pow(double(image.pixels[i])/255,cal_gamma[0]);
         X=0.95047*Y;Z=1.08883*Y;
        }else{
         double component[3];
         for(unsigned c=0;c<3;++c)
          component[c]=std::pow(double(image.pixels[i*3+c])/255,
                                cal_gamma[c]);
         X=cal_matrix[0]*component[0]+cal_matrix[3]*component[1]+
           cal_matrix[6]*component[2];
         Y=cal_matrix[1]*component[0]+cal_matrix[4]*component[1]+
           cal_matrix[7]*component[2];
         Z=cal_matrix[2]*component[0]+cal_matrix[5]*component[1]+
           cal_matrix[8]*component[2];
        }
        rgb[3*i]=srgb(3.2406*X-1.5372*Y-0.4986*Z);
        rgb[3*i+1]=srgb(-0.9689*X+1.8758*Y+0.0415*Z);
        rgb[3*i+2]=srgb(0.0557*X-0.2040*Y+1.0570*Z);
       }
       image.pixels=std::move(rgb);image.channels=3;
      }else if(channels==4){
       // Keep decoded CMYK samples for native CMYK group rendering.
       // The RGB fallback remains available to ordinary page rendering.
       image.cmyk_pixels=image.pixels;
       std::vector<uint8_t> rgb(size_t(image.width)*image.height*3);
       for(size_t i=0;i<size_t(image.width)*image.height;++i){
        const unsigned k=image.pixels[4*i+3];
        for(unsigned c=0;c<3;++c)
         rgb[3*i+c]=uint8_t(((255-unsigned(image.pixels[4*i+c]))*
                                  (255-k)+127)/255);
       }
       image.pixels=std::move(rgb);image.channels=3;
      }
      return true;
 }
 bool walk_images(const Value& ref,size_t depth,size_t& current,size_t target,
  std::set<size_t>& active,const Value* inherited,
  std::map<std::string,PageImage>& output) const {
  if(depth>64||ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  if(!ScopedPageXref::path_contains(data,size,target,ref))return true;
  const size_t id=static_cast<size_t>(ref.integer);
  if(!active.insert(id).second)return false;
  Value node,resources_owned;
  if(!object(ref,node)){active.erase(id);return false;}
  if(const Value* resources=get(node,"Resources")){
   if(resources->kind==Value::Kind::Reference){
    if(!object(*resources,resources_owned)){active.erase(id);return false;}
   }else resources_owned=*resources;
   if(resources_owned.kind!=Value::Kind::Dictionary){active.erase(id);return false;}
   inherited=&resources_owned;
  }
  const Value* type=get(node,"Type");
  if(!type||type->kind!=Value::Kind::Name){active.erase(id);return false;}
  if(type->text=="Page"){
   if(current==target&&inherited){
    if(const Value* xobjs=get(*inherited,"XObject")){
     Value entries;
     if(xobjs->kind==Value::Kind::Reference){
      if(!object(*xobjs,entries)){active.erase(id);return false;}
     }else entries=*xobjs;
     if(entries.kind!=Value::Kind::Dictionary||
        entries.dictionary.size()>256){active.erase(id);return false;}
     for(const auto& [name,ref_image]:entries.dictionary){
      if(ref_image.kind!=Value::Kind::Reference){active.erase(id);return false;}
      const XrefEntry* ent=nullptr;
      if(!entry(ref_image,ent)){active.erase(id);return false;}
      StreamRange range;
      if(!locate_indirect_stream(data,size,*ent,xref,range)){
       active.erase(id);return false;
      }
      const Value* subtype=get(range.dictionary,"Subtype");
      if(!subtype||subtype->kind!=Value::Kind::Name){active.erase(id);return false;}
      if(subtype->text=="Form")continue;
      if(subtype->text!="Image"){active.erase(id);return false;}
      PageImage image;
      if(!decode_page_image(range,image)){active.erase(id);return false;}
      output.emplace(name,std::move(image));
     }
    }
   }
   ++current;active.erase(id);return true;
  }
  if(type->text!="Pages"){active.erase(id);return false;}
  const Value* kids=get(node,"Kids");
  if(!kids||kids->kind!=Value::Kind::Array){active.erase(id);return false;}
  if(current>target){active.erase(id);return true;}
  for(const Value& child:kids->array){
   if(current>target)break;
   if(!skip_preceding_subtree(child,current,target) && !walk_images(child,depth+1,current,target,active,inherited,output)){
    active.erase(id);return false;
   }
  }
  active.erase(id);return true;
 }
 // Evaluate one-input Type 2 and nested Type 3 functions. References
 // are resolved explicitly and recursion is bounded for untrusted PDFs.
 #include "function_eval.inc"
 bool parse_cid_descendant(const Value& descendant,PageFont& resource) const {
  const Value* subtype=get(descendant,"Subtype");
  if(!subtype||subtype->kind!=Value::Kind::Name||
     subtype->text!="CIDFontType2"||
     !parse_cid_widths(get(descendant,"DW"),get(descendant,"W"),
                       resource.cid_widths)||
     !parse_cid_vertical_metrics(get(descendant,"DW2"),get(descendant,"W2"),
                                 resource.vertical_metrics))return false;
  if(const Value* map=get(descendant,"CIDToGIDMap")){
   if(map->kind==Value::Kind::Name){
    if(map->text!="Identity")return false;
   }else if(map->kind==Value::Kind::Reference){
    resource.cid_map.identity=false;
    if(!stream(*map,resource.cid_map.bytes,131072)||
       !cid_map_valid(resource.cid_map))return false;
   }else return false;
  }
  return true;
 }
 bool parse_font_descriptor(const Value& reference,PageFont& font) const {
  Value descriptor;
  if(reference.kind==Value::Kind::Reference){
   if(!object(reference,descriptor))return false;
  }else descriptor=reference;
  if(descriptor.kind!=Value::Kind::Dictionary)return false;
  if(font.subtype!="Type0"){
   if(const Value* missing=get(descriptor,"MissingWidth")){
    if(missing->kind==Value::Kind::Integer)
     font.missing_width=double(missing->integer);
    else if(missing->kind==Value::Kind::Real)
     font.missing_width=missing->real;
    else return false;
    if(!std::isfinite(font.missing_width)||
       font.missing_width<0||font.missing_width>100000)return false;
   }
  }
  if(const Value* file=get(descriptor,"FontFile2")){
   if(file->kind!=Value::Kind::Reference||
      !stream(*file,font.embedded_truetype,16*1024*1024))return false;
   renderer::SfntFont validated;
   if(!renderer::parse_sfnt(font.embedded_truetype.data(),
                            font.embedded_truetype.size(),validated))return false;
  }
  return true;
 }
 bool parse_font_encoding(const Value& font,PageFont& out) const {
  const Value* encoding=get(font,"Encoding");
  if(!encoding)return true;
  Value resolved;
  if(encoding->kind==Value::Kind::Reference){
   if(!object(*encoding,resolved))return false;
  }else resolved=*encoding;
  if(resolved.kind==Value::Kind::Name){
   out.encoding=resolved.text;
   return true;
  }
  if(resolved.kind!=Value::Kind::Dictionary)return false;
  const Value* base=get(resolved,"BaseEncoding");
  const Value* differences=get(resolved,"Differences");
  if(!base||base->kind!=Value::Kind::Name||!differences||
     !decode_font_differences(*differences,out.differences))return false;
  out.encoding=base->text;
  return true;
 }
 bool parse_font_dictionary(const Value& font,const Value* scope,
                           PageFont& out) const {
      const Value* subtype=get(font,"Subtype"),*base=get(font,"BaseFont");
      if(font.kind!=Value::Kind::Dictionary||!subtype||!base||
         subtype->kind!=Value::Kind::Name||base->kind!=Value::Kind::Name){
       return false;
      }
      out=PageFont{base->text,subtype->text,"",{}};
      if(!parse_font_encoding(font,out)){return false;}
      if(out.subtype!="Type0"){
       const Value* first=get(font,"FirstChar");
       const Value* last=get(font,"LastChar");
       const Value* widths=get(font,"Widths");
       if(first||last||widths){
        if(!first||!last||!widths||
           first->kind!=Value::Kind::Integer||
           last->kind!=Value::Kind::Integer||
           first->integer<0||last->integer>255||
           first->integer>last->integer){return false;}
        Value width_array;
        if(widths->kind==Value::Kind::Reference){
         if(!object(*widths,width_array)){return false;}
        }else width_array=*widths;
        if(width_array.kind!=Value::Kind::Array||
           width_array.array.size()!=size_t(last->integer-first->integer+1)){
         return false;
        }
        for(size_t i=0;i<width_array.array.size();++i){
         double width=0;
         const Value& value=width_array.array[i];
         if(value.kind==Value::Kind::Integer)width=double(value.integer);
         else if(value.kind==Value::Kind::Real)width=value.real;
         else{return false;}
         if(!std::isfinite(width)||width<0||width>100000){
          return false;
         }
         out.simple_widths.emplace(static_cast<uint8_t>(first->integer+static_cast<long long>(i)),width);
        }
       }
      }
      // Type0 fonts select a descendant CIDFont; CIDToGIDMap is distinct
      // from the ToUnicode extraction map and must not be confused with it.
      if(out.subtype=="Type0"){
       const Value* descendants=get(font,"DescendantFonts");
       if(!descendants||descendants->kind!=Value::Kind::Array||
          descendants->array.size()!=1){return false;}
       const Value& reference=descendants->array.front();
       Value descendant;
       if(reference.kind==Value::Kind::Reference){
        if(!object(reference,descendant)){return false;}
       }else descendant=reference;
       if(!parse_cid_descendant(descendant,out)){
        return false;
       }
       if(const Value* child_descriptor=get(descendant,"FontDescriptor")){
        if(!parse_font_descriptor(*child_descriptor,out)){
         return false;
        }
       }
      }
      if(const Value* descriptor=get(font,"FontDescriptor")){
       if(!parse_font_descriptor(*descriptor,out)){
        return false;
       }
      }
      if(const Value* cmap=get(font,"ToUnicode")){
       std::set<size_t> visiting;
       std::string cmap_name;
       if(!resolve_unicode_cmap(*cmap,0,visiting,out.unicode_sequences,
            out.unicode_codespaces,cmap_name,scope)){
        return false;
       }
      }
  return true;
 }
 bool load_local_form(const Value& ref,unsigned depth,std::set<size_t>& visiting,
                     const std::map<std::pair<int64_t,int64_t>,std::string>& page_names,
                     PageForm& form) const {
  if(depth>=8||ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  const size_t id=size_t(ref.integer);
  if(!visiting.insert(id).second)return false;
  const XrefEntry* ent=nullptr;
  if(!entry(ref,ent))return false;
  StreamRange range;
  if(!locate_indirect_stream(data,size,*ent,xref,range))return false;
  const Value* subtype=get(range.dictionary,"Subtype");
  const Value* form_type=get(range.dictionary,"FormType");
  if(!subtype||subtype->kind!=Value::Kind::Name||subtype->text!="Form"||
     (form_type&&(form_type->kind!=Value::Kind::Integer||form_type->integer!=1)))
   return false;
  if(const Value* group_ref=get(range.dictionary,"Group")){
   Value group;
   if(group_ref->kind==Value::Kind::Reference){
    if(!object(*group_ref,group))return false;
   }else group=*group_ref;
   if(group.kind!=Value::Kind::Dictionary)return false;
   const Value* subtype=get(group,"S");
   const Value* isolated=get(group,"I");
   const Value* knockout=get(group,"K");
   if(!subtype||subtype->kind!=Value::Kind::Name||
      subtype->text!="Transparency"||
      (isolated&&isolated->kind!=Value::Kind::Boolean)||
      (knockout&&knockout->kind!=Value::Kind::Boolean))
    return false;
   if(const Value* cs=get(group,"CS")){
    // Resolve an indirect color-space name, but reject other families
    // rather than interpreting them as DeviceRGB.
    Value color_space;
    if(cs->kind==Value::Kind::Reference){
     if(!object(*cs,color_space))return false;
    }else color_space=*cs;
    if(color_space.kind==Value::Kind::Array){
     // ICCBased spaces are an array [/ICCBased profile-stream-ref].
     if(color_space.array.size()!=2||
        color_space.array[0].kind!=Value::Kind::Name||
        color_space.array[0].text!="ICCBased"||
        color_space.array[1].kind!=Value::Kind::Reference)return false;
     const XrefEntry* profile_entry=nullptr;
     if(!entry(color_space.array[1],profile_entry))return false;
     StreamRange profile_range;
     if(!locate_indirect_stream(data,size,*profile_entry,xref,
                                profile_range))return false;
     const Value* channels=get(profile_range.dictionary,"N");
     if(!channels||channels->kind!=Value::Kind::Integer||
        (channels->integer!=1&&channels->integer!=3&&
         channels->integer!=4))return false;
     // Reject malformed ICC header; do not claim color conversion here.
     if(!decode_stream(data,size,profile_range,form.group_icc_profile,
                       4*1024*1024))return false;
     const auto& bytes=form.group_icc_profile;
     if(bytes.size()<128||
        bytes[36]!='a'||bytes[37]!='c'||
        bytes[38]!='s'||bytes[39]!='p')return false;
     const uint32_t declared=(uint32_t(bytes[0])<<24)|
         (uint32_t(bytes[1])<<16)|(uint32_t(bytes[2])<<8)|bytes[3];
     if(declared!=bytes.size())return false;
     form.group_icc_channels=unsigned(channels->integer);
     form.group_color_space="ICCBased";
    }else{
     if(color_space.kind!=Value::Kind::Name||
        (color_space.text!="DeviceRGB"&&
         color_space.text!="DeviceGray"&&
         color_space.text!="DeviceCMYK"))return false;
     form.group_color_space=color_space.text;
    }
   }
   form.transparency_group=true;
   form.isolated_transparency_group=isolated&&isolated->boolean;
   form.knockout_transparency_group=knockout&&knockout->boolean;
  }
  const Value* bbox=get(range.dictionary,"BBox");
  if(!bbox||bbox->kind!=Value::Kind::Array||bbox->array.size()!=4)return false;
  auto number=[](const Value& value,double& out){
   if(value.kind==Value::Kind::Integer)out=double(value.integer);
   else if(value.kind==Value::Kind::Real)out=value.real;
   else return false;
   return std::isfinite(out)&&std::abs(out)<=10000000;
  };
  for(size_t i=0;i<4;++i)if(!number(bbox->array[i],form.bbox[i]))return false;
  if(form.bbox[0]>=form.bbox[2]||form.bbox[1]>=form.bbox[3])return false;
  if(const Value* matrix=get(range.dictionary,"Matrix")){
   if(matrix->kind!=Value::Kind::Array||matrix->array.size()!=6)return false;
   for(size_t i=0;i<6;++i)
    if(!number(matrix->array[i],form.matrix[i]))return false;
  }
  if(!decode_stream(data,size,range,form.content,4*1024*1024))return false;
  if(const Value* resource=get(range.dictionary,"Resources")){
   form.own_resources=true;
   Value scope;
   if(resource->kind==Value::Kind::Reference){
    if(!object(*resource,scope))return false;
   }else scope=*resource;
   if(scope.kind!=Value::Kind::Dictionary||
      get(scope,"Properties")||
      get(scope,"ExtGState")||
      get(scope,"Pattern")||get(scope,"Shading"))return false;
   if(const Value* spaces=get(scope,"ColorSpace")){
    Value entries;
    if(spaces->kind==Value::Kind::Reference){
     if(!object(*spaces,entries))return false;
    }else entries=*spaces;
    if(entries.kind!=Value::Kind::Dictionary||
       entries.dictionary.size()>64)return false;
    for(const auto& [alias,definition]:entries.dictionary){
     Value resolved;
     if(definition.kind==Value::Kind::Reference){
      if(!object(definition,resolved))return false;
     }else resolved=definition;
     if(resolved.kind==Value::Kind::Array&&
        resolved.array.size()==4&&
        resolved.array[0].kind==Value::Kind::Name&&
        resolved.array[0].text=="Separation"){
      const auto& alternative=resolved.array[2];
      if(alternative.kind!=Value::Kind::Name)return false;
      SeparationColorSpace separation;
      separation.alternate=alternative.text;
      separation.components=separation.alternate=="DeviceGray"?1u:
        separation.alternate=="DeviceRGB"?3u:
        separation.alternate=="DeviceCMYK"?4u:0u;
      if(!separation.components)return false;
      Value function;
      StreamRange sample_range;
      bool sampled_stream=false;
      if(resolved.array[3].kind==Value::Kind::Reference){
       const XrefEntry* function_entry=nullptr;
       if(!entry(resolved.array[3],function_entry))return false;
       if(locate_indirect_stream(data,size,*function_entry,xref,
                                 sample_range)){
        function=sample_range.dictionary;
        sampled_stream=true;
       }else if(!object(resolved.array[3],function))return false;
      }else function=resolved.array[3];
      if(function.kind!=Value::Kind::Dictionary)return false;
      auto numeric=[](const Value& v,double& out)->bool{
       if(v.kind==Value::Kind::Integer)out=double(v.integer);
       else if(v.kind==Value::Kind::Real)out=v.real;
       else return false;
       return std::isfinite(out);
      };
      // Compile a single-input PDF function tree with bounded recursion.
      // Type 0 samples are normalized to eight-bit values for painting.
      using Segment=SeparationColorSpace::Segment;
      unsigned node_count=0;
      std::function<bool(const Value&,const StreamRange*,unsigned,Segment&)>
       parse_function;
      parse_function=[&](const Value& fn,const StreamRange* stream,
                         unsigned depth,Segment& node)->bool{
       if(depth>8||++node_count>128||fn.kind!=Value::Kind::Dictionary)
        return false;
       const Value* type=get(fn,"FunctionType");
       if(!type||type->kind!=Value::Kind::Integer)return false;
       if(type->integer==2){
        const Value* exponent=get(fn,"N");
        if(!exponent||!numeric(*exponent,node.exponent)||
           node.exponent<=0||node.exponent>100)return false;
        for(unsigned c=0;c<separation.components;++c)node.c1[c]=1;
        for(const char* key:{"C0","C1"}){
         const Value* values=get(fn,key);
         if(!values)continue;
         if(values->kind!=Value::Kind::Array||
            values->array.size()!=separation.components)return false;
         for(unsigned c=0;c<separation.components;++c){
          double value=0;
          if(!numeric(values->array[c],value)||value<0||value>1)
           return false;
          if(key[1]=='0')node.c0[c]=value;
          else node.c1[c]=value;
         }
        }
        return true;
       }
       if(type->integer==0){
        if(!stream)return false;
        const Value* sizes=get(fn,"Size");
        const Value* bits=get(fn,"BitsPerSample");
        const Value* domain=get(fn,"Domain");
        const Value* range=get(fn,"Range");
        const Value* encode=get(fn,"Encode");
        const Value* order=get(fn,"Order");
        const Value* decode=get(fn,"Decode");
        if(!sizes||sizes->kind!=Value::Kind::Array||
           sizes->array.size()!=1||
           sizes->array[0].kind!=Value::Kind::Integer||
           sizes->array[0].integer<2||sizes->array[0].integer>4096||
           !bits||bits->kind!=Value::Kind::Integer||
           (bits->integer!=1&&bits->integer!=2&&bits->integer!=4&&
            bits->integer!=8&&bits->integer!=12&&bits->integer!=16)||
           !domain||domain->kind!=Value::Kind::Array||
           domain->array.size()!=2||
           !range||range->kind!=Value::Kind::Array||
           range->array.size()!=2*separation.components||
           (order&&(order->kind!=Value::Kind::Integer||order->integer!=1)))
         return false;
        if(!numeric(domain->array[0],node.domain[0])||
           !numeric(domain->array[1],node.domain[1])||
           node.domain[0]<0||node.domain[0]>=node.domain[1]||
           node.domain[1]>1)return false;
        for(unsigned c=0;c<separation.components;++c){
         double lo=0,hi=0;
         if(!numeric(range->array[2*c],lo)||
            !numeric(range->array[2*c+1],hi)||
            lo<0||lo>hi||hi>1)return false;
         node.range[2*c]=lo;node.range[2*c+1]=hi;
         node.decode[2*c]=lo;node.decode[2*c+1]=hi;
        }
        if(decode){
         if(decode->kind!=Value::Kind::Array||
            decode->array.size()!=2*separation.components)return false;
         for(unsigned c=0;c<2*separation.components;++c)
          if(!numeric(decode->array[c],node.decode[c]))return false;
        }
        node.sample_count=unsigned(sizes->array[0].integer);
        node.encode[1]=double(node.sample_count-1);
        if(encode){
         if(encode->kind!=Value::Kind::Array||encode->array.size()!=2)
          return false;
         for(unsigned c=0;c<2;++c)
          if(!numeric(encode->array[c],node.encode[c])||
             node.encode[c]<0||
             node.encode[c]>double(node.sample_count-1))return false;
        }
        const unsigned bit_depth=unsigned(bits->integer);
        const size_t count=size_t(node.sample_count)*separation.components;
        const size_t expected=(count*bit_depth+7u)/8u;
        std::vector<uint8_t> packed;
        if(!decode_stream(data,size,*stream,packed,expected)||
           packed.size()!=expected)return false;
        node.samples.reserve(count);
        const unsigned maximum=(1u<<bit_depth)-1u;
        for(size_t n=0;n<count;++n){
         const size_t offset=n*bit_depth;
         unsigned value=0;
         for(unsigned bit=0;bit<bit_depth;++bit)
          value=(value<<1u)|
            ((packed[(offset+bit)/8u]>>
               (7u-unsigned((offset+bit)%8u)))&1u);
         node.samples.push_back(uint8_t(
                                  (value*255u+maximum/2u)/maximum));
        }
        return true;
       }
       if(type->integer!=3)return false;
       const Value* functions=get(fn,"Functions");
       const Value* bounds=get(fn,"Bounds");
       const Value* encode=get(fn,"Encode");
       const Value* domain=get(fn,"Domain");
       if(!functions||functions->kind!=Value::Kind::Array||
          functions->array.empty()||functions->array.size()>16||
          !bounds||bounds->kind!=Value::Kind::Array||
          bounds->array.size()+1!=functions->array.size()||
          !encode||encode->kind!=Value::Kind::Array||
          encode->array.size()!=2*functions->array.size()||
          !domain||domain->kind!=Value::Kind::Array||
          domain->array.size()!=2)return false;
       double lo=0,hi=0;
       if(!numeric(domain->array[0],lo)||
          !numeric(domain->array[1],hi)||lo!=0||hi!=1)return false;
       double previous=0;
       for(const Value& bound:bounds->array){
        double value=0;
        if(!numeric(bound,value)||value<=previous||value>=1)
         return false;
        node.bounds.push_back(value);previous=value;
       }
       for(const Value& encoded:encode->array){
        double value=0;
        if(!numeric(encoded,value)||value<0||value>1)return false;
        node.stitch_encode.push_back(value);
       }
       for(const Value& ref:functions->array){
        Value child;
        StreamRange child_stream;
        const StreamRange* child_ptr=nullptr;
        if(ref.kind==Value::Kind::Reference){
         const XrefEntry* child_entry=nullptr;
         if(!this->entry(ref,child_entry))return false;
         if(locate_indirect_stream(data,size,*child_entry,xref,child_stream)){
          child=child_stream.dictionary;child_ptr=&child_stream;
         }else if(!object(ref,child))return false;
        }else child=ref;
        Segment sub;
        if(!parse_function(child,child_ptr,depth+1,sub))return false;
        node.children.push_back(std::move(sub));
       }
       return true;
      };
      Segment root_function;
      if(!parse_function(function,sampled_stream?&sample_range:nullptr,
                         0,root_function))return false;
      if(!root_function.children.empty()){
       separation.segments=std::move(root_function.children);
       separation.bounds=std::move(root_function.bounds);
       separation.encode=std::move(root_function.stitch_encode);
      }else if(!root_function.samples.empty()){
       separation.samples=std::move(root_function.samples);
       separation.sample_count=root_function.sample_count;
       for(unsigned i=0;i<2;++i){
        separation.sample_domain[i]=root_function.domain[i];
        separation.sample_encode[i]=root_function.encode[i];
       }
       for(unsigned i=0;i<2*separation.components;++i){
        separation.sample_range[i]=root_function.range[i];
        separation.sample_decode[i]=root_function.decode[i];
       }
      }else{
       for(unsigned i=0;i<separation.components;++i){
        separation.c0[i]=root_function.c0[i];
        separation.c1[i]=root_function.c1[i];
       }
       separation.exponent=root_function.exponent;
      }
      form.local_separations.emplace(alias,separation);
      continue;
     }
     // Device aliases can be used immediately by the renderer. Named
     // Separation/DeviceN definitions still need tint-transform support.
     if(resolved.kind!=Value::Kind::Name||
        (resolved.text!="DeviceGray"&&resolved.text!="DeviceRGB"&&
         resolved.text!="DeviceCMYK"))return false;
     form.local_color_spaces.emplace(alias,resolved.text);
    }
   }
   if(const Value* fonts=get(scope,"Font")){
    Value dictionary;
    if(fonts->kind==Value::Kind::Reference){
     if(!object(*fonts,dictionary))return false;
    }else dictionary=*fonts;
    if(dictionary.kind!=Value::Kind::Dictionary||
       dictionary.dictionary.size()>64)return false;
    for(const auto& [alias,reference]:dictionary.dictionary){
     Value font;
     if(reference.kind==Value::Kind::Reference){
      if(!object(reference,font))return false;
     }else font=reference;
     PageFont local;
     if(!parse_font_dictionary(font,&scope,local)||
        (local.subtype!="Type0"&&local.subtype!="TrueType")||
        local.embedded_truetype.empty())return false;
     form.local_fonts.emplace(alias,std::move(local));
    }
   }
   if(const Value* x=get(scope,"XObject")){
    Value children;
    if(x->kind==Value::Kind::Reference){
     if(!object(*x,children))return false;
    }else children=*x;
    if(children.kind!=Value::Kind::Dictionary||
       children.dictionary.size()>128)return false;
    for(const auto& [alias,child_ref]:children.dictionary){
     if(child_ref.kind!=Value::Kind::Reference)return false;
     const XrefEntry* child_entry=nullptr;
     if(!entry(child_ref,child_entry))return false;
     StreamRange child_range;
     if(!locate_indirect_stream(data,size,*child_entry,xref,child_range))
      return false;
     const Value* child_type=get(child_range.dictionary,"Subtype");
     if(!child_type||child_type->kind!=Value::Kind::Name)return false;
     if(child_type->text=="Image"){
      const auto page=page_names.find({child_ref.integer,
                                        int64_t(child_ref.generation)});
      if(page!=page_names.end()){
       form.xobject_aliases.emplace(alias,page->second);
      }else{
       PageImage image;
       if(!decode_page_image(child_range,image))return false;
       form.local_images.emplace(alias,std::move(image));
      }
     }else if(child_type->text=="Form"){
      PageForm child;
      if(!load_local_form(child_ref,depth+1,visiting,page_names,child))
       return false;
      form.local_forms.emplace(alias,std::move(child));
     }else return false;
    }
   }
  }
  visiting.erase(id);
  return true;
 }
 bool walk_color_spaces(const Value& ref,size_t depth,size_t& current,
  size_t target,std::set<size_t>& active,const Value* inherited,
  std::map<std::string,std::string>& aliases,
  std::map<std::string,SeparationColorSpace>& separations) const {
  if(depth>64||ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  if(!ScopedPageXref::path_contains(data,size,target,ref))return true;
  const size_t id=size_t(ref.integer);
  if(!active.insert(id).second)return false;
  Value node,owned;
  if(!object(ref,node)){active.erase(id);return false;}
  if(const Value* resources=get(node,"Resources")){
   if(resources->kind==Value::Kind::Reference){
    if(!object(*resources,owned)){active.erase(id);return false;}
   }else owned=*resources;
   if(owned.kind!=Value::Kind::Dictionary){active.erase(id);return false;}
   inherited=&owned;
  }
  const Value* type=get(node,"Type");
  if(!type||type->kind!=Value::Kind::Name){active.erase(id);return false;}
  if(type->text=="Page"){
   if(current==target&&inherited){
    if(const Value* spaces=get(*inherited,"ColorSpace")){
     Value definitions;
     if(spaces->kind==Value::Kind::Reference){
      if(!object(*spaces,definitions)){active.erase(id);return false;}
     }else definitions=*spaces;
     if(definitions.kind!=Value::Kind::Dictionary||
        definitions.dictionary.size()>128){active.erase(id);return false;}
     for(const auto& [name,definition]:definitions.dictionary){
      Value resolved;
      if(definition.kind==Value::Kind::Reference){
       if(!object(definition,resolved)){active.erase(id);return false;}
      }else resolved=definition;
      if(resolved.kind==Value::Kind::Name){
       if(resolved.text=="DeviceGray"||resolved.text=="DeviceRGB"||
          resolved.text=="DeviceCMYK")aliases.emplace(name,resolved.text);
       continue;
      }
      if(resolved.kind!=Value::Kind::Array||resolved.array.size()!=4||
         resolved.array[0].kind!=Value::Kind::Name||
         resolved.array[0].text!="Separation"||
         resolved.array[2].kind!=Value::Kind::Name)continue;
      SeparationColorSpace sep;
      sep.alternate=resolved.array[2].text;
      sep.components=sep.alternate=="DeviceGray"?1u:
         sep.alternate=="DeviceRGB"?3u:
         sep.alternate=="DeviceCMYK"?4u:0u;
      if(!sep.components)continue;
      Value function;
      if(resolved.array[3].kind==Value::Kind::Reference){
       if(!object(resolved.array[3],function)){active.erase(id);return false;}
      }else function=resolved.array[3];
      const Value* ft=get(function,"FunctionType");
      const Value* exponent=get(function,"N");
      if(!ft||ft->kind!=Value::Kind::Integer||ft->integer!=2||
         !exponent)continue;
      double n=0;
      if(exponent->kind==Value::Kind::Integer)n=double(exponent->integer);
      else if(exponent->kind==Value::Kind::Real)n=exponent->real;
      else continue;
      if(!std::isfinite(n)||n<=0||n>100)continue;
      sep.exponent=n;
      for(unsigned c=0;c<sep.components;++c)sep.c1[c]=1;
      bool valid=true;
      for(const char* key:{"C0","C1"}){
       const Value* values=get(function,key);
       if(!values)continue;
       if(values->kind!=Value::Kind::Array||
          values->array.size()!=sep.components){valid=false;break;}
       for(unsigned c=0;c<sep.components;++c){
        const Value& v=values->array[c];
        double d=0;
        if(v.kind==Value::Kind::Integer)d=double(v.integer);
        else if(v.kind==Value::Kind::Real)d=v.real;
        else {valid=false;break;}
        if(!std::isfinite(d)||d<0||d>1){valid=false;break;}
        if(key[1]=='0')sep.c0[c]=d;else sep.c1[c]=d;
       }
       if(!valid)break;
      }
      if(valid)separations.emplace(name,std::move(sep));
     }
    }
   }
   ++current;active.erase(id);return true;
  }
  if(type->text!="Pages"){active.erase(id);return false;}
  const Value* kids=get(node,"Kids");
  if(!kids||kids->kind!=Value::Kind::Array){active.erase(id);return false;}
  if(current>target){active.erase(id);return true;}
  for(const Value& child:kids->array){
   if(current>target)break;
if(!skip_preceding_subtree(child,current,target) && !walk_color_spaces(child,depth+1,current,target,active,inherited,
                         aliases,separations)){
    active.erase(id);return false;
   }
  }
  active.erase(id);return true;
 }
 bool walk_forms(const Value& ref,size_t depth,size_t& current,size_t target,
  std::set<size_t>& active,const Value* inherited,
  std::map<std::string,PageForm>& output) const {
  if(depth>64||ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  if(!ScopedPageXref::path_contains(data,size,target,ref))return true;
  const size_t id=size_t(ref.integer);
  if(!active.insert(id).second)return false;
  Value node,owned;
  if(!object(ref,node)){active.erase(id);return false;}
  if(const Value* resources=get(node,"Resources")){
   if(resources->kind==Value::Kind::Reference){
    if(!object(*resources,owned)){active.erase(id);return false;}
   }else owned=*resources;
   if(owned.kind!=Value::Kind::Dictionary){active.erase(id);return false;}
   inherited=&owned;
  }
  const Value* type=get(node,"Type");
  if(!type||type->kind!=Value::Kind::Name){active.erase(id);return false;}
  if(type->text=="Page"){
   if(current==target&&inherited){
    if(const Value* x=get(*inherited,"XObject")){
     Value entries;
     if(x->kind==Value::Kind::Reference){
      if(!object(*x,entries)){active.erase(id);return false;}
     }else entries=*x;
     if(entries.kind!=Value::Kind::Dictionary||entries.dictionary.size()>256){
      active.erase(id);return false;
     }
     // Build a reverse lookup for objects already resolved at page scope.
     // A Form may rename these entries in its own XObject dictionary.
     std::map<std::pair<int64_t,int64_t>,std::string> page_names;
     for(const auto& [page_name,page_ref]:entries.dictionary)
      if(page_ref.kind==Value::Kind::Reference)
       page_names.emplace(std::make_pair(page_ref.integer,
                           int64_t(page_ref.generation)),page_name);
     for(const auto& [name,objref]:entries.dictionary){
      if(objref.kind!=Value::Kind::Reference){active.erase(id);return false;}
      const XrefEntry* ent=nullptr;
      if(!entry(objref,ent)){active.erase(id);return false;}
      StreamRange range;
      if(!locate_indirect_stream(data,size,*ent,xref,range)){
       active.erase(id);return false;
      }
      const Value* subtype=get(range.dictionary,"Subtype");
      if(!subtype||subtype->kind!=Value::Kind::Name){active.erase(id);return false;}
      if(subtype->text!="Form")continue;
      PageForm form;
      std::set<size_t> visiting;
      if(!load_local_form(objref,0,visiting,page_names,form)){
       active.erase(id);return false;
      }
      output.emplace(name,std::move(form));
     }
    }
   }
   ++current;active.erase(id);return true;
  }
  if(type->text!="Pages"){active.erase(id);return false;}
  const Value* kids=get(node,"Kids");
  if(!kids||kids->kind!=Value::Kind::Array){active.erase(id);return false;}
  if(current>target){active.erase(id);return true;}
  for(const Value& child:kids->array){
   if(current>target)break;
   if(!skip_preceding_subtree(child,current,target) && !walk_forms(child,depth+1,current,target,active,inherited,output)){
    active.erase(id);return false;
   }
  }
  active.erase(id);return true;
 }
 bool walk_graphics_states(const Value& ref,size_t depth,size_t& current,size_t target,
  std::set<size_t>& active,const Value* inherited,PageGraphicsStates& output) const {
  if(depth>64||ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  if(!ScopedPageXref::path_contains(data,size,target,ref))return true;
  const size_t id=size_t(ref.integer);
  if(!active.insert(id).second)return false;
  Value node,owned;
  if(!object(ref,node)){active.erase(id);return false;}
  if(const Value* resources=get(node,"Resources")){
   if(resources->kind==Value::Kind::Reference){
    if(!object(*resources,owned)){active.erase(id);return false;}
   }else owned=*resources;
   if(owned.kind!=Value::Kind::Dictionary){active.erase(id);return false;}
   inherited=&owned;
  }
  const Value* type=get(node,"Type");
  if(!type||type->kind!=Value::Kind::Name){active.erase(id);return false;}
  if(type->text=="Page"){
   if(current==target&&inherited){
    if(const Value* source=get(*inherited,"ExtGState")){
     Value entries;
     if(source->kind==Value::Kind::Reference){
      if(!object(*source,entries)){active.erase(id);return false;}
     }else entries=*source;
     if(entries.kind!=Value::Kind::Dictionary||entries.dictionary.size()>128){
      active.erase(id);return false;
     }
     for(const auto& [name,value]:entries.dictionary){
      Value state;
      if(value.kind==Value::Kind::Reference){
       if(!object(value,state)){active.erase(id);return false;}
      }else state=value;
      if(state.kind!=Value::Kind::Dictionary){active.erase(id);return false;}
      PageGraphicsState item;
      for(const auto& [key,destination]:
          std::initializer_list<std::pair<const char*,double*>>{
           {"ca",&item.fill_alpha},{"CA",&item.stroke_alpha}}){
       if(const Value* v=get(state,key)){
        double number=0;
        if(v->kind==Value::Kind::Integer)number=double(v->integer);
        else if(v->kind==Value::Kind::Real)number=v->real;
        else {active.erase(id);return false;}
        if(!std::isfinite(number)||number<0||number>1){
         active.erase(id);return false;
        }
        *destination=number;
        if(key[0]=='c')item.has_fill_alpha=true;
        else item.has_stroke_alpha=true;
       }
      }
      // Explicit defaults are common in real ExtGState dictionaries.
      // Accept them without claiming support for non-Normal blending.
      if(const Value* blend=get(state,"BM")){
       if(blend->kind!=Value::Kind::Name||
          (blend->text!="Normal"&&blend->text!="Multiply"&&blend->text!="Screen")){
        active.erase(id);return false;
       }
       item.blend_mode=blend->text;
       item.has_blend_mode=true;
      }
      if(const Value* soft=get(state,"SMask")){
       if(soft->kind!=Value::Kind::Name||soft->text!="None"){
        active.erase(id);return false;
       }
      }
      if(get(state,"AIS")||get(state,"OP")||get(state,"op")){
       active.erase(id);return false;
      }
      output.emplace(name,item);
     }
    }
   }
   ++current;active.erase(id);return true;
  }
  if(type->text!="Pages"){active.erase(id);return false;}
  const Value* children=get(node,"Kids");
  if(!children||children->kind!=Value::Kind::Array){
   active.erase(id);return false;
  }
  if(current>target){active.erase(id);return true;}
  for(const Value& child:children->array){
   if(current>target)break;
if(!skip_preceding_subtree(child,current,target) && !walk_graphics_states(child,depth+1,current,target,active,inherited,output)){
    active.erase(id);return false;
   }
  }
  active.erase(id);return true;
 }
 bool walk_properties(const Value& ref,size_t depth,size_t& current,size_t target,
  std::set<size_t>& active,const Value* inherited,PageProperties& output) const {
  if(depth>64||ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  if(!ScopedPageXref::path_contains(data,size,target,ref))return true;
  const size_t id=static_cast<size_t>(ref.integer);
  if(!active.insert(id).second)return false;
  Value node,owned_resources;
  if(!object(ref,node)){active.erase(id);return false;}
  if(const Value* resources=get(node,"Resources")){
   if(resources->kind==Value::Kind::Reference){
    if(!object(*resources,owned_resources)){active.erase(id);return false;}
   }else owned_resources=*resources;
   if(owned_resources.kind!=Value::Kind::Dictionary){active.erase(id);return false;}
   inherited=&owned_resources;
  }
  const Value* type=get(node,"Type");
  if(!type||type->kind!=Value::Kind::Name){active.erase(id);return false;}
  if(type->text=="Page"){
   if(current==target&&inherited){
    if(const Value* source=get(*inherited,"Properties")){
     Value properties;
     if(source->kind==Value::Kind::Reference){
      if(!object(*source,properties)){active.erase(id);return false;}
     }else properties=*source;
     if(properties.kind!=Value::Kind::Dictionary||
        properties.dictionary.size()>4096){active.erase(id);return false;}
     for(const auto& [key,value]:properties.dictionary){
      Value dictionary;
      if(value.kind==Value::Kind::Reference){
       if(!object(value,dictionary)){active.erase(id);return false;}
      }else dictionary=value;
      if(dictionary.kind!=Value::Kind::Dictionary){active.erase(id);return false;}
      PageProperty property;
      if(const Value* lang=get(dictionary,"Lang")){
       if(lang->kind!=Value::Kind::String||lang->text.size()>64){
        active.erase(id);return false;
       }
       property.language=lang->text;
      }
      if(const Value* actual=get(dictionary,"ActualText")){
       if(actual->kind!=Value::Kind::String||actual->text.size()>4*1024*1024){
        active.erase(id);return false;
       }
       property.actual_text=actual->text;
       property.has_actual_text=true;
      }
      output.emplace(key,std::move(property));
     }
    }
   }
   ++current;active.erase(id);return true;
  }
  if(type->text!="Pages"){active.erase(id);return false;}
  const Value* kids=get(node,"Kids");
  if(!kids||kids->kind!=Value::Kind::Array){active.erase(id);return false;}
  if(current>target){active.erase(id);return true;}
  for(const auto& child:kids->array){
   if(current>target)break;
if(!skip_preceding_subtree(child,current,target) && !walk_properties(child,depth+1,current,target,active,inherited,output)){
    active.erase(id);return false;
   }
  }
  active.erase(id);return true;
 }
 bool walk_fonts(const Value& ref,size_t depth,size_t& current,size_t target,
  std::set<size_t>& active,const Value* inherited,
  std::map<std::string,PageFont>& output,
  const std::string& catalog_language) const {
  if(depth>64||ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  if(!ScopedPageXref::path_contains(data,size,target,ref))return true;
  const size_t id=static_cast<size_t>(ref.integer);
  if(!active.insert(id).second)return false;
  Value node;
  if(!object(ref,node)){active.erase(id);return false;}
  Value owned_resources;
  if(const Value* resources=get(node,"Resources")){
   if(resources->kind==Value::Kind::Reference){
    if(!object(*resources,owned_resources)){active.erase(id);return false;}
   }else owned_resources=*resources;
   if(owned_resources.kind!=Value::Kind::Dictionary){active.erase(id);return false;}
   inherited=&owned_resources;
  }
  const Value* type=get(node,"Type");
  if(!type||type->kind!=Value::Kind::Name){active.erase(id);return false;}
  if(type->text=="Page"){
   if(current==target&&inherited){
    Value font_dictionary;
    if(const Value* fonts=get(*inherited,"Font")){
     if(fonts->kind==Value::Kind::Reference){
      if(!object(*fonts,font_dictionary)){active.erase(id);return false;}
     }else font_dictionary=*fonts;
     if(font_dictionary.kind!=Value::Kind::Dictionary||
        font_dictionary.dictionary.size()>4096){active.erase(id);return false;}
     for(const auto& [key,entry]:font_dictionary.dictionary){
      Value font;
      if(entry.kind==Value::Kind::Reference){
       if(!object(entry,font)){active.erase(id);return false;}
      }else font=entry;
      PageFont resource;
      if(!parse_font_dictionary(font,inherited,resource)){
       active.erase(id);return false;
      }
      resource.language=catalog_language;
      if(const Value* lang=get(node,"Lang")){
       if(lang->kind!=Value::Kind::String||lang->text.size()>64){
        active.erase(id);return false;
       }
       resource.language=lang->text;
      }
      output.emplace(key,std::move(resource));
     }
    }
   }
   ++current;active.erase(id);return true;
  }
  if(type->text!="Pages"){active.erase(id);return false;}
  const Value* kids=get(node,"Kids");
  if(!kids||kids->kind!=Value::Kind::Array){active.erase(id);return false;}
  if(current>target){active.erase(id);return true;}
  for(const Value& child:kids->array){
   if(current>target)break;
if(!skip_preceding_subtree(child,current,target) && !walk_fonts(child,depth+1,current,target,active,inherited,output,catalog_language)){
    active.erase(id);return false;
   }
  }
  active.erase(id);return true;
 }
 bool walk_box(const Value& ref,size_t depth,size_t& current,size_t target,
               std::set<size_t>& active,const MediaBox* inherited,const MediaBox* inherited_crop,unsigned inherited_rotation,MediaBox& output) const {
  if(depth>64||ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  if(!ScopedPageXref::path_contains(data,size,target,ref))return true;
  size_t id=(size_t)ref.integer;
  if(!active.insert(id).second)return false;
  Value node;
  if(!object(ref,node)){active.erase(id);return false;}
  MediaBox own{};
  const Value *media=get(node,"MediaBox");
  if(media){if(!box_value(*media,own)){active.erase(id);return false;}inherited=&own;}
  const Value *rotate=get(node,"Rotate");
  if(rotate){
   if(rotate->kind!=Value::Kind::Integer||rotate->integer%90!=0){active.erase(id);return false;}
   long long angle=rotate->integer%360;
   if(angle<0)angle+=360;
   inherited_rotation=(unsigned)angle;
  }
  MediaBox own_crop{};
  const Value *crop=get(node,"CropBox");
  if(crop){if(!box_value(*crop,own_crop)){active.erase(id);return false;}inherited_crop=&own_crop;}
  const Value *type=get(node,"Type");
  if(!type||type->kind!=Value::Kind::Name){active.erase(id);return false;}
  if(type->text=="Page"){
   if(current==target){if(!inherited){active.erase(id);return false;}
    output=*inherited;
    output.rotation=inherited_rotation;
    if(inherited_crop){
     output.x0=std::max(output.x0,inherited_crop->x0);
     output.y0=std::max(output.y0,inherited_crop->y0);
     output.x1=std::min(output.x1,inherited_crop->x1);
     output.y1=std::min(output.y1,inherited_crop->y1);
     if(output.x0>=output.x1||output.y0>=output.y1){active.erase(id);return false;}
    }}
   ++current;active.erase(id);return true;
  }
  if(type->text!="Pages"){active.erase(id);return false;}
  const Value *kids=get(node,"Kids");
  if(!kids||kids->kind!=Value::Kind::Array){active.erase(id);return false;}
  if(current>target){active.erase(id);return true;}
  for(const Value& child:kids->array){
   if(current>target)break;
   if(!skip_preceding_subtree(child,current,target) && !walk_box(child,depth+1,current,target,active,inherited,inherited_crop,inherited_rotation,output)){active.erase(id);return false;}
  }
  active.erase(id);return true;
 }
 bool walk(const Value& ref,size_t depth,size_t& current,size_t target,
           std::set<size_t>& active,std::vector<uint8_t>& result,size_t limit) const {
  if(depth>64||ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  if(!ScopedPageXref::path_contains(data,size,target,ref))return true;
  size_t id=(size_t)ref.integer;
  if(!active.insert(id).second)return false;
  Value node;
  if(!object(ref,node)){active.erase(id);return false;}
  const Value *type=get(node,"Type");
  if(!type||type->kind!=Value::Kind::Name){active.erase(id);return false;}
  if(type->text=="Page"){
   bool ok=true;
   if(current==target){
    const Value *contents=get(node,"Contents");
    if(contents){
     if(contents->kind==Value::Kind::Reference)ok=stream(*contents,result,limit);
     else if(contents->kind==Value::Kind::Array){
      for(const Value& child:contents->array){
       std::vector<uint8_t> part;
       if(!stream(child,part,limit-result.size())){ok=false;break;}
       result.insert(result.end(),part.begin(),part.end());
       if(result.size()<limit)result.push_back('\n');
      }
     }else ok=false;
    }
   }
   ++current;active.erase(id);return ok;
  }
  if(type->text!="Pages"){active.erase(id);return false;}
  const Value *kids=get(node,"Kids");
  if(!kids||kids->kind!=Value::Kind::Array){active.erase(id);return false;}
  if(current>target){active.erase(id);return true;}
  for(const Value& child:kids->array){
   if(current>target)break;
   if(!skip_preceding_subtree(child,current,target) && !walk(child,depth+1,current,target,active,result,limit)){active.erase(id);return false;}
  }
  active.erase(id);return true;
 }
};
// Centralized catalog validation for all page resource readers.
static bool resolve_page_catalog(const Reader& reader,const Value& trailer,
 Value& catalog,const Value*& pages){
 const Value* root=get(trailer,"Root");
 if(!root||get(trailer,"Encrypt"))return false;
 if(!reader.object(*root,catalog))return false;
 const Value* type=get(catalog,"Type");
 pages=get(catalog,"Pages");
 return type&&type->kind==Value::Kind::Name&&
  type->text=="Catalog"&&pages;
}
bool read_page_contents(const uint8_t *data,size_t size,size_t index,std::vector<uint8_t>& contents,size_t limit){
 contents.clear();if(!data||limit==0)return false;
 ScopedPageXref::View view;
 if(!view.open(data,size))return false;
 const auto& xref=*view.entries;const auto& compressed=*view.compressed;const auto& trailer=*view.trailer;
 if(get(trailer,"Encrypt"))return false;
 Reader reader{data,size,xref,compressed,{}};
 Value catalog;const Value* pages=nullptr;
 if(!resolve_page_catalog(reader,trailer,catalog,pages))return false;
 std::set<size_t> active;size_t current=0;reader.prepare_path(*pages,index,current);
 return reader.walk(*pages,0,current,index,active,contents,limit)&&index<current;
}
bool read_page_box(const uint8_t *data,size_t size,size_t index,MediaBox& box){
 box={0,0,0,0};if(!data)return false;
 ScopedPageXref::View view;
 if(!view.open(data,size))return false;
 const auto& xref=*view.entries;const auto& compressed=*view.compressed;const auto& trailer=*view.trailer;
 if(get(trailer,"Encrypt"))return false;
 Reader reader{data,size,xref,compressed,{}};
 Value catalog;const Value* pages=nullptr;
 if(!resolve_page_catalog(reader,trailer,catalog,pages))return false;
 std::set<size_t> active;size_t current=0;reader.prepare_path(*pages,index,current);
 return reader.walk_box(*pages,0,current,index,active,nullptr,nullptr,0,box)&&index<current;
}

bool read_page_fonts(const uint8_t* data,size_t size,size_t index,
 std::map<std::string,PageFont>& fonts){
 if(!data)return false;
 ScopedPageXref::View view;
 if(!view.open(data,size))return false;
 const auto& xref=*view.entries;const auto& compressed=*view.compressed;const auto& trailer=*view.trailer;
 Reader reader{data,size,xref,compressed,{}};
 Value catalog;const Value* pages=nullptr;
 if(!resolve_page_catalog(reader,trailer,catalog,pages))return false;
 std::map<std::string,PageFont> result;
 size_t current=0;reader.prepare_path(*pages,index,current);std::set<size_t> active;
 std::string document_language;
 if(const Value* lang=get(catalog,"Lang")){
  if(lang->kind!=Value::Kind::String||lang->text.size()>64)return false;
  document_language=lang->text;
 }
 if(!reader.walk_fonts(*pages,0,current,index,active,nullptr,result,document_language)||index>=current)
  return false;
 fonts=std::move(result);
 return true;
}

bool read_page_graphics_states(const uint8_t* data,size_t size,size_t index,
 PageGraphicsStates& states){
 if(!data)return false;
 ScopedPageXref::View view;
 if(!view.open(data,size))return false;
 const auto& xref=*view.entries;const auto& compressed=*view.compressed;const auto& trailer=*view.trailer;
 if(get(trailer,"Encrypt"))return false;
 Reader reader{data,size,xref,compressed,{}};
 Value catalog;const Value* pages=nullptr;
 if(!resolve_page_catalog(reader,trailer,catalog,pages))return false;
 size_t current=0;reader.prepare_path(*pages,index,current);std::set<size_t> active;
 PageGraphicsStates found;
 if(!reader.walk_graphics_states(*pages,0,current,index,active,nullptr,found)||
    index>=current)return false;
 states=std::move(found);return true;
}

bool read_page_properties(const uint8_t* data,size_t size,size_t index,
 PageProperties& properties){
 if(!data)return false;
 ScopedPageXref::View view;
 if(!view.open(data,size))return false;
 const auto& xref=*view.entries;const auto& compressed=*view.compressed;const auto& trailer=*view.trailer;
 if(get(trailer,"Encrypt"))return false;
 Reader reader{data,size,xref,compressed,{}};
 Value catalog;const Value* pages=nullptr;
 if(!resolve_page_catalog(reader,trailer,catalog,pages))return false;
 size_t current=0;reader.prepare_path(*pages,index,current);
 std::set<size_t> active;
 PageProperties result;
 if(!reader.walk_properties(*pages,0,current,index,active,nullptr,result)||
    index>=current)return false;
 properties=std::move(result);return true;
}

bool read_page_images(const uint8_t* data,size_t size,size_t index,
 std::map<std::string,PageImage>& images){
 if(!data)return false;
 ScopedPageXref::View view;
 if(!view.open(data,size))return false;
 const auto& xref=*view.entries;const auto& compressed=*view.compressed;const auto& trailer=*view.trailer;
 if(get(trailer,"Encrypt"))return false;
 Reader reader{data,size,xref,compressed,{}};
 Value catalog;const Value* pages=nullptr;
 if(!resolve_page_catalog(reader,trailer,catalog,pages))return false;
 size_t current=0;reader.prepare_path(*pages,index,current);std::set<size_t> active;
 std::map<std::string,PageImage> result;
 if(!reader.walk_images(*pages,0,current,index,active,nullptr,result)||
    index>=current)return false;
 images=std::move(result);return true;
}

bool read_page_color_spaces(const uint8_t* data,size_t size,size_t index,
 std::map<std::string,std::string>& aliases,
 std::map<std::string,SeparationColorSpace>& separations){
 if(!data)return false;
 ScopedPageXref::View view;
 if(!view.open(data,size))return false;
 const auto& xref=*view.entries;const auto& compressed=*view.compressed;const auto& trailer=*view.trailer;
 if(get(trailer,"Encrypt"))return false;
 Reader reader{data,size,xref,compressed,{}};
 Value catalog;const Value* pages=nullptr;
 if(!resolve_page_catalog(reader,trailer,catalog,pages))return false;
 size_t current=0;reader.prepare_path(*pages,index,current);
 std::set<size_t> active;
 std::map<std::string,std::string> named;
 std::map<std::string,SeparationColorSpace> tint;
 if(!reader.walk_color_spaces(*pages,0,current,index,active,nullptr,
                               named,tint)||index>=current)return false;
 aliases=std::move(named);
 separations=std::move(tint);
 return true;
}

bool read_page_forms(const uint8_t* data,size_t size,size_t index,
 std::map<std::string,PageForm>& forms){
 if(!data)return false;
 ScopedPageXref::View view;
 if(!view.open(data,size))return false;
 const auto& xref=*view.entries;const auto& compressed=*view.compressed;const auto& trailer=*view.trailer;
 if(get(trailer,"Encrypt"))return false;
 Reader reader{data,size,xref,compressed,{}};
 Value catalog;const Value* pages=nullptr;
 if(!resolve_page_catalog(reader,trailer,catalog,pages))return false;
 size_t current=0;reader.prepare_path(*pages,index,current);std::set<size_t> active;
 std::map<std::string,PageForm> result;
 if(!reader.walk_forms(*pages,0,current,index,active,nullptr,result)||
    index>=current)return false;
 forms=std::move(result);return true;
}

}
