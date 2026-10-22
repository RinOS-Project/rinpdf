#include "rinpdf/rinpdf.h"
#include "../parser/pages.h"
#include "../parser/xref_stream.h"
#include "../parser/object_resolver.h"
#include "../parser/value.h"
#include "../parser/contents.h"
#include "../renderer/rectangles.h"
#include "rinpdf/render.h"
#include <cstdio>
#include <cwchar>
#include <climits>
#include <cstring>
#include <new>
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <set>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <span>
#include <mutex>
#include <atomic>
#include <chrono>
#include <array>
#include <cstdint>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif
template<class T>
struct PageLru {
 std::map<size_t,std::shared_ptr<const T>> entries;
 std::map<size_t,size_t> ages;
 std::map<size_t,size_t> weights;
 size_t resident_bytes=0;
 std::atomic<uint64_t>* shared_clock=nullptr;
 uint64_t tick(){return shared_clock?shared_clock->fetch_add(1,std::memory_order_relaxed)+1:0;}
 std::shared_ptr<const T> find(size_t page) {
  auto it=entries.find(page);
  if(it==entries.end())return {};
  ages[page]=tick();
  return it->second;
 }
 std::shared_ptr<const T> insert(size_t page,std::shared_ptr<const T> value,size_t limit,size_t weight){
  auto it=entries.find(page);
  if(it!=entries.end()){ages[page]=tick();return it->second;}
  if(entries.size()>=limit && !ages.empty()){
   auto oldest=std::min_element(ages.begin(),ages.end(),
    [](const auto& a,const auto& b){return a.second<b.second;});
   auto old_weight=weights.find(oldest->first);
   if(old_weight!=weights.end()){resident_bytes-=old_weight->second;weights.erase(old_weight);}
   entries.erase(oldest->first);ages.erase(oldest);
  }
  entries.emplace(page,value);
  try {weights.emplace(page,weight);}catch(...){entries.erase(page);throw;}
  resident_bytes+=weight;
  ages[page]=tick();
  return value;
 }
};
struct rinpdf_document {
 std::vector<uint8_t> bytes;
 const uint8_t* mapped=nullptr;
 size_t mapped_size=0;
 unsigned major=0,minor=0;
 mutable std::once_flag xref_once;
 mutable bool xref_valid=false;
 mutable size_t cached_ordinary_objects=0;
 mutable size_t cached_compressed_objects=0;
 mutable std::vector<rinpdf::parser::XrefEntry> cached_xref;
 mutable std::vector<rinpdf::parser::XrefStreamEntry> cached_compressed;
 mutable rinpdf::parser::Value cached_trailer;
 mutable rinpdf::parser::VerifiedPageCounts verified_page_counts;
 mutable std::once_flag page_count_once;
 mutable size_t cached_page_count=0;
 mutable bool page_count_valid=false;
 mutable size_t render_cache_budget=64*1024*1024;
 mutable std::atomic<uint64_t> global_cache_clock{0};
 mutable std::atomic<uint64_t> preparation_ns{0};
 mutable std::atomic<uint64_t> raster_ns{0};
 mutable std::atomic<uint64_t> prepared_calls{0};
 mutable std::atomic<uint64_t> rendered_calls{0};
 mutable std::atomic<uint64_t> cache_hits{0};
 mutable std::atomic<uint64_t> cache_misses{0};
 mutable std::atomic<uint64_t> cache_evictions{0};
 mutable std::array<std::mutex,64> page_prepare_locks;
 mutable std::mutex property_mutex;
 mutable PageLru<rinpdf::parser::PageProperties> property_cache;
 mutable std::mutex form_mutex;
 mutable PageLru<rinpdf::renderer::FormResources> form_cache;
 mutable std::mutex graphics_mutex;
 mutable PageLru<rinpdf::parser::PageGraphicsStates> graphics_cache;
 mutable std::mutex image_mutex;
 mutable PageLru<rinpdf::renderer::ImageResources> image_cache;
 mutable std::mutex font_mutex;
 mutable PageLru<rinpdf::renderer::FontResources> font_cache;
 mutable std::mutex content_mutex;
 mutable std::map<size_t,std::shared_ptr<const std::vector<uint8_t>>> content_cache;
 mutable size_t content_cache_bytes=0;
 mutable size_t content_cache_budget=16*1024*1024;
 mutable std::map<size_t,size_t> content_access;

 mutable std::mutex geometry_mutex;
 mutable std::map<size_t,rinpdf_page_geometry> geometry_cache;
 rinpdf_document(){
  property_cache.shared_clock=&global_cache_clock;
  form_cache.shared_clock=&global_cache_clock;
  graphics_cache.shared_clock=&global_cache_clock;
  image_cache.shared_clock=&global_cache_clock;
  font_cache.shared_clock=&global_cache_clock;
 }
 const uint8_t* data() const {return mapped?mapped:bytes.data();}
 size_t size() const {return mapped?mapped_size:bytes.size();}
 ~rinpdf_document(){
#ifdef _WIN32
  if(mapped)UnmapViewOfFile(mapped);
#else
  if(mapped)munmap(const_cast<uint8_t*>(mapped),mapped_size);
#endif
 }
};
extern "C" rinpdf_status rinpdf_document_open_memory(const uint8_t *data,size_t size,rinpdf_document **out) {
 if(!out) return RINPDF_INVALID_ARGUMENT;
 *out=nullptr;
 if(!data || size<8) return RINPDF_INVALID_PDF;
 if(std::memcmp(data,"%PDF-",5)!=0 || data[5]<'0'||data[5]>'9'||data[6]!='.'||data[7]<'0'||data[7]>'9') return RINPDF_INVALID_PDF;
 try {
  auto d=std::make_unique<rinpdf_document>();
  d->major=data[5]-'0';d->minor=data[7]-'0';
  d->bytes.assign(data,data+size);
  *out=d.release();return RINPDF_OK;
 }
 catch(const std::bad_alloc&) {return RINPDF_OUT_OF_MEMORY;}
}
extern "C" rinpdf_status rinpdf_document_open_file(const char *path,rinpdf_document **out) {
 if(!path||!out)return RINPDF_INVALID_ARGUMENT;
 *out=nullptr;
#ifndef _WIN32
 int fd=open(path,O_RDONLY);
 if(fd<0)return RINPDF_IO_ERROR;
 struct stat statbuf{};
 if(fstat(fd,&statbuf)!=0){close(fd);return RINPDF_IO_ERROR;}
 if(!S_ISREG(statbuf.st_mode)||statbuf.st_size<8){
  close(fd);return RINPDF_INVALID_PDF;
 }
 if(static_cast<uintmax_t>(statbuf.st_size)>SIZE_MAX){
  close(fd);return RINPDF_UNSUPPORTED;
 }
 const size_t length=(size_t)statbuf.st_size;
 void* memory=mmap(nullptr,length,PROT_READ,MAP_PRIVATE,fd,0);
 close(fd);
 if(memory==MAP_FAILED)return RINPDF_IO_ERROR;
 const auto* bytes=static_cast<const uint8_t*>(memory);
 if(std::memcmp(bytes,"%PDF-",5)!=0||bytes[5]<'0'||bytes[5]>'9'||
    bytes[6]!='.'||bytes[7]<'0'||bytes[7]>'9'){
  munmap(memory,length);return RINPDF_INVALID_PDF;
 }
 rinpdf_document* doc=new(std::nothrow) rinpdf_document;
 if(!doc){munmap(memory,length);return RINPDF_OUT_OF_MEMORY;}
 doc->mapped=bytes;doc->mapped_size=length;
 doc->major=bytes[5]-'0';doc->minor=bytes[7]-'0';
 *out=doc;return RINPDF_OK;
#else
 // Open UTF-8 paths through the Unicode Win32 file API.
 const size_t path_bytes=std::strlen(path);
 if(!path_bytes||path_bytes>INT_MAX)return RINPDF_INVALID_ARGUMENT;
 const int wide_length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
    path,(int)path_bytes,nullptr,0);
 if(wide_length<=0)return RINPDF_INVALID_ARGUMENT;
 std::wstring wide_path((size_t)wide_length,L'\0');
 if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,(int)path_bytes,
     wide_path.data(),wide_length)!=wide_length)return RINPDF_INVALID_ARGUMENT;
 HANDLE file=CreateFileW(wide_path.c_str(),GENERIC_READ,FILE_SHARE_READ,
     nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file==INVALID_HANDLE_VALUE)return RINPDF_IO_ERROR;
 LARGE_INTEGER file_size{};
 if(!GetFileSizeEx(file,&file_size)){CloseHandle(file);return RINPDF_IO_ERROR;}
 if(file_size.QuadPart<8){CloseHandle(file);return RINPDF_INVALID_PDF;}
 if((unsigned long long)file_size.QuadPart>SIZE_MAX){
  CloseHandle(file);return RINPDF_UNSUPPORTED;
 }
 HANDLE mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY,0,0,nullptr);
 if(!mapping){CloseHandle(file);return RINPDF_IO_ERROR;}
 void* memory=MapViewOfFile(mapping,FILE_MAP_READ,0,0,0);
 CloseHandle(mapping);
 CloseHandle(file);
 if(!memory)return RINPDF_IO_ERROR;
 const auto* bytes=static_cast<const uint8_t*>(memory);
 if(std::memcmp(bytes,"%PDF-",5)!=0||bytes[5]<'0'||bytes[5]>'9'||
    bytes[6]!='.'||bytes[7]<'0'||bytes[7]>'9'){
  UnmapViewOfFile(memory);return RINPDF_INVALID_PDF;
 }
 auto* doc=new(std::nothrow) rinpdf_document;
 if(!doc){UnmapViewOfFile(memory);return RINPDF_OUT_OF_MEMORY;}
 doc->mapped=bytes;doc->mapped_size=(size_t)file_size.QuadPart;
 doc->major=bytes[5]-'0';doc->minor=bytes[7]-'0';
 *out=doc;return RINPDF_OK;
