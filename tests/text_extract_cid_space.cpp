#include "../src/parser/text_extract.h"
#include <cstdio>
#include <string>
using namespace rinpdf::parser;
int main(){
 const std::string content="BT /F1 12 Tf 10 Tw [<0041> -300 <0042>] TJ ET";
 const std::string cmap=
  "3 beginbfchar <0020> <0020> <0041> <0041> <0042> <0042> endbfchar";
 std::string pdf="%PDF-1.5\n";
 size_t offsets[8]{};
 auto append=[&](unsigned id,const std::string& body){
  offsets[id]=pdf.size();
  pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 append(1,"<< /Type /Catalog /Pages 2 0 R >>");
 append(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] /Resources << /Font << /F1 4 0 R >> >> >>");
 append(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Contents 6 0 R >>");
 append(4,"<< /Type /Font /Subtype /Type0 /BaseFont /Example /Encoding /Identity-H /DescendantFonts [5 0 R] /ToUnicode 7 0 R >>");
 append(5,"<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Example /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /CIDToGIDMap /Identity /DW 1000 /W [32 [250]] >>");
 append(6,"<< /Length "+std::to_string(content.size())+" >>\nstream\n"+content+"\nendstream");
 append(7,"<< /Length "+std::to_string(cmap.size())+" >>\nstream\n"+cmap+"\nendstream");
 const size_t xref=pdf.size();
 pdf+="xref\n0 8\n0000000000 65535 f \n";
 for(unsigned id=1;id<=7;++id){
  char row[32];std::snprintf(row,sizeof(row),"%010zu 00000 n \n",offsets[id]);
  pdf+=row;
 }
 pdf+="trailer\n<< /Root 1 0 R /Size 8 >>\nstartxref\n"+
       std::to_string(xref)+"\n%%EOF\n";
 std::string extracted;
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(pdf.data()),
     pdf.size(),0,extracted))return 1;
 if(extracted!="A B")return 2;
 return 0;
}
