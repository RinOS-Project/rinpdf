#include "rinpdf/rinpdf.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>
#include <zlib.h>

namespace {
void object(std::string& pdf,std::vector<size_t>& positions,const std::string& payload) {
 positions.push_back(pdf.size());
 pdf+=std::to_string(positions.size()-1)+" 0 obj\n";
 pdf+=payload;
 pdf+="\nendobj\n";
}
bool encode(const uint8_t* rgb,unsigned width,unsigned height,size_t stride,
            unsigned components,std::string& compressed) {
 z_stream z{};
 if(deflateInit(&z,Z_DEFAULT_COMPRESSION)!=Z_OK)return false;
 unsigned char out[16384];
 bool good=true;
 for(unsigned y=0;y<height && good;++y){
  z.next_in=const_cast<Bytef*>(reinterpret_cast<const Bytef*>(rgb+(size_t)y*stride));
  z.avail_in=width*components;
  while(z.avail_in && good){
   z.next_out=out;z.avail_out=sizeof(out);
   int rc=deflate(&z,Z_NO_FLUSH);
   if(rc!=Z_OK){good=false;break;}
   compressed.append(reinterpret_cast<char*>(out),sizeof(out)-z.avail_out);
  }
 }
 if(good){
  int rc;
  do {
   z.next_out=out;z.avail_out=sizeof(out);
   rc=deflate(&z,Z_FINISH);
   if(rc!=Z_OK && rc!=Z_STREAM_END){good=false;break;}
   compressed.append(reinterpret_cast<char*>(out),sizeof(out)-z.avail_out);
  }while(rc!=Z_STREAM_END);
 }
 deflateEnd(&z);
 return good;
}
rinpdf_status create_image_pdf(const uint8_t* pixels,unsigned width,unsigned height,
 size_t stride,unsigned components,uint8_t** data,size_t* size){
 if(!data||!size)return RINPDF_INVALID_ARGUMENT;
 *data=nullptr;*size=0;
 if(!pixels||width==0||height==0||width>16384||height>16384||
    (components!=1 && components!=3)||width>SIZE_MAX/components||
    stride<(size_t)width*components||(size_t)width*height>64000000)
  return RINPDF_INVALID_ARGUMENT;
 try {
  std::string compressed;
  if(!encode(pixels,width,height,stride,components,compressed))return RINPDF_UNSUPPORTED;
  const unsigned pw=std::max(100u,width),ph=std::max(100u,height);
  const double scale=std::min({1.0,595.0/pw,842.0/ph});
  const unsigned page_w=std::max(1u,(unsigned)(pw*scale));
  const unsigned page_h=std::max(1u,(unsigned)(ph*scale));
  std::string pdf="%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
  std::vector<size_t> positions(1,0);
  object(pdf,positions,"<< /Type /Catalog /Pages 2 0 R >>");
  object(pdf,positions,"<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
  object(pdf,positions,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "+
      std::to_string(page_w)+" "+std::to_string(page_h)+
      "] /Resources << /XObject << /Im0 5 0 R >> >> /Contents 4 0 R >>");
  const std::string commands="q\n"+std::to_string(page_w)+" 0 0 "+
      std::to_string(page_h)+" 0 0 cm\n/Im0 Do\nQ\n";
  object(pdf,positions,"<< /Length "+std::to_string(commands.size())+
      " >>\nstream\n"+commands+"endstream");
  object(pdf,positions,"<< /Type /XObject /Subtype /Image /Width "+
      std::to_string(width)+" /Height "+std::to_string(height)+
      " /ColorSpace "+(components==1?"/DeviceGray":"/DeviceRGB")+
      " /BitsPerComponent 8 /Filter /FlateDecode /Length "+
      std::to_string(compressed.size())+" >>\nstream\n"+compressed+"\nendstream");
  const size_t start=pdf.size();
  pdf+="xref\n0 6\n0000000000 65535 f \n";
  char line[32];
  for(size_t i=1;i<positions.size();++i) {
   std::snprintf(line,sizeof(line),"%010zu 00000 n \n",positions[i]);
   pdf+=line;
  }
  pdf+="trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"+
      std::to_string(start)+"\n%%EOF\n";
  auto* result=(uint8_t*)std::malloc(pdf.size());
  if(!result)return RINPDF_OUT_OF_MEMORY;
  std::memcpy(result,pdf.data(),pdf.size());
  *data=result;*size=pdf.size();
  return RINPDF_OK;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
}
extern "C" rinpdf_status rinpdf_write_image_memory(const uint8_t* pixels,
 unsigned width,unsigned height,size_t stride,unsigned components,
 uint8_t** data,size_t* size) {
 return create_image_pdf(pixels,width,height,stride,components,data,size);
}
extern "C" rinpdf_status rinpdf_write_image_file(const char* path,const uint8_t* pixels,
 unsigned width,unsigned height,size_t stride,unsigned components) {
 if(!path)return RINPDF_INVALID_ARGUMENT;
 uint8_t* bytes=nullptr;size_t size=0;
 const auto status=create_image_pdf(pixels,width,height,stride,components,&bytes,&size);
 if(status!=RINPDF_OK)return status;
 FILE* file=std::fopen(path,"wb");
 if(!file){std::free(bytes);return RINPDF_IO_ERROR;}
 bool ok=std::fwrite(bytes,1,size,file)==size;
 if(std::fclose(file)!=0)ok=false;
 std::free(bytes);
 return ok?RINPDF_OK:RINPDF_IO_ERROR;
}
