#include "../src/parser/contents.h"
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

int main(){
 std::string pdf="%PDF-1.4\n";
 size_t offsets[5]{};
 auto add=[&](unsigned id,const std::string& body){
  offsets[id]=pdf.size();
  pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 add(1,"<< /Type /Catalog /Pages 2 0 R >>");
 add(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] >>");
 add(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 2 1] /Resources << /XObject << /CMYK 4 0 R >> >> >>");
 const std::string pixels("\x00\x00\x00\x00\xff\xff\xff\x00",8);
 add(4,"<< /Type /XObject /Subtype /Image /Width 2 /Height 1 /ColorSpace /DeviceCMYK /BitsPerComponent 8 /Decode [1 0 0 1 0 1 0 1] /Length 8 >>\nstream\n"+pixels+"\nendstream");
 const size_t xref=pdf.size();
 pdf+="xref\n0 5\n0000000000 65535 f \n";
 for(unsigned id=1;id<=4;++id){
  char line[32];
  std::snprintf(line,sizeof(line),"%010zu 00000 n \n",offsets[id]);
  pdf+=line;
 }
 pdf+="trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n"+
      std::to_string(xref)+"\n%%EOF\n";
 std::map<std::string,rinpdf::parser::PageImage> images;
 if(!rinpdf::parser::read_page_images(
       reinterpret_cast<const uint8_t*>(pdf.data()),pdf.size(),0,images))return 1;
 const auto it=images.find("CMYK");
 if(it==images.end()||it->second.channels!=3||
    it->second.pixels.size()!=6)return 2;
 const uint8_t expected[6]={0,255,255,255,0,0};
 for(unsigned i=0;i<6;++i)
  if(it->second.pixels[i]!=expected[i])return 3;
 return 0;
}