#endif
}
extern "C" void rinpdf_document_close(rinpdf_document *d){delete d;}
extern "C" rinpdf_status rinpdf_document_content_cache_stats(
 const rinpdf_document* doc,size_t* bytes,size_t* entries,size_t* budget){
 if(!doc||!bytes||!entries||!budget)return RINPDF_INVALID_ARGUMENT;
 std::lock_guard<std::mutex> lock(doc->content_mutex);
 *bytes=doc->content_cache_bytes;
 *entries=doc->content_cache.size();
 *budget=doc->content_cache_budget;
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_document_set_content_cache_budget(
 rinpdf_document* doc,size_t budget){
 if(!doc)return RINPDF_INVALID_ARGUMENT;
 std::lock_guard<std::mutex> lock(doc->content_mutex);
 doc->content_cache_budget=budget;
 while(doc->content_cache_bytes>budget && !doc->content_access.empty()){
  auto oldest=std::min_element(doc->content_access.begin(),doc->content_access.end(),
   [](const auto& a,const auto& b){return a.second<b.second;});
  auto it=doc->content_cache.find(oldest->first);
  if(it!=doc->content_cache.end()){
   doc->content_cache_bytes-=it->second->size();
   doc->content_cache.erase(it);
  }
  doc->content_access.erase(oldest);
 }
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_document_clear_content_cache(rinpdf_document* doc){
 if(!doc)return RINPDF_INVALID_ARGUMENT;
 std::lock_guard<std::mutex> lock(doc->content_mutex);
 doc->content_cache.clear();
 doc->content_access.clear();
 doc->content_cache_bytes=0;
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_document_render_cache_counts(
 const rinpdf_document* doc,rinpdf_render_cache_counts* counts){
 if(!doc||!counts)return RINPDF_INVALID_ARGUMENT;
 std::scoped_lock lock(doc->content_mutex,doc->font_mutex,doc->image_mutex,
  doc->form_mutex,doc->graphics_mutex,doc->property_mutex,doc->geometry_mutex);
 counts->content_pages=doc->content_cache.size();
 counts->font_pages=doc->font_cache.entries.size();
 counts->image_pages=doc->image_cache.entries.size();
 counts->form_pages=doc->form_cache.entries.size();
 counts->graphics_pages=doc->graphics_cache.entries.size();
 counts->property_pages=doc->property_cache.entries.size();
 counts->geometry_pages=doc->geometry_cache.size();
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_document_clear_render_caches(rinpdf_document* doc){
 if(!doc)return RINPDF_INVALID_ARGUMENT;
 // All cached resources use shared ownership so ongoing renders remain valid.
 {
  std::scoped_lock lock(doc->content_mutex,doc->font_mutex,doc->image_mutex,
   doc->form_mutex,doc->graphics_mutex,doc->property_mutex,doc->geometry_mutex);
  doc->content_cache.clear();doc->content_access.clear();doc->content_cache_bytes=0;
  doc->font_cache.entries.clear();doc->font_cache.ages.clear();
  doc->image_cache.entries.clear();doc->image_cache.ages.clear();
  doc->form_cache.entries.clear();doc->form_cache.ages.clear();
  doc->graphics_cache.entries.clear();doc->graphics_cache.ages.clear();
  doc->property_cache.entries.clear();doc->property_cache.ages.clear();
  doc->geometry_cache.clear();
 }
 {
  std::lock_guard<std::mutex> lock(doc->verified_page_counts.mutex);
  doc->verified_page_counts.paths.clear();
  doc->verified_page_counts.path_ages.clear();
  doc->verified_page_counts.counts.clear();
 }
 return RINPDF_OK;
}

namespace {
size_t render_cache_estimate_locked(const rinpdf_document& d){
 size_t bytes=d.content_cache_bytes;
 for(const auto& [page,fonts]:d.font_cache.entries){
  bytes+=sizeof(*fonts);
  for(const auto& [name,font]:*fonts)
   bytes+=name.size()+sizeof(font)+font.embedded_truetype.size();
 }
 for(const auto& [page,images]:d.image_cache.entries){
  bytes+=sizeof(*images);
  for(const auto& [name,image]:*images)
   bytes+=name.size()+sizeof(image)+image.pixels.size()+
     image.alpha.size()+image.cmyk_pixels.size();
 }
 for(const auto& [page,forms]:d.form_cache.entries){
  bytes+=sizeof(*forms);
  for(const auto& [name,form]:*forms)
   bytes+=name.size()+sizeof(form)+form.content.size();
 }
 for(const auto& [page,graphics]:d.graphics_cache.entries)
  bytes+=sizeof(*graphics)+graphics->size()*128;
 for(const auto& [page,properties]:d.property_cache.entries){
  bytes+=sizeof(*properties);
  for(const auto& [key,value]:*properties)
   bytes+=key.size()+sizeof(value)+value.language.size()+value.actual_text.size();
 }
 bytes+=d.geometry_cache.size()*sizeof(rinpdf_page_geometry);
 {
  std::lock_guard<std::mutex> guard(d.verified_page_counts.mutex);
  using Key=std::pair<int64_t,int64_t>;
  bytes+=d.verified_page_counts.counts.size()*(sizeof(Key)+sizeof(size_t)+48);
  for(const auto& [page,path]:d.verified_page_counts.paths)
   bytes+=sizeof(page)+path.size()*(sizeof(Key)+48)+48;
 }
 return bytes;
}
bool trim_one_locked(rinpdf_document& d){
 // Compare the oldest access timestamps from every resource category.
 uint64_t oldest=UINT64_MAX;
 int category=-1;
 auto inspect=[&](const auto& ages,int kind){
  for(const auto& [page,stamp]:ages)
   if(stamp<oldest){oldest=stamp;category=kind;}
 };
 inspect(d.content_access,0);
 inspect(d.font_cache.ages,1);
 inspect(d.image_cache.ages,2);
 inspect(d.form_cache.ages,3);
 inspect(d.graphics_cache.ages,4);
 inspect(d.property_cache.ages,5);
 // Tree paths have their own clock and are trimmed when resource caches
 // are empty; timestamps are not comparable to the render clock.
 if(category<0){
  std::lock_guard<std::mutex> guard(d.verified_page_counts.mutex);
  auto& shared=d.verified_page_counts;
  if(!shared.path_ages.empty()){
   auto it=std::min_element(shared.path_ages.begin(),shared.path_ages.end(),
    [](const auto& a,const auto& b){return a.second<b.second;});
   shared.paths.erase(it->first);shared.path_ages.erase(it);
  }else if(!shared.counts.empty())shared.counts.erase(shared.counts.begin());
  else return false;
  d.cache_evictions.fetch_add(1,std::memory_order_relaxed);
  return true;
 }
 auto erase_oldest=[&](auto& cache){
  auto it=std::min_element(cache.ages.begin(),cache.ages.end(),
   [](const auto& a,const auto& b){return a.second<b.second;});
  if(it==cache.ages.end())return;
  cache.entries.erase(it->first);cache.ages.erase(it);
 };
 if(category==0){
  auto it=std::min_element(d.content_access.begin(),d.content_access.end(),
   [](const auto& a,const auto& b){return a.second<b.second;});
  auto entry=d.content_cache.find(it->first);
  if(entry!=d.content_cache.end()){
   d.content_cache_bytes-=entry->second->size();
   d.content_cache.erase(entry);
  }
  d.content_access.erase(it);
 }else if(category==1)erase_oldest(d.font_cache);
 else if(category==2)erase_oldest(d.image_cache);
 else if(category==3)erase_oldest(d.form_cache);
 else if(category==4)erase_oldest(d.graphics_cache);
 else erase_oldest(d.property_cache);
 d.cache_evictions.fetch_add(1,std::memory_order_relaxed);
 return true;
}
size_t trim_locked(rinpdf_document& d,size_t target){
 // Estimate once before a trim pass. Recompute only after batches
 // instead of rescanning every font/image/form after each eviction.
 size_t current=render_cache_estimate_locked(d);
 while(current>target){
  bool removed=false;
  for(unsigned batch=0;batch<16;++batch){
   if(!trim_one_locked(d))break;
   removed=true;
  }
  if(!removed)break;
  current=render_cache_estimate_locked(d);
 }
 return current;
}
}
extern "C" rinpdf_status rinpdf_document_set_render_cache_budget(
 rinpdf_document* doc,size_t budget){
 if(!doc)return RINPDF_INVALID_ARGUMENT;
 std::scoped_lock lock(doc->content_mutex,doc->font_mutex,doc->image_mutex,
  doc->form_mutex,doc->graphics_mutex,doc->property_mutex,doc->geometry_mutex);
 doc->render_cache_budget=budget;
 trim_locked(*doc,budget);
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_document_render_cache_usage(
 const rinpdf_document* doc,size_t* used,size_t* budget){
 if(!doc||!used||!budget)return RINPDF_INVALID_ARGUMENT;
 std::scoped_lock lock(doc->content_mutex,doc->font_mutex,doc->image_mutex,
  doc->form_mutex,doc->graphics_mutex,doc->property_mutex,doc->geometry_mutex);
 *used=render_cache_estimate_locked(*doc);*budget=doc->render_cache_budget;
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_document_trim_render_caches(
 rinpdf_document* doc,size_t target){
 if(!doc)return RINPDF_INVALID_ARGUMENT;
 std::scoped_lock lock(doc->content_mutex,doc->font_mutex,doc->image_mutex,
  doc->form_mutex,doc->graphics_mutex,doc->property_mutex,doc->geometry_mutex);
 trim_locked(*doc,target);
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_document_render_timings(
 const rinpdf_document* doc,rinpdf_render_timings* out){
 if(!doc||!out)return RINPDF_INVALID_ARGUMENT;
 out->preparation_ns=doc->preparation_ns.load(std::memory_order_relaxed);
 out->raster_ns=doc->raster_ns.load(std::memory_order_relaxed);
 out->prepared_calls=doc->prepared_calls.load(std::memory_order_relaxed);
 out->rendered_calls=doc->rendered_calls.load(std::memory_order_relaxed);
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_document_reset_render_timings(rinpdf_document* doc){
 if(!doc)return RINPDF_INVALID_ARGUMENT;
 doc->preparation_ns.store(0,std::memory_order_relaxed);
 doc->raster_ns.store(0,std::memory_order_relaxed);
 doc->prepared_calls.store(0,std::memory_order_relaxed);
 doc->rendered_calls.store(0,std::memory_order_relaxed);
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_document_cache_metrics(
 const rinpdf_document* doc,rinpdf_cache_metrics* metrics){
 if(!doc||!metrics)return RINPDF_INVALID_ARGUMENT;
 metrics->hits=doc->cache_hits.load(std::memory_order_relaxed);
 metrics->misses=doc->cache_misses.load(std::memory_order_relaxed);
 metrics->evictions=doc->cache_evictions.load(std::memory_order_relaxed);
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_document_reset_cache_metrics(rinpdf_document* doc){
 if(!doc)return RINPDF_INVALID_ARGUMENT;
 doc->cache_hits.store(0,std::memory_order_relaxed);
 doc->cache_misses.store(0,std::memory_order_relaxed);
 doc->cache_evictions.store(0,std::memory_order_relaxed);
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_document_version(const rinpdf_document *d,unsigned *major,unsigned *minor){
 if(!d||!major||!minor)return RINPDF_INVALID_ARGUMENT;
 *major=d->major;*minor=d->minor;return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_document_xref_statistics(
 const rinpdf_document* doc,size_t* ordinary,size_t* compressed){
 if(!doc||!ordinary||!compressed)return RINPDF_INVALID_ARGUMENT;
 try{
  std::call_once(doc->xref_once,[doc]{
   doc->xref_valid=rinpdf::parser::read_object_xref(doc->data(),doc->size(),
    doc->cached_xref,doc->cached_compressed,doc->cached_trailer,
    &doc->verified_page_counts);
   if(doc->xref_valid){
    for(const auto& item:doc->cached_xref)
     if(item.in_use)++doc->cached_ordinary_objects;
    for(const auto& item:doc->cached_compressed)
     if(item.type==2)++doc->cached_compressed_objects;
   }
  });
  if(!doc->xref_valid)return RINPDF_UNSUPPORTED;
  *ordinary=doc->cached_ordinary_objects;
  *compressed=doc->cached_compressed_objects;
  return RINPDF_OK;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
extern "C" rinpdf_status rinpdf_document_page_count(const rinpdf_document *d,size_t *count){
 if(!d||!count)return RINPDF_INVALID_ARGUMENT;
 std::call_once(d->page_count_once,[d]{
  size_t result=0;
  std::call_once(d->xref_once,[d]{
   d->xref_valid=rinpdf::parser::read_object_xref(d->data(),d->size(),
    d->cached_xref,d->cached_compressed,d->cached_trailer);
   if(d->xref_valid){
    for(const auto& item:d->cached_xref)
     if(item.in_use)++d->cached_ordinary_objects;
    for(const auto& item:d->cached_compressed)
     if(item.type==2)++d->cached_compressed_objects;
   }
  });
  d->page_count_valid=d->xref_valid &&
   rinpdf::parser::count_page_tree_indexed(d->data(),d->size(),
    d->cached_xref,d->cached_compressed,d->cached_trailer,result);
  if(d->page_count_valid)d->cached_page_count=result;
 });
 if(!d->page_count_valid)return RINPDF_UNSUPPORTED;
 *count=d->cached_page_count;return RINPDF_OK;
}

extern "C" rinpdf_status rinpdf_document_page_geometry(
 const rinpdf_document *doc,size_t page_index,rinpdf_page_geometry *geometry) {
 if(!doc||!geometry)return RINPDF_INVALID_ARGUMENT;
 try {
  {
   std::lock_guard<std::mutex> lock(doc->geometry_mutex);
   const auto it=doc->geometry_cache.find(page_index);
   if(it!=doc->geometry_cache.end()){*geometry=it->second;return RINPDF_OK;}
  }
  rinpdf::parser::MediaBox box{};
  if(!rinpdf::parser::read_page_box(doc->data(),doc->size(),page_index,box))
   return RINPDF_UNSUPPORTED;
  const rinpdf_page_geometry resolved{box.x0,box.y0,box.x1,box.y1,box.rotation};
  {
   std::lock_guard<std::mutex> lock(doc->geometry_mutex);
   if(doc->geometry_cache.size()<4096)
    doc->geometry_cache.emplace(page_index,resolved);
  }
  *geometry=resolved;
  return RINPDF_OK;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}

namespace {
size_t trim_locked(rinpdf_document& doc,size_t target);
}
extern "C" rinpdf_status rinpdf_render_page(const rinpdf_document *doc,size_t index,unsigned width,unsigned height,unsigned char *rgba,size_t stride){
 if(!doc||!rgba||width==0||height==0||width>16384||height>16384||stride<(size_t)width*4)return RINPDF_INVALID_ARGUMENT;
 if((size_t)width*height>16777216)return RINPDF_UNSUPPORTED;
 size_t pages=0;
 if(rinpdf_document_page_count(doc,&pages)!=RINPDF_OK)return RINPDF_UNSUPPORTED;
 if(index>=pages)return RINPDF_INVALID_ARGUMENT;
 try{
  const auto prep_start=std::chrono::steady_clock::now();
  // Bounded striped synchronization: no per-page mutex allocations or
  // unbounded lock table for documents with many distinct page indices.
  // Hash collisions serialize preparation only, not rasterization.
  std::unique_lock<std::mutex> page_guard(
   doc->page_prepare_locks[index%doc->page_prepare_locks.size()]);
  size_t ordinary_objects=0,compressed_objects=0;
  const auto index_status=rinpdf_document_xref_statistics(doc,
   &ordinary_objects,&compressed_objects);
  if(index_status!=RINPDF_OK)return index_status;
  rinpdf::parser::ScopedPageXref xref_scope(doc->data(),doc->size(),
    doc->cached_xref,doc->cached_compressed,doc->cached_trailer);
  std::shared_ptr<const std::vector<uint8_t>> contents;
  {
   std::lock_guard<std::mutex> guard(doc->content_mutex);
   auto it=doc->content_cache.find(index);
   if(it!=doc->content_cache.end()){
    contents=it->second;
    doc->cache_hits.fetch_add(1,std::memory_order_relaxed);
    doc->content_access[index]=doc->global_cache_clock.fetch_add(1,std::memory_order_relaxed)+1;
   }
  }
  if(!contents){
   doc->cache_misses.fetch_add(1,std::memory_order_relaxed);
   auto decoded=std::make_shared<std::vector<uint8_t>>();
   if(!rinpdf::parser::read_page_contents(doc->data(),doc->size(),index,*decoded))
    return RINPDF_UNSUPPORTED;
   contents=decoded;
   if(decoded->size()<=4*1024*1024){
    std::lock_guard<std::mutex> guard(doc->content_mutex);
    if(decoded->size()<=doc->content_cache_budget){
    auto existing=doc->content_cache.find(index);
    if(existing!=doc->content_cache.end()){
     contents=existing->second;
     doc->content_access[index]=doc->global_cache_clock.fetch_add(1,std::memory_order_relaxed)+1;
    }else{
     while((doc->content_cache.size()>=64 ||
           doc->content_cache_bytes>doc->content_cache_budget-decoded->size()) &&
           !doc->content_access.empty()){
      const auto oldest=std::min_element(doc->content_access.begin(),
       doc->content_access.end(),[](const auto& a,const auto& b){
        return a.second<b.second;
       });
      auto victim=doc->content_cache.find(oldest->first);
      if(victim!=doc->content_cache.end()){
       doc->content_cache_bytes-=victim->second->size();
       doc->content_cache.erase(victim);
      }
      doc->content_access.erase(oldest);
     }
     doc->content_cache.emplace(index,decoded);
     doc->content_access[index]=doc->global_cache_clock.fetch_add(1,std::memory_order_relaxed)+1;
     doc->content_cache_bytes+=decoded->size();
    }
    }
   }
  }
  rinpdf_page_geometry geometry{};
  const auto geometry_status=rinpdf_document_page_geometry(doc,index,&geometry);
  if(geometry_status!=RINPDF_OK)return geometry_status;
  rinpdf::parser::MediaBox box{geometry.x0,geometry.y0,geometry.x1,geometry.y1,geometry.rotation};
  std::shared_ptr<const rinpdf::renderer::FontResources> fonts;
  {
   std::lock_guard<std::mutex> lock(doc->font_mutex);
   fonts=doc->font_cache.find(index);
   if(fonts)doc->cache_hits.fetch_add(1,std::memory_order_relaxed);
   else doc->cache_misses.fetch_add(1,std::memory_order_relaxed);
  }
  if(!fonts){
   std::map<std::string,rinpdf::parser::PageFont> page_fonts;
   if(!rinpdf::parser::read_page_fonts(doc->data(),doc->size(),index,page_fonts))
    return RINPDF_UNSUPPORTED;
   auto decoded=std::make_shared<rinpdf::renderer::FontResources>();
   size_t font_bytes=0;
   for(const auto& [name,resource]:page_fonts){
    font_bytes+=resource.embedded_truetype.size();
    decoded->emplace(name,rinpdf::renderer::FontResource{
      resource.base_font,resource.subtype,resource.encoding,
      resource.embedded_truetype,resource.differences,resource.cid_map,
      resource.cid_widths,resource.vertical_metrics,resource.language});
   }
   fonts=decoded;
   if(font_bytes<=4*1024*1024){
    std::lock_guard<std::mutex> lock(doc->font_mutex);
    fonts=doc->font_cache.insert(index,decoded,32,sizeof(*decoded)+font_bytes);
   }
  }
  std::shared_ptr<const rinpdf::parser::PageProperties> properties;
  {
   std::lock_guard<std::mutex> lock(doc->property_mutex);
   properties=doc->property_cache.find(index);
   if(properties)doc->cache_hits.fetch_add(1,std::memory_order_relaxed);
   else doc->cache_misses.fetch_add(1,std::memory_order_relaxed);
  }
  if(!properties){
   auto decoded=std::make_shared<rinpdf::parser::PageProperties>();
   if(!rinpdf::parser::read_page_properties(doc->data(),doc->size(),index,*decoded))
    return RINPDF_UNSUPPORTED;
   properties=decoded;
   size_t bytes=0;
   for(const auto& [key,value]:*decoded)
    bytes+=key.size()+value.language.size()+value.actual_text.size();
   if(bytes<=256*1024){
    std::lock_guard<std::mutex> lock(doc->property_mutex);
    properties=doc->property_cache.insert(index,decoded,128,sizeof(*decoded)+bytes);
   }
  }
  std::shared_ptr<const rinpdf::renderer::ImageResources> images;
  {
   std::lock_guard<std::mutex> lock(doc->image_mutex);
   images=doc->image_cache.find(index);
   if(images)doc->cache_hits.fetch_add(1,std::memory_order_relaxed);
   else doc->cache_misses.fetch_add(1,std::memory_order_relaxed);
  }
  if(!images){
   auto decoded=std::make_shared<rinpdf::renderer::ImageResources>();
   if(!rinpdf::parser::read_page_images(doc->data(),doc->size(),index,*decoded))
    return RINPDF_UNSUPPORTED;
   size_t bytes=0;
   for(const auto& [name,image]:*decoded)
    bytes+=image.pixels.size()+image.alpha.size()+image.cmyk_pixels.size();
   images=decoded;
   if(bytes<=8*1024*1024){
    std::lock_guard<std::mutex> lock(doc->image_mutex);
    images=doc->image_cache.insert(index,decoded,16,sizeof(*decoded)+bytes);
   }
  }
  std::shared_ptr<const rinpdf::renderer::FormResources> forms;
  {
   std::lock_guard<std::mutex> lock(doc->form_mutex);
   forms=doc->form_cache.find(index);
   if(forms)doc->cache_hits.fetch_add(1,std::memory_order_relaxed);
   else doc->cache_misses.fetch_add(1,std::memory_order_relaxed);
  }
  if(!forms){
   auto decoded=std::make_shared<rinpdf::renderer::FormResources>();
   if(!rinpdf::parser::read_page_forms(doc->data(),doc->size(),index,*decoded))
    return RINPDF_UNSUPPORTED;
   forms=decoded;
   // Bound the number of cached form trees; large forms remain transient.
   size_t content_bytes=0;
   for(const auto& [name,form]:*decoded)content_bytes+=form.content.size();
   if(content_bytes<=4*1024*1024){
    std::lock_guard<std::mutex> lock(doc->form_mutex);
    forms=doc->form_cache.insert(index,decoded,16,sizeof(*decoded)+content_bytes);
   }
  }
  std::shared_ptr<const rinpdf::parser::PageGraphicsStates> graphic_states;
  {
   std::lock_guard<std::mutex> lock(doc->graphics_mutex);
   graphic_states=doc->graphics_cache.find(index);
   if(graphic_states)doc->cache_hits.fetch_add(1,std::memory_order_relaxed);
   else doc->cache_misses.fetch_add(1,std::memory_order_relaxed);
  }
  if(!graphic_states){
   auto decoded=std::make_shared<rinpdf::parser::PageGraphicsStates>();
   if(!rinpdf::parser::read_page_graphics_states(doc->data(),doc->size(),index,*decoded))
    return RINPDF_UNSUPPORTED;
   graphic_states=decoded;
   std::lock_guard<std::mutex> lock(doc->graphics_mutex);
   graphic_states=doc->graphics_cache.insert(index,decoded,128,decoded->size()*128+sizeof(*decoded));
  }
  // Parsed resource handles remain alive via shared_ptr after this unlock.
  // Only first-time parsing is serialized; pixel rendering can run in parallel.
  {
   std::scoped_lock lock(doc->content_mutex,doc->font_mutex,doc->image_mutex,
    doc->form_mutex,doc->graphics_mutex,doc->property_mutex,doc->geometry_mutex);
   trim_locked(*doc,doc->render_cache_budget);
  }
  page_guard.unlock();
  const auto raster_start=std::chrono::steady_clock::now();
  const auto prep_elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(
    raster_start-prep_start).count();
  doc->preparation_ns.fetch_add(uint64_t(prep_elapsed),std::memory_order_relaxed);
  doc->prepared_calls.fetch_add(1,std::memory_order_relaxed);
  const unsigned rotation=box.rotation;
  const unsigned source_width=(rotation==90||rotation==270)?height:width;
  const unsigned source_height=(rotation==90||rotation==270)?width:height;
  if(rotation==0){
   // Render directly into the caller's buffer to avoid an extra full-page
   // RGBA allocation and pixel copy.
   if(!rinpdf::renderer::render_rectangles_box_with_fonts(
      contents->data(),contents->size(),width,height,rgba,stride,
      {box.x0,box.y0,box.x1,box.y1},*fonts,*properties,*images,*forms,
      true,0,*graphic_states))return RINPDF_UNSUPPORTED;
   const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(
     std::chrono::steady_clock::now()-raster_start).count();
   doc->raster_ns.fetch_add(uint64_t(elapsed),std::memory_order_relaxed);
   doc->rendered_calls.fetch_add(1,std::memory_order_relaxed);
   return RINPDF_OK;
  }
  std::vector<uint8_t> temporary((size_t)width*height*4);
  if(!rinpdf::renderer::render_rectangles_box_with_fonts(contents->data(),contents->size(),source_width,source_height,temporary.data(),(size_t)source_width*4,{box.x0,box.y0,box.x1,box.y1},*fonts,*properties,*images,*forms,true,0,*graphic_states))return RINPDF_UNSUPPORTED;
  for(unsigned y=0;y<height;++y){
   for(unsigned x=0;x<width;++x){
    unsigned sx=x,sy=y;
    if(rotation==90){sx=y;sy=source_height-1-x;}
    else if(rotation==180){sx=source_width-1-x;sy=source_height-1-y;}
    else if(rotation==270){sx=source_width-1-y;sy=x;}
    std::memcpy(rgba+(size_t)y*stride+(size_t)x*4,temporary.data()+((size_t)sy*source_width+sx)*4,4);
   }
  }
  const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now()-raster_start).count();
  doc->raster_ns.fetch_add(uint64_t(elapsed),std::memory_order_relaxed);
  doc->rendered_calls.fetch_add(1,std::memory_order_relaxed);
  return RINPDF_OK;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}

namespace {
using PDFValue=rinpdf::parser::Value;
using PDFKind=PDFValue::Kind;
const PDFValue* pdf_field(const PDFValue& node,const char* key){
 if(node.kind!=PDFKind::Dictionary)return nullptr;
 auto it=node.dictionary.find(key);
 return it==node.dictionary.end()?nullptr:&it->second;
}
bool pdf_named(const PDFValue* v,const char* name){
 return v&&v->kind==PDFKind::Name&&v->text==name;
}
std::string pdf_name(const std::string& name){
 static const char hex[]="0123456789ABCDEF";
 std::string r="/";
 for(unsigned char c:name){
  if(c<=32||c>=127||c=='#'||c=='/'||c=='%'||c=='<'||c=='>'||
     c=='('||c==')'||c=='['||c==']'||c=='{'||c=='}'){
   r+='#';r+=hex[c>>4];r+=hex[c&15];
  }else r+=(char)c;
 }
 return r;
}
bool pdf_serialize(const PDFValue& v,std::string& result,unsigned depth=0){
 if(depth>64)return false;
 switch(v.kind){
 case PDFKind::Null:result+="null";break;
 case PDFKind::Boolean:result+=v.boolean?"true":"false";break;
 case PDFKind::Integer:result+=std::to_string(v.integer);break;
 case PDFKind::Real:{
  if(!std::isfinite(v.real))return false;
  char buf[64];std::snprintf(buf,sizeof(buf),"%.12g",v.real);
  result+=buf;break;
 }
 case PDFKind::Name:result+=pdf_name(v.text);break;
 case PDFKind::String:{
  static const char hex[]="0123456789ABCDEF";
  result+='<';
  for(unsigned char c:v.text){result+=hex[c>>4];result+=hex[c&15];}
  result+='>';break;
 }
 case PDFKind::Reference:
  if(v.integer<=0)return false;
  result+=std::to_string(v.integer)+" "+std::to_string(v.generation)+" R";break;
 case PDFKind::Array:
  result+="[ ";
  for(const auto& item:v.array){
   if(!pdf_serialize(item,result,depth+1))return false;
   result+=' ';
  }
  result+=']';break;
 case PDFKind::Dictionary:
  result+="<< ";
  for(const auto& [key,item]:v.dictionary){
   result+=pdf_name(key);result+=' ';
   if(!pdf_serialize(item,result,depth+1))return false;
   result+=' ';
  }
  result+=">>";break;
 default:return false;
 }
 return true;
}
struct ExportWalker{
 const uint8_t* data;
 size_t size;
 const std::vector<rinpdf::parser::XrefEntry>& entries;
 const std::vector<rinpdf::parser::XrefStreamEntry>& compressed;
 std::set<size_t> active;
 std::vector<PDFValue> pages;
 size_t visited=0;
 bool load(const PDFValue& ref,PDFValue& value){
  return rinpdf::parser::resolve_reference_value(data,size,entries,compressed,ref,value);
 }
 bool walk(const PDFValue& ref,const std::map<std::string,PDFValue>& inherited,unsigned depth){
  if(depth>64||++visited>100000||ref.kind!=PDFKind::Reference||ref.integer<=0)return false;
  if(!active.insert((size_t)ref.integer).second)return false;
  PDFValue node;
  if(!load(ref,node)||node.kind!=PDFKind::Dictionary)return false;
  auto next=inherited;
  for(const char* key:{"Resources","MediaBox","CropBox","Rotate"}){
   if(const auto* value=pdf_field(node,key))next[key]=*value;
  }
  if(pdf_named(pdf_field(node,"Type"),"Page")){
   for(const auto& [key,value]:next)
    if(!pdf_field(node,key))node.dictionary[key]=value;
   pages.push_back(std::move(node));
  }else if(pdf_named(pdf_field(node,"Type"),"Pages")){
   const auto* kids=pdf_field(node,"Kids");
   if(!kids||kids->kind!=PDFKind::Array||kids->array.size()>100000)return false;
   for(const auto& child:kids->array)if(!walk(child,next,depth+1))return false;
  }else return false;
  active.erase((size_t)ref.integer);
  return true;
 }
};
}
extern "C" rinpdf_status rinpdf_export_pages_with_boxes_memory(
 const rinpdf_document* doc,const size_t* indices,const rinpdf_page_box_override* overrides,
 size_t count,uint8_t** output,size_t* output_size){
 if(!output||!output_size)return RINPDF_INVALID_ARGUMENT;
 *output=nullptr;*output_size=0;
 if(!doc||!indices||count==0||count>10000)return RINPDF_INVALID_ARGUMENT;
 try {
  const std::span<const uint8_t> bytes(doc->data(),doc->size());
  std::vector<rinpdf::parser::XrefEntry> entries;
  std::vector<rinpdf::parser::XrefStreamEntry> compressed;
  PDFValue trailer;
  size_t ordinary_count=0,compressed_count=0;
  if(rinpdf_document_xref_statistics(doc,&ordinary_count,&compressed_count)!=RINPDF_OK)
   return RINPDF_UNSUPPORTED;
  entries=doc->cached_xref;
  compressed=doc->cached_compressed;
  trailer=doc->cached_trailer;
  if(!compressed.empty()||pdf_field(trailer,"Encrypt")||
     pdf_field(trailer,"Prev")||pdf_field(trailer,"XRefStm"))
   return RINPDF_UNSUPPORTED;
  const PDFValue* root_ref=pdf_field(trailer,"Root");
  if(!root_ref||root_ref->kind!=PDFKind::Reference)return RINPDF_UNSUPPORTED;
  ExportWalker walker{bytes.data(),bytes.size(),entries,compressed};
  PDFValue root;
  if(!walker.load(*root_ref,root)||!pdf_named(pdf_field(root,"Type"),"Catalog"))
   return RINPDF_UNSUPPORTED;
  const PDFValue* page_root=pdf_field(root,"Pages");
  if(!page_root||!walker.walk(*page_root,{},0))return RINPDF_UNSUPPORTED;
  for(size_t i=0;i<count;++i)if(indices[i]>=walker.pages.size())return RINPDF_INVALID_ARGUMENT;
  size_t max_id=0;
  for(const auto& entry:entries)max_id=std::max(max_id,entry.object);
  if(max_id>999997||count>1000000-max_id-2)return RINPDF_UNSUPPORTED;
  const size_t pages_id=max_id+1;
  const size_t catalog_id=max_id+2;
  const size_t first_page_id=max_id+3;
  if(count>1000000-first_page_id)return RINPDF_UNSUPPORTED;
  const size_t last_id=first_page_id+count-1;
  std::vector<size_t> offsets(last_id+1,0);
  std::vector<unsigned> generations(last_id+1,0);
  for(const auto& entry:entries){
   if(entry.object>max_id)continue;
   if(entry.in_use){
    if(entry.offset>=bytes.size())return RINPDF_UNSUPPORTED;
    offsets[entry.object]=entry.offset;
    generations[entry.object]=entry.generation;
   }
  }
  std::string result(reinterpret_cast<const char*>(bytes.data()),bytes.size());
  result+="\n";
  auto append=[&](size_t id,const std::string& payload){
   offsets[id]=result.size();
   result+=std::to_string(id)+" 0 obj\n"+payload+"\nendobj\n";
  };
  std::string kids="[ ";
  for(size_t i=0;i<count;++i)kids+=std::to_string(first_page_id+i)+" 0 R ";
  kids+="]";
  append(pages_id,"<< /Type /Pages /Count "+std::to_string(count)+" /Kids "+kids+" >>");
  root.dictionary["Pages"].kind=PDFKind::Reference;
  root.dictionary["Pages"].integer=(long long)pages_id;
  root.dictionary["Pages"].generation=0;
  std::string serialized;
  if(!pdf_serialize(root,serialized))return RINPDF_UNSUPPORTED;
  append(catalog_id,serialized);
  for(size_t i=0;i<count;++i){
   PDFValue page=walker.pages[indices[i]];
   PDFValue parent;
   parent.kind=PDFKind::Reference;
   parent.integer=(long long)pages_id;
   page.dictionary["Parent"]=parent;
   if(overrides){
    const auto& edit=overrides[i];
    if(edit.set_rotation){
     if(edit.rotation!=0&&edit.rotation!=90&&edit.rotation!=180&&edit.rotation!=270)
      return RINPDF_INVALID_ARGUMENT;
     PDFValue r; r.kind=PDFKind::Integer;r.integer=edit.rotation;
     page.dictionary["Rotate"]=r;
    }
    auto set_box=[&](const char* name,const double* values)->bool{
     for(unsigned k=0;k<4;++k)if(!std::isfinite(values[k]))return false;
     if(values[2]<=values[0]||values[3]<=values[1])return false;
     PDFValue v;v.kind=PDFKind::Array;
     for(unsigned k=0;k<4;++k){
      PDFValue n;n.kind=PDFKind::Real;n.real=values[k];
      v.array.push_back(n);
     }
     page.dictionary[name]=std::move(v);return true;
    };
    if(edit.set_media_box&&!set_box("MediaBox",edit.media_box))return RINPDF_INVALID_ARGUMENT;
    if(edit.set_crop_box&&!set_box("CropBox",edit.crop_box))return RINPDF_INVALID_ARGUMENT;
    if(edit.set_trim_box&&!set_box("TrimBox",edit.trim_box))return RINPDF_INVALID_ARGUMENT;
    if(edit.set_bleed_box&&!set_box("BleedBox",edit.bleed_box))return RINPDF_INVALID_ARGUMENT;
    if(edit.set_art_box&&!set_box("ArtBox",edit.art_box))return RINPDF_INVALID_ARGUMENT;
   }
   serialized.clear();
   if(!pdf_serialize(page,serialized))return RINPDF_UNSUPPORTED;
   append(first_page_id+i,serialized);
  }
  const size_t xref=result.size();
  result+="xref\n0 "+std::to_string(last_id+1)+"\n";
  result+="0000000000 65535 f \n";
  char line[40];
  for(size_t i=1;i<=last_id;++i){
   if(offsets[i]>9999999999ULL)return RINPDF_UNSUPPORTED;
   std::snprintf(line,sizeof(line),"%010zu %05u %c \n",offsets[i],
                 offsets[i]?generations[i]:0,offsets[i]?'n':'f');
   result+=line;
  }
  result+="trailer\n<< /Size "+std::to_string(last_id+1)+
          " /Root "+std::to_string(catalog_id)+" 0 R >>\nstartxref\n"+
          std::to_string(xref)+"\n%%EOF\n";
  uint8_t* memory=(uint8_t*)std::malloc(result.size());
  if(!memory)return RINPDF_OUT_OF_MEMORY;
  std::memcpy(memory,result.data(),result.size());
  *output=memory;*output_size=result.size();
  return RINPDF_OK;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
extern "C" rinpdf_status rinpdf_export_pages_with_boxes_file(
 const char* path,const rinpdf_document* doc,const size_t* indices,
 const rinpdf_page_box_override* overrides,size_t count){
 if(!path)return RINPDF_INVALID_ARGUMENT;
 uint8_t* buffer=nullptr;size_t size=0;
 const auto status=rinpdf_export_pages_with_boxes_memory(
    doc,indices,overrides,count,&buffer,&size);
 if(status!=RINPDF_OK)return status;
 FILE* file=std::fopen(path,"wb");
 if(!file){rinpdf_free_buffer(buffer);return RINPDF_IO_ERROR;}
 bool ok=std::fwrite(buffer,1,size,file)==size;
 if(std::fclose(file)!=0)ok=false;
 rinpdf_free_buffer(buffer);
 return ok?RINPDF_OK:RINPDF_IO_ERROR;
}
extern "C" rinpdf_status rinpdf_export_pages_lossless_memory(
 const rinpdf_document* doc,const size_t* indices,size_t count,uint8_t** output,size_t* output_size){
 return rinpdf_export_pages_with_boxes_memory(doc,indices,nullptr,count,output,output_size);
}
extern "C" rinpdf_status rinpdf_export_pages_lossless_file(
 const char* path,const rinpdf_document* doc,const size_t* indices,size_t count){
 if(!path)return RINPDF_INVALID_ARGUMENT;
 uint8_t* data=nullptr;size_t size=0;
 const auto status=rinpdf_export_pages_lossless_memory(doc,indices,count,&data,&size);
 if(status!=RINPDF_OK)return status;
 FILE* file=std::fopen(path,"wb");
 if(!file){std::free(data);return RINPDF_IO_ERROR;}
 bool ok=std::fwrite(data,1,size,file)==size;
 if(std::fclose(file)!=0)ok=false;
 std::free(data);
 return ok?RINPDF_OK:RINPDF_IO_ERROR;
}

namespace rinpdf {
rinpdf_status merge_original_pdf_bytes(const uint8_t* const* sources,const size_t* lengths,
 size_t source_count,uint8_t** out,size_t* out_size);
}
extern "C" rinpdf_status rinpdf_merge_documents_lossless_memory(
 const rinpdf_document* const* documents,size_t count,uint8_t** data,size_t* size){
 if(!data||!size)return RINPDF_INVALID_ARGUMENT;
 *data=nullptr;*size=0;
 if(!documents||count==0||count>128)return RINPDF_INVALID_ARGUMENT;
 try {
  std::vector<const uint8_t*> buffers;
  std::vector<size_t> lengths;
  buffers.reserve(count);lengths.reserve(count);
  for(size_t i=0;i<count;++i){
   if(!documents[i])return RINPDF_INVALID_ARGUMENT;
   buffers.push_back(documents[i]->data());
   lengths.push_back(documents[i]->size());
  }
  return rinpdf::merge_original_pdf_bytes(buffers.data(),lengths.data(),count,data,size);
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
extern "C" rinpdf_status rinpdf_merge_documents_lossless_file(
 const char* path,const rinpdf_document* const* documents,size_t count){
 if(!path)return RINPDF_INVALID_ARGUMENT;
 uint8_t* data=nullptr;size_t size=0;
 const auto status=rinpdf_merge_documents_lossless_memory(documents,count,&data,&size);
 if(status!=RINPDF_OK)return status;
 FILE* file=std::fopen(path,"wb");
 if(!file){std::free(data);return RINPDF_IO_ERROR;}
 bool ok=std::fwrite(data,1,size,file)==size;
 if(std::fclose(file)!=0)ok=false;
 std::free(data);
 return ok?RINPDF_OK:RINPDF_IO_ERROR;
}

extern "C" rinpdf_status rinpdf_merge_files_lossless(
 const char* path,const char* const* source_paths,size_t count){
 if(!path||!source_paths||count==0||count>128)return RINPDF_INVALID_ARGUMENT;
 try {
  using OwnedDocument=std::unique_ptr<rinpdf_document,decltype(&rinpdf_document_close)>;
  std::vector<OwnedDocument> documents;
  documents.reserve(count);
  rinpdf_status status=RINPDF_OK;
  for(size_t i=0;i<count;++i){
   if(!source_paths[i]){status=RINPDF_INVALID_ARGUMENT;break;}
   rinpdf_document* doc=nullptr;
   status=rinpdf_document_open_file(source_paths[i],&doc);
   if(status!=RINPDF_OK)break;
   documents.emplace_back(doc,rinpdf_document_close);
  }
  if(status==RINPDF_OK){
   std::vector<const rinpdf_document*> refs;
   refs.reserve(documents.size());
   for(const auto& doc:documents)refs.push_back(doc.get());
   status=rinpdf_merge_documents_lossless_file(path,refs.data(),refs.size());
  }
  return status;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}

extern "C" rinpdf_status rinpdf_compose_pages_direct_memory(
 const rinpdf_page_source*,const rinpdf_page_box_override*,size_t,uint8_t**,size_t*);
extern "C" rinpdf_status rinpdf_merge_selected_pages_lossless_memory(
 const rinpdf_page_source* sources,size_t count,uint8_t** data,size_t* size){
 return rinpdf_compose_pages_direct_memory(sources,nullptr,count,data,size);
}
extern "C" rinpdf_status rinpdf_merge_selected_pages_lossless_file(
 const char* path,const rinpdf_page_source* pages,size_t count){
 if(!path)return RINPDF_INVALID_ARGUMENT;
 uint8_t* buffer=nullptr;size_t length=0;
 const rinpdf_status status=rinpdf_merge_selected_pages_lossless_memory(
    pages,count,&buffer,&length);
 if(status!=RINPDF_OK)return status;
 FILE* file=std::fopen(path,"wb");
 if(!file){rinpdf_free_buffer(buffer);return RINPDF_IO_ERROR;}
 bool ok=std::fwrite(buffer,1,length,file)==length;
 if(std::fclose(file)!=0)ok=false;
 rinpdf_free_buffer(buffer);
 return ok?RINPDF_OK:RINPDF_IO_ERROR;
}

namespace rinpdf {
rinpdf_status compose_selected_pdf_bytes(
 const uint8_t* const*,const size_t*,size_t,
 const size_t*,const size_t*,const rinpdf_page_box_override*,size_t,
 uint8_t**,size_t*);
}
extern "C" rinpdf_status rinpdf_compose_pages_direct_memory(
 const rinpdf_page_source* pages,const rinpdf_page_box_override* geometry,
 size_t count,uint8_t** output,size_t* output_size){
 if(!output||!output_size)return RINPDF_INVALID_ARGUMENT;
 *output=nullptr;*output_size=0;
 if(!pages||count==0||count>10000)return RINPDF_INVALID_ARGUMENT;
 try {
  std::vector<const rinpdf_document*> documents;
  std::vector<const uint8_t*> buffers;
  std::vector<size_t> lengths,document_ids,page_ids;
  documents.reserve(128);buffers.reserve(128);lengths.reserve(128);
  document_ids.reserve(count);page_ids.reserve(count);
  for(size_t i=0;i<count;++i){
   if(!pages[i].document)return RINPDF_INVALID_ARGUMENT;
   auto it=std::find(documents.begin(),documents.end(),pages[i].document);
   size_t id=(size_t)(it-documents.begin());
   if(it==documents.end()){
    if(documents.size()>=128)return RINPDF_UNSUPPORTED;
    documents.push_back(pages[i].document);
    buffers.push_back(pages[i].document->data());lengths.push_back(pages[i].document->size());
   }
   document_ids.push_back(id);page_ids.push_back(pages[i].page_index);
  }
  return rinpdf::compose_selected_pdf_bytes(buffers.data(),lengths.data(),
      buffers.size(),document_ids.data(),page_ids.data(),geometry,count,
      output,output_size);
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}

namespace rinpdf {
rinpdf_status compose_selected_pdf_stream(
 const uint8_t* const*,const size_t*,size_t,const size_t*,const size_t*,
 const rinpdf_page_box_override*,size_t,FILE*,uint8_t**,size_t*);
}
extern "C" rinpdf_status rinpdf_compose_pages_direct_file(
 const char* path,const rinpdf_page_source* pages,
 const rinpdf_page_box_override* geometry,size_t count){
 if(!path||!pages||!count||count>10000)return RINPDF_INVALID_ARGUMENT;
 try {
  std::vector<const rinpdf_document*> documents;
  std::vector<const uint8_t*> buffers;
  std::vector<size_t> lengths,document_ids,page_ids;
  documents.reserve(128);buffers.reserve(128);lengths.reserve(128);
  document_ids.reserve(count);page_ids.reserve(count);
  for(size_t i=0;i<count;++i){
   if(!pages[i].document)return RINPDF_INVALID_ARGUMENT;
   auto it=std::find(documents.begin(),documents.end(),pages[i].document);
   size_t id=(size_t)(it-documents.begin());
   if(it==documents.end()){
    if(documents.size()>=128)return RINPDF_UNSUPPORTED;
    const std::span<const uint8_t> bytes(pages[i].document->data(),pages[i].document->size());
    documents.push_back(pages[i].document);
    buffers.push_back(bytes.data());lengths.push_back(bytes.size());
   }
   document_ids.push_back(id);page_ids.push_back(pages[i].page_index);
  }
#ifdef _WIN32
  // Public paths use UTF-8; Windows APIs require UTF-16 for Japanese filenames.
  auto wide_path=[](const std::string& utf8,std::wstring& wide)->bool{
   if(utf8.empty()||utf8.size()>INT_MAX)return false;
   int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),
                                  (int)utf8.size(),nullptr,0);
   if(length<=0)return false;
   wide.resize(length);
   return MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),
                              (int)utf8.size(),wide.data(),length)==length;
  };
  std::wstring destination;
  if(!wide_path(path,destination))return RINPDF_INVALID_ARGUMENT;
  std::wstring temporary=destination+L".rinpdf-";
  std::wstring temp_name;
  FILE* file=nullptr;
  for(unsigned attempt=0;attempt<32;++attempt){
   wchar_t suffix[32];std::swprintf(suffix,32,L"%08lx-%u",
      (unsigned long)GetCurrentProcessId(),attempt);
   temp_name=temporary+suffix;
   HANDLE handle=CreateFileW(temp_name.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL,nullptr);
   if(handle==INVALID_HANDLE_VALUE){
    if(GetLastError()==ERROR_FILE_EXISTS||GetLastError()==ERROR_ALREADY_EXISTS)continue;
    return RINPDF_IO_ERROR;
   }
   int fd=_open_osfhandle((intptr_t)handle,_O_BINARY|_O_WRONLY);
   if(fd<0){CloseHandle(handle);DeleteFileW(temp_name.c_str());return RINPDF_IO_ERROR;}
   file=_fdopen(fd,"wb");
   if(!file){_close(fd);DeleteFileW(temp_name.c_str());return RINPDF_IO_ERROR;}
   break;
  }
  if(!file)return RINPDF_IO_ERROR;
#else
  std::string temporary=std::string(path)+".rinpdf-XXXXXX";
  std::vector<char> name(temporary.begin(),temporary.end());
  name.push_back('\0');
  int fd=mkstemp(name.data());
  if(fd<0)return RINPDF_IO_ERROR;
  FILE* file=fdopen(fd,"wb");
  if(!file){close(fd);std::remove(name.data());return RINPDF_IO_ERROR;}
#endif
  uint8_t* unused=nullptr;size_t unused_size=0;
  auto result=rinpdf::compose_selected_pdf_stream(
   buffers.data(),lengths.data(),buffers.size(),
   document_ids.data(),page_ids.data(),geometry,count,file,&unused,&unused_size);
  if(result==RINPDF_OK){
   if(std::fflush(file)!=0)result=RINPDF_IO_ERROR;
#ifdef _WIN32
   if(result==RINPDF_OK && _commit(_fileno(file))!=0)result=RINPDF_IO_ERROR;
#else
   if(result==RINPDF_OK && fsync(fileno(file))!=0)result=RINPDF_IO_ERROR;
#endif
  }
  if(std::fclose(file)!=0)result=RINPDF_IO_ERROR;
#ifdef _WIN32
  if(result==RINPDF_OK &&
     !MoveFileExW(temp_name.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
    result=RINPDF_IO_ERROR;
  if(result!=RINPDF_OK)DeleteFileW(temp_name.c_str());
#else
  if(result==RINPDF_OK && std::rename(name.data(),path)!=0)result=RINPDF_IO_ERROR;
  if(result!=RINPDF_OK)std::remove(name.data());
#endif
  return result;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
