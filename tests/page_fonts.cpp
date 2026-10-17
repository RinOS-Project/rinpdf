#include "../src/parser/contents.h"
#include <cstdio>
#include <string>
#include <map>
using namespace rinpdf::parser;
int main(){
 std::string pdf="%PDF-1.4\n";
 size_t offsets[5]{};
 auto object=[&](unsigned id,const std::string& body){
  offsets[id]=pdf.size();
  pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 object(1,"<< /Type /Catalog /Pages 2 0 R >>");
 object(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] /Resources << /Font << /F1 4 0 R >> >> >>");
 object(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] >>");
 object(4,"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
 const size_t start=pdf.size();
 pdf+="xref\n0 5\n0000000000 65535 f \n";
 for(unsigned i=1;i<=4;++i){
  char row[32];std::snprintf(row,sizeof(row),"%010zu 00000 n \n",offsets[i]);
  pdf+=row;
 }
 pdf+="trailer\n<< /Root 1 0 R /Size 5 >>\nstartxref\n"+
      std::to_string(start)+"\n%%EOF\n";
 std::map<std::string,PageFont> fonts;
 if(!read_page_fonts(reinterpret_cast<const uint8_t*>(pdf.data()),pdf.size(),0,fonts))return 1;
 if(fonts.size()!=1||fonts.at("F1").base_font!="Helvetica"||
    fonts.at("F1").subtype!="Type1")return 2;
 if(read_page_fonts(reinterpret_cast<const uint8_t*>(pdf.data()),pdf.size(),1,fonts))return 3;
 return 0;
}
