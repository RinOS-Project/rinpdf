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
 add(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 2 1] /Resources << /XObject << /Im 4 0 R >> >> >>");
 // First pixel is the exact color key and must become transparent;
 // second pixel differs in blue and must remain opaque.
 const std::string raw("\x0a\x14\x1e\x0a\x14\x1f",6);
 add(4,"<< /Type /XObject /Subtype /Image /Width 2 /Height 1 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Mask [10 10 20 20 30 30] /Length 6 >>\nstream\n"+raw+"\nendstream");
 const size_t xref=pdf.size();
 pdf+="xref\n0 5\n0000000000 65535 f \n";
 for(unsigned i=1;i<=4;++i){
  char line[32];
  std::snprintf(line,sizeof(line),"%010zu 00000 n \n",offsets[i]);
  pdf+=line;
 }
 pdf+="trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n"+
      std::to_string(xref)+"\n%%EOF\n";
 std::map<std::string,rinpdf::parser::PageImage> images;
 if(!rinpdf::parser::read_page_images(
      reinterpret_cast<const uint8_t*>(pdf.data()),pdf.size(),0,images))return 1;
 const auto it=images.find("Im");
 if(it==images.end()||it->second.channels!=3||
    it->second.alpha.size()!=2||it->second.alpha[0]!=0||
    it->second.alpha[1]!=255)return 2;
 return 0;
}
