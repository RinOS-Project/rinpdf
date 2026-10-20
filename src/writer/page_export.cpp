#include "rinpdf/rinpdf.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

namespace {
rinpdf_status export_pages(const rinpdf_page_source* sources,size_t count,
 unsigned dpi,uint8_t** result,size_t* result_size) {
 if(!result||!result_size)return RINPDF_INVALID_ARGUMENT;
 *result=nullptr;*result_size=0;
 if(!sources||count==0||count>1000||dpi<36||dpi>600)
  return RINPDF_INVALID_ARGUMENT;
 rinpdf_status status=RINPDF_OK;
 try {
  std::vector<std::vector<uint8_t>> buffers;
  std::vector<rinpdf_image_page> pages;
  buffers.reserve(count);pages.reserve(count);
  size_t accumulated=0;
  for(size_t i=0;i<count;++i) {
   const rinpdf_document* doc=sources[i].document;
   if(!doc)return RINPDF_INVALID_ARGUMENT;
   size_t total=0;
   status=rinpdf_document_page_count(doc,&total);
   if(status!=RINPDF_OK)return status;
   const size_t index=sources[i].page_index;
   if(index>=total)return RINPDF_INVALID_ARGUMENT;
   rinpdf_page_geometry box{};
   status=rinpdf_document_page_geometry(doc,index,&box);
   if(status!=RINPDF_OK)return status;
   double w=std::abs(box.x1-box.x0),h=std::abs(box.y1-box.y0);
   if(box.rotation==90||box.rotation==270)std::swap(w,h);
   if(!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0)return RINPDF_UNSUPPORTED;
   const double scaled_w=std::ceil(w*dpi/72.0);
   const double scaled_h=std::ceil(h*dpi/72.0);
   if(scaled_w>16384||scaled_h>16384||scaled_w<1||scaled_h<1)
    return RINPDF_UNSUPPORTED;
   const unsigned width=static_cast<unsigned>(scaled_w);
   const unsigned height=static_cast<unsigned>(scaled_h);
   const size_t area=(size_t)width*height;
   if(area>16777216||area>64000000-accumulated)return RINPDF_UNSUPPORTED;
   accumulated+=area;
   buffers.emplace_back(area*4);
   status=rinpdf_render_page(doc,index,width,height,buffers.back().data(),width*4);
   if(status!=RINPDF_OK)return status;
   pages.push_back({buffers.back().data(),width,height,(size_t)width*4,4});
  }
  return rinpdf_write_image_pages_memory(pages.data(),pages.size(),result,result_size);
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
}
extern "C" rinpdf_status rinpdf_export_pages_memory(const rinpdf_document* doc,
 const size_t* indices,size_t count,unsigned dpi,uint8_t** data,size_t* size) {
 if(!doc||!indices||count>1000)return RINPDF_INVALID_ARGUMENT;
 try {
  std::vector<rinpdf_page_source> sources;
  sources.reserve(count);
  for(size_t i=0;i<count;++i)sources.push_back({doc,indices[i]});
  return export_pages(sources.data(),count,dpi,data,size);
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
extern "C" rinpdf_status rinpdf_export_pages_file(const char* path,
 const rinpdf_document* doc,const size_t* indices,size_t count,unsigned dpi) {
 if(!path)return RINPDF_INVALID_ARGUMENT;
 uint8_t* data=nullptr;size_t size=0;
 rinpdf_status status=export_pages(doc,indices,count,dpi,&data,&size);
 if(status!=RINPDF_OK)return status;
 FILE* f=std::fopen(path,"wb");
 if(!f){rinpdf_free_buffer(data);return RINPDF_IO_ERROR;}
 bool ok=std::fwrite(data,1,size,f)==size;
 if(std::fclose(f)!=0)ok=false;
 rinpdf_free_buffer(data);
 return ok?RINPDF_OK:RINPDF_IO_ERROR;
}

extern "C" rinpdf_status rinpdf_merge_pages_memory(
 const rinpdf_page_source* pages,size_t count,unsigned dpi,uint8_t** data,size_t* size){
 return export_pages(pages,count,dpi,data,size);
}
extern "C" rinpdf_status rinpdf_merge_pages_file(
 const char* path,const rinpdf_page_source* pages,size_t count,unsigned dpi){
 if(!path)return RINPDF_INVALID_ARGUMENT;
 uint8_t* data=nullptr;size_t size=0;
 rinpdf_status status=export_pages(pages,count,dpi,&data,&size);
 if(status!=RINPDF_OK)return status;
 FILE* f=std::fopen(path,"wb");
 if(!f){rinpdf_free_buffer(data);return RINPDF_IO_ERROR;}
 bool ok=std::fwrite(data,1,size,f)==size;
 if(std::fclose(f)!=0)ok=false;
 rinpdf_free_buffer(data);
 return ok?RINPDF_OK:RINPDF_IO_ERROR;
}
