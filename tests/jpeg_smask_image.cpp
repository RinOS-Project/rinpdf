#include "../src/parser/contents.h"
#include <jpeglib.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

int main(){
 jpeg_compress_struct encoder{};
 jpeg_error_mgr errors{};
 encoder.err=jpeg_std_error(&errors);
 jpeg_create_compress(&encoder);
 unsigned char* buffer=nullptr;
 unsigned long length=0;
 jpeg_mem_dest(&encoder,&buffer,&length);
 encoder.image_width=8;
 encoder.image_height=8;
 encoder.input_components=1;
 encoder.in_color_space=JCS_GRAYSCALE;
 jpeg_set_defaults(&encoder);
 jpeg_set_quality(&encoder,100,TRUE);
 jpeg_start_compress(&encoder,TRUE);
 // Constant midgray data avoids reliance on JPEG lossless reproduction.
 unsigned char row[8]={128,128,128,128,128,128,128,128};
 while(encoder.next_scanline<8){
  JSAMPROW p=row;
  jpeg_write_scanlines(&encoder,&p,1);
 }
 jpeg_finish_compress(&encoder);
 jpeg_destroy_compress(&encoder);
 if(!buffer||length==0)return 1;
 const std::string encoded(reinterpret_cast<char*>(buffer),length);
 std::free(buffer);
 std::string pdf="%PDF-1.4\n";
 size_t offsets[6]{};
 auto add=[&](unsigned id,const std::string& body){
  offsets[id]=pdf.size();
  pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 add(1,"<< /Type /Catalog /Pages 2 0 R >>");
 add(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] >>");
 add(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 8 8] /Resources << /XObject << /Im 4 0 R >> >> >>");
 const std::string rgb(8*8*3,char(64));
 add(4,"<< /Type /XObject /Subtype /Image /Width 8 /Height 8 /ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask 5 0 R /Length "+
       std::to_string(rgb.size())+" >>\nstream\n"+rgb+"\nendstream");
 add(5,"<< /Type /XObject /Subtype /Image /Width 8 /Height 8 /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /DCTDecode /Decode [1 0] /Length "+
       std::to_string(encoded.size())+" >>\nstream\n"+encoded+"\nendstream");
 const size_t xref=pdf.size();
 pdf+="xref\n0 6\n0000000000 65535 f \n";
 for(unsigned id=1;id<=5;++id){
  char entry[32];
  std::snprintf(entry,sizeof(entry),"%010zu 00000 n \n",offsets[id]);
  pdf+=entry;
 }
 pdf+="trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"+
      std::to_string(xref)+"\n%%EOF\n";
 std::map<std::string,rinpdf::parser::PageImage> images;
 if(!rinpdf::parser::read_page_images(
      reinterpret_cast<const uint8_t*>(pdf.data()),pdf.size(),0,images))
  return 2;
 const auto found=images.find("Im");
 if(found==images.end()||found->second.alpha.size()!=64||
    found->second.pixels.size()!=192)return 3;
 for(uint8_t alpha:found->second.alpha)
  if(alpha<124||alpha>130)return 4;
 return 0;
}
