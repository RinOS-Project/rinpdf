#include "rinpdf/document.h"
#include <cstdio>
#include <cstring>
#include <new>
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
 if(!d||!count)return RINPDF_INVALID_ARGUMENT;return RINPDF_UNSUPPORTED;
}
