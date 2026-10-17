#include "rinpdf/document.h"
#include "../parser/pages.h"
#include "../parser/contents.h"
#include "../renderer/rectangles.h"
#include "rinpdf/render.h"
#include <cstdio>
#include <cstring>
#include <new>
#include <map>
#include <vector>
struct rinpdf_document { std::vector<uint8_t> bytes; unsigned major,minor; };
extern "C" rinpdf_status rinpdf_document_open_memory(const uint8_t *data,size_t size,rinpdf_document **out) {
 if(!out) return RINPDF_INVALID_ARGUMENT;
 *out=nullptr;
 if(!data || size<8) return RINPDF_INVALID_PDF;
 if(std::memcmp(data,"%PDF-",5)!=0 || data[5]<'0'||data[5]>'9'||data[6]!='.'||data[7]<'0'||data[7]>'9') return RINPDF_INVALID_PDF;
 try { auto *d=new rinpdf_document; d->major=data[5]-'0'; d->minor=data[7]-'0'; d->bytes.assign(data,data+size); *out=d; return RINPDF_OK; }
 catch(const std::bad_alloc&) {return RINPDF_OUT_OF_MEMORY;}
}
extern "C" rinpdf_status rinpdf_document_open_file(const char *path,rinpdf_document **out) {
 if(!path||!out) return RINPDF_INVALID_ARGUMENT;
 *out=nullptr;
 FILE *f=std::fopen(path,"rb"); if(!f) return RINPDF_IO_ERROR;
 std::vector<uint8_t> bytes; unsigned char buffer[8192]; size_t n;
 try {
  while((n=std::fread(buffer,1,sizeof(buffer),f))>0) bytes.insert(bytes.end(),buffer,buffer+n);
 } catch(const std::bad_alloc&) {std::fclose(f);return RINPDF_OUT_OF_MEMORY;}
 bool bad=std::ferror(f);std::fclose(f);if(bad)return RINPDF_IO_ERROR;
 return rinpdf_document_open_memory(bytes.data(),bytes.size(),out);
}
extern "C" void rinpdf_document_close(rinpdf_document *d){delete d;}
extern "C" rinpdf_status rinpdf_document_version(const rinpdf_document *d,unsigned *major,unsigned *minor){
 if(!d||!major||!minor)return RINPDF_INVALID_ARGUMENT;
 *major=d->major;*minor=d->minor;return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_document_page_count(const rinpdf_document *d,size_t *count){
 if(!d||!count)return RINPDF_INVALID_ARGUMENT;
 size_t result=0;
 if(!rinpdf::parser::count_page_tree(d->bytes.data(),d->bytes.size(),result))return RINPDF_UNSUPPORTED;
 *count=result;return RINPDF_OK;
}

extern "C" rinpdf_status rinpdf_render_page(const rinpdf_document *doc,size_t index,unsigned width,unsigned height,unsigned char *rgba,size_t stride){
 if(!doc||!rgba||width==0||height==0||width>16384||height>16384||stride<(size_t)width*4)return RINPDF_INVALID_ARGUMENT;
 if((size_t)width*height>16777216)return RINPDF_UNSUPPORTED;
 size_t pages=0;
 if(!rinpdf::parser::count_page_tree(doc->bytes.data(),doc->bytes.size(),pages))return RINPDF_UNSUPPORTED;
 if(index>=pages)return RINPDF_INVALID_ARGUMENT;
 try{
  std::vector<uint8_t> contents;
  if(!rinpdf::parser::read_page_contents(doc->bytes.data(),doc->bytes.size(),index,contents))return RINPDF_UNSUPPORTED;
  rinpdf::parser::MediaBox box{};
  if(!rinpdf::parser::read_page_box(doc->bytes.data(),doc->bytes.size(),index,box))return RINPDF_UNSUPPORTED;
  std::map<std::string,rinpdf::parser::PageFont> page_fonts;
  if(!rinpdf::parser::read_page_fonts(doc->bytes.data(),doc->bytes.size(),index,page_fonts))
   return RINPDF_UNSUPPORTED;
  rinpdf::renderer::FontResources fonts;
  for(const auto& [name,resource]:page_fonts)
   fonts.emplace(name,rinpdf::renderer::FontResource{resource.base_font,resource.subtype,resource.encoding,resource.embedded_truetype});
  const unsigned rotation=box.rotation;
  const unsigned source_width=(rotation==90||rotation==270)?height:width;
  const unsigned source_height=(rotation==90||rotation==270)?width:height;
  std::vector<uint8_t> temporary((size_t)width*height*4);
  if(!rinpdf::renderer::render_rectangles_box_with_fonts(contents.data(),contents.size(),source_width,source_height,temporary.data(),(size_t)source_width*4,{box.x0,box.y0,box.x1,box.y1},fonts))return RINPDF_UNSUPPORTED;
  for(unsigned y=0;y<height;++y){
   for(unsigned x=0;x<width;++x){
    unsigned sx=x,sy=y;
    if(rotation==90){sx=y;sy=source_height-1-x;}
    else if(rotation==180){sx=source_width-1-x;sy=source_height-1-y;}
    else if(rotation==270){sx=source_width-1-y;sy=x;}
    std::memcpy(rgba+(size_t)y*stride+(size_t)x*4,temporary.data()+((size_t)sy*source_width+sx)*4,4);
   }
  }
  return RINPDF_OK;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
