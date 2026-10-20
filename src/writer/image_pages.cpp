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
struct EncodedPage {
 std::string image, alpha;
 unsigned width=0,height=0,components=0;
};
bool compress_pixels(const std::vector<unsigned char>& input,std::string& output) {
 if(input.size()>ULONG_MAX)return false;
 uLongf bound=compressBound(static_cast<uLong>(input.size()));
 output.resize((size_t)bound);
 int result=compress2((Bytef*)output.data(),&bound,input.data(),
                      (uLong)input.size(),Z_DEFAULT_COMPRESSION);
 if(result!=Z_OK)return false;
 output.resize((size_t)bound);
 return true;
}
bool encode_page(const rinpdf_image_page& page,EncodedPage& encoded) {
 if(!page.pixels||!page.width||!page.height||page.width>16384||page.height>16384||
    (page.components!=1&&page.components!=3&&page.components!=4)||
    (size_t)page.width*page.height>64000000||
    page.stride<(size_t)page.width*page.components)return false;
 const size_t pixels=(size_t)page.width*page.height;
 const unsigned output_components=page.components==4?3:page.components;
 std::vector<unsigned char> image(pixels*output_components);
 std::vector<unsigned char> alpha;
 if(page.components==4)alpha.resize(pixels);
 for(unsigned y=0;y<page.height;++y){
  const unsigned char* src=page.pixels+(size_t)y*page.stride;
  for(unsigned x=0;x<page.width;++x){
   const size_t pixel=(size_t)y*page.width+x;
   for(unsigned c=0;c<output_components;++c)
    image[pixel*output_components+c]=src[(size_t)x*page.components+c];
   if(page.components==4)alpha[pixel]=src[(size_t)x*4+3];
  }
 }
 if(!compress_pixels(image,encoded.image))return false;
 if(page.components==4&&!compress_pixels(alpha,encoded.alpha))return false;
 encoded.width=page.width;encoded.height=page.height;encoded.components=page.components;
 return true;
}
void append(std::string& pdf,std::vector<size_t>& offsets,const std::string& value) {
 offsets.push_back(pdf.size());
 pdf+=std::to_string(offsets.size()-1)+" 0 obj\n"+value+"\nendobj\n";
}
rinpdf_status generate(const rinpdf_image_page* pages,size_t count,uint8_t** out,size_t* out_size) {
 if(!out||!out_size)return RINPDF_INVALID_ARGUMENT;
 *out=nullptr;*out_size=0;
 if(!pages||!count||count>10000)return RINPDF_INVALID_ARGUMENT;
 try {
  std::vector<EncodedPage> encoded(count);
  for(size_t i=0;i<count;++i)
   if(!encode_page(pages[i],encoded[i]))return RINPDF_INVALID_ARGUMENT;
  std::vector<size_t> ids(count),contents(count),images(count),masks(count,0);
  size_t next=3;
  for(size_t i=0;i<count;++i){
   ids[i]=next++;contents[i]=next++;images[i]=next++;
   if(encoded[i].components==4)masks[i]=next++;
  }
  std::string pdf="%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
  std::vector<size_t> offsets(1,0);
  append(pdf,offsets,"<< /Type /Catalog /Pages 2 0 R >>");
  std::string kids="[";
  for(size_t id:ids)kids+=std::to_string(id)+" 0 R ";
  kids+="]";
  append(pdf,offsets,"<< /Type /Pages /Kids "+kids+" /Count "+std::to_string(count)+" >>");
  for(size_t i=0;i<count;++i){
   const auto& e=encoded[i];
   double scale=std::min({1.0,595.0/e.width,842.0/e.height});
   unsigned w=std::max(1u,(unsigned)(e.width*scale));
   unsigned h=std::max(1u,(unsigned)(e.height*scale));
   append(pdf,offsets,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "+
      std::to_string(w)+" "+std::to_string(h)+
      "] /Resources << /XObject << /Im0 "+std::to_string(images[i])+
      " 0 R >> >> /Contents "+std::to_string(contents[i])+" 0 R >>");
   std::string commands="q\n"+std::to_string(w)+" 0 0 "+
      std::to_string(h)+" 0 0 cm\n/Im0 Do\nQ\n";
   append(pdf,offsets,"<< /Length "+std::to_string(commands.size())+
      " >>\nstream\n"+commands+"endstream");
   append(pdf,offsets,"<< /Type /XObject /Subtype /Image /Width "+
      std::to_string(e.width)+" /Height "+std::to_string(e.height)+
      " /ColorSpace "+(e.components==1?"/DeviceGray":"/DeviceRGB")+
      (masks[i]?" /SMask "+std::to_string(masks[i])+" 0 R":"")+
      " /BitsPerComponent 8 /Filter /FlateDecode /Length "+
      std::to_string(e.image.size())+" >>\nstream\n"+e.image+"\nendstream");
   if(masks[i])
    append(pdf,offsets,"<< /Type /XObject /Subtype /Image /Width "+
      std::to_string(e.width)+" /Height "+std::to_string(e.height)+
      " /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode /Length "+
      std::to_string(e.alpha.size())+" >>\nstream\n"+e.alpha+"\nendstream");
  }
  const size_t xref=pdf.size();
  pdf+="xref\n0 "+std::to_string(offsets.size())+"\n0000000000 65535 f \n";
  char buffer[32];
  for(size_t i=1;i<offsets.size();++i){
   std::snprintf(buffer,sizeof(buffer),"%010zu 00000 n \n",offsets[i]);
   pdf+=buffer;
  }
  pdf+="trailer\n<< /Size "+std::to_string(offsets.size())+
    " /Root 1 0 R >>\nstartxref\n"+std::to_string(xref)+"\n%%EOF\n";
  uint8_t* result=(uint8_t*)std::malloc(pdf.size());
  if(!result)return RINPDF_OUT_OF_MEMORY;
  std::memcpy(result,pdf.data(),pdf.size());
  *out=result;*out_size=pdf.size();
  return RINPDF_OK;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
}
extern "C" rinpdf_status rinpdf_write_image_pages_memory(
 const rinpdf_image_page* pages,size_t count,uint8_t** data,size_t* size){
 return generate(pages,count,data,size);
}
extern "C" rinpdf_status rinpdf_write_image_pages_file(
 const char* path,const rinpdf_image_page* pages,size_t count){
 if(!path)return RINPDF_INVALID_ARGUMENT;
 uint8_t* data=nullptr;size_t size=0;
 rinpdf_status status=generate(pages,count,&data,&size);
 if(status!=RINPDF_OK)return status;
 FILE* file=std::fopen(path,"wb");
 if(!file){std::free(data);return RINPDF_IO_ERROR;}
 bool ok=std::fwrite(data,1,size,file)==size;
 if(std::fclose(file)!=0)ok=false;
 std::free(data);
 return ok?RINPDF_OK:RINPDF_IO_ERROR;
}
