#include "../src/parser/contents.h"
#include <cstdio>
#include <string>
#include <map>
using namespace rinpdf::parser;
int main(){
 std::string pdf="%PDF-1.5\n";
 size_t offsets[6]{};
 auto append=[&](unsigned id,const std::string& body){
  offsets[id]=pdf.size();
  pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 append(1,"<< /Type /Catalog /Pages 2 0 R >>");
 append(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] /Resources << /Font << /F1 4 0 R >> >> >>");
 append(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 20 20] >>");
 append(4,"<< /Type /Font /Subtype /Type0 /BaseFont /Example /Encoding /Identity-H /DescendantFonts [5 0 R] >>");
 append(5,"<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Example /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /CIDToGIDMap /Identity >>");
 const size_t xref=pdf.size();
 pdf+="xref\n0 6\n0000000000 65535 f \n";
 for(unsigned id=1;id<=5;++id){
  char row[32];
  std::snprintf(row,sizeof(row),"%010zu 00000 n \n",offsets[id]);
  pdf+=row;
 }
 pdf+="trailer\n<< /Root 1 0 R /Size 6 >>\nstartxref\n"+
      std::to_string(xref)+"\n%%EOF\n";
 std::map<std::string,PageFont> fonts;
 if(!read_page_fonts(reinterpret_cast<const uint8_t*>(pdf.data()),pdf.size(),0,fonts))return 1;
 if(fonts.size()!=1||fonts.at("F1").subtype!="Type0"||
    fonts.at("F1").encoding!="Identity-H"||
    !fonts.at("F1").cid_map.identity)return 2;
 return 0;
}
