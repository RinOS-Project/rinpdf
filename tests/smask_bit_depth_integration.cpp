#include "../src/parser/contents.h"
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <zlib.h>
using rinpdf::parser::PageImage;
static std::string flate(const std::string& raw){
 uLongf size=compressBound(raw.size());
 std::string out(size,'\0');
 if(compress2(reinterpret_cast<Bytef*>(out.data()),&size,
              reinterpret_cast<const Bytef*>(raw.data()),raw.size(),
              Z_BEST_COMPRESSION)!=Z_OK)return {};
 out.resize(size);return out;
}
static bool check(unsigned bits,const std::string& samples,
                  const std::vector<uint8_t>& expected){
 std::string pdf="%PDF-1.4\n";
 size_t offsets[6]{};
 auto add=[&](unsigned id,const std::string& body){
  offsets[id]=pdf.size();
  pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 const std::string compressed=flate(samples);
 if(compressed.empty())return false;
 add(1,"<< /Type /Catalog /Pages 2 0 R >>");
 add(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] >>");
 add(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 3 1] /Resources << /XObject << /Im 4 0 R >> >> >>");
 add(4,"<< /Type /XObject /Subtype /Image /Width 3 /Height 1 /ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask 5 0 R /Length 9 >>\nstream\n"+
       std::string("\xff\0\0\0\xff\0\0\0\xff",9)+"\nendstream");
 add(5,"<< /Type /XObject /Subtype /Image /Width 3 /Height 1 /ColorSpace /DeviceGray /BitsPerComponent "+
       std::to_string(bits)+" /Filter /FlateDecode /DecodeParms << /Predictor 1 /Colors 1 /Columns 3 /BitsPerComponent "+
       std::to_string(bits)+" >> /Length "+std::to_string(compressed.size())+" >>\nstream\n"+
       compressed+"\nendstream");
 const size_t xref=pdf.size();
 pdf+="xref\n0 6\n0000000000 65535 f \n";
 for(unsigned id=1;id<=5;++id){
  char entry[32];std::snprintf(entry,sizeof(entry),"%010zu 00000 n \n",offsets[id]);
  pdf+=entry;
 }
 pdf+="trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"+
      std::to_string(xref)+"\n%%EOF\n";
 std::map<std::string,PageImage> images;
 if(!rinpdf::parser::read_page_images(
       reinterpret_cast<const uint8_t*>(pdf.data()),pdf.size(),0,images))
  return false;
 const auto it=images.find("Im");
 return it!=images.end()&&it->second.alpha==expected;
}
int main(){
 if(!check(1,std::string("\x40",1),{0,255,0}))return 1;
 if(!check(2,std::string("\x1b",1),{0,85,255}))return 2;
 if(!check(4,std::string("\x08\xf0",2),{0,136,255}))return 3;
 if(!check(16,std::string("\0\0\x80\0\xff\xff",6),{0,128,255}))return 4;
 return 0;
}
