#include "../src/parser/text_extract.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using namespace rinpdf::parser;
int main(){
 const std::string content=
  "/Span << /ActualText <FEFF65E5672C> >> BDC "
  "BT /F1 12 Tf <41> Tj ET EMC "
  "BT /F1 12 Tf <41> Tj ET";
 std::string pdf="%PDF-1.5\n";
 size_t offsets[7]{};
 auto append=[&](unsigned id,const std::string& body){
  offsets[id]=pdf.size();
  pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 append(1,"<< /Type /Catalog /Pages 2 0 R >>");
 append(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] /Resources << /Font << /F1 4 0 R >> >> >>");
 append(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Contents 5 0 R >>");
 append(4,"<< /Type /Font /Subtype /TrueType /BaseFont /Example /Encoding /WinAnsiEncoding /ToUnicode 6 0 R >>");
 append(5,"<< /Length "+std::to_string(content.size())+" >>\nstream\n"+content+"\nendstream");
 const std::string cmap="1 beginbfchar <41> <0041> endbfchar";
 append(6,"<< /Length "+std::to_string(cmap.size())+" >>\nstream\n"+cmap+"\nendstream");
 const size_t xref=pdf.size();
 pdf+="xref\n0 7\n0000000000 65535 f \n";
 for(unsigned i=1;i<=6;++i){
  char row[32];std::snprintf(row,sizeof(row),"%010zu 00000 n \n",offsets[i]);
  pdf+=row;
 }
 pdf+="trailer\n<< /Root 1 0 R /Size 7 >>\nstartxref\n"+
      std::to_string(xref)+"\n%%EOF\n";
 std::string result;
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(pdf.data()),pdf.size(),0,result))return 1;
 if(result!=std::string("\xe6\x97\xa5\xe6\x9c\xac")+"A")return 2;
 return 0;
}
