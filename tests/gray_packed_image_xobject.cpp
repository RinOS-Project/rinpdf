#include "../src/parser/contents.h"
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

int main(){
 std::string pdf="%PDF-1.4\n";
 size_t offsets[6]{};
 auto add=[&](int id,const std::string& body){
  offsets[id]=pdf.size();
  pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 add(1,"<< /Type /Catalog /Pages 2 0 R >>");
 add(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] >>");
 add(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 5 2] /Resources << /XObject << /G2 4 0 R /G4 5 0 R >> >> >>");
 // 5 pixels per scanline. Group 2: [0,1,2,3,0] then [3,2,1,0,3].
 const std::string two("\x1b\x00\xe4\xc0",4);
 add(4,"<< /Type /XObject /Subtype /Image /Width 5 /Height 2 /ColorSpace /DeviceGray /BitsPerComponent 2 /Length 4 >>\nstream\n"+two+"\nendstream");
 // Group 4: [0,5,10,15,0] then [15,10,5,0,15].
 const std::string four("\x05\xaf\x00\xfa\x50\xf0",6);
 add(5,"<< /Type /XObject /Subtype /Image /Width 5 /Height 2 /ColorSpace /DeviceGray /BitsPerComponent 4 /Length 6 >>\nstream\n"+four+"\nendstream");
 const size_t xref=pdf.size();
 pdf+="xref\n0 6\n0000000000 65535 f \n";
 for(int i=1;i<=5;++i){
  char line[32];
  std::snprintf(line,sizeof(line),"%010zu 00000 n \n",offsets[i]);
  pdf+=line;
 }
 pdf+="trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"+
      std::to_string(xref)+"\n%%EOF\n";
 std::map<std::string,rinpdf::parser::PageImage> images;
 if(!rinpdf::parser::read_page_images(
      reinterpret_cast<const uint8_t*>(pdf.data()),pdf.size(),0,images))
  return 1;
 const unsigned levels2[10]={0,1,2,3,0,3,2,1,0,3};
 const unsigned levels4[10]={0,5,10,15,0,15,10,5,0,15};
 for(const auto& name:{"G2","G4"}){
  const auto it=images.find(name);
  if(it==images.end()||it->second.channels!=1||
     it->second.pixels.size()!=10)return 2;
  const unsigned *levels=std::string(name)=="G2"?levels2:levels4;
  const unsigned max=std::string(name)=="G2"?3u:15u;
  for(size_t i=0;i<10;++i)
   if(it->second.pixels[i]!=(levels[i]*255u+max/2)/max)return 3;
 }
 return 0;
}
