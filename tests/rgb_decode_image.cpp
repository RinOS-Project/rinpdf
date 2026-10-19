#include "../src/parser/contents.h"
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

int main(){
 std::string pdf="%PDF-1.4\n";
 size_t offsets[6]{};
 auto add=[&](unsigned id,const std::string& body){
  offsets[id]=pdf.size();
  pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 add(1,"<< /Type /Catalog /Pages 2 0 R >>");
 add(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] >>");
 add(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 1 1] /Resources << /XObject << /RGB 4 0 R /Gray 5 0 R >> >> >>");
 add(4,"<< /Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Decode [1 0 0 1 0.25 0.75] /Length 3 >>\nstream\n"+std::string("\x00\x80\xff",3)+"\nendstream");
 add(5,"<< /Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceGray /BitsPerComponent 8 /Decode [1 0] /Length 1 >>\nstream\n"+std::string("\x00",1)+"\nendstream");
 const size_t xref=pdf.size();
 pdf+="xref\n0 6\n0000000000 65535 f \n";
 for(unsigned i=1;i<=5;++i){
  char line[32];
  std::snprintf(line,sizeof(line),"%010zu 00000 n \n",offsets[i]);
  pdf+=line;
 }
 pdf+="trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"+
      std::to_string(xref)+"\n%%EOF\n";
 std::map<std::string,rinpdf::parser::PageImage> images;
 if(!rinpdf::parser::read_page_images(
     reinterpret_cast<const uint8_t*>(pdf.data()),pdf.size(),0,images))return 1;
 const auto rgb=images.find("RGB"),gray=images.find("Gray");
 if(rgb==images.end()||gray==images.end())return 2;
 if(rgb->second.channels!=3||rgb->second.pixels.size()!=3||
    rgb->second.pixels[0]!=255||rgb->second.pixels[1]!=128||
    rgb->second.pixels[2]!=191)return 3;
 if(gray->second.channels!=1||gray->second.pixels.size()!=1||
    gray->second.pixels[0]!=255)return 4;
 return 0;
}
