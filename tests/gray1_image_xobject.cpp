#include "../src/parser/contents.h"
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static std::string fixture(){
 std::string pdf="%PDF-1.4\n";
 size_t offsets[6]{};
 auto add=[&](unsigned id,const std::string& body){
  offsets[id]=pdf.size();
  pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 add(1,"<< /Type /Catalog /Pages 2 0 R >>");
 add(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] >>");
 add(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 10 2] /Resources << /XObject << /Im1 4 0 R /ImInv 5 0 R >> >> >>");
 // Width 10 forces a new byte boundary at each row. Pixel samples:
 // row0: 1010101010; row1: 0101010101.
 const std::string bitmap("\xAA\x80\x55\x40",4);
 add(4,"<< /Type /XObject /Subtype /Image /Width 10 /Height 2 /ColorSpace /DeviceGray /BitsPerComponent 1 /Length 4 >>\nstream\n"+bitmap+"\nendstream");
 add(5,"<< /Type /XObject /Subtype /Image /Width 10 /Height 2 /ColorSpace /DeviceGray /BitsPerComponent 1 /Decode [1 0] /Length 4 >>\nstream\n"+bitmap+"\nendstream");
 const size_t xref=pdf.size();
 pdf+="xref\n0 6\n0000000000 65535 f \n";
 for(unsigned id=1;id<=5;++id){
  char entry[32];
  std::snprintf(entry,sizeof(entry),"%010zu 00000 n \n",offsets[id]);
  pdf+=entry;
 }
 pdf+="trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"+
      std::to_string(xref)+"\n%%EOF\n";
 return pdf;
}
int main(){
 const std::string pdf=fixture();
 std::map<std::string,rinpdf::parser::PageImage> images;
 if(!rinpdf::parser::read_page_images(
     reinterpret_cast<const uint8_t*>(pdf.data()),pdf.size(),0,images))
  return 1;
 const auto it=images.find("Im1");
 if(it==images.end())return 2;
 const auto& im=it->second;
 if(im.width!=10||im.height!=2||im.channels!=1||
    im.pixels.size()!=20)return 3;
 for(size_t y=0;y<2;++y)
  for(size_t x=0;x<10;++x){
   const uint8_t expected=((x+y)%2==0)?255:0;
   if(im.pixels[y*10+x]!=expected)return 4;
  }
 const auto inverse=images.find("ImInv");
 if(inverse==images.end()||inverse->second.pixels.size()!=20)return 5;
 for(size_t i=0;i<20;++i)
  if(inverse->second.pixels[i]!=uint8_t(255-im.pixels[i]))return 6;
 return 0;
}
