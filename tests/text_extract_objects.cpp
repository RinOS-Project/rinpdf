#include "../src/parser/text_extract.h"
#include <cstdio>
#include <string>
#include <vector>
using namespace rinpdf::parser;
static std::string make_pdf(const std::string& content){
 std::string pdf="%PDF-1.5\n";
 size_t offsets[7]{};
 auto obj=[&](int id,const std::string& body){
  offsets[id]=pdf.size();
  pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 obj(1,"<< /Type /Catalog /Pages 2 0 R >>");
 obj(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] /Resources << /Font << /F1 4 0 R >> >> >>");
 obj(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 5 0 R >>");
 obj(4,"<< /Type /Font /Subtype /TrueType /BaseFont /Example /Encoding /WinAnsiEncoding /FirstChar 65 /LastChar 66 /Widths [600 600] /ToUnicode 6 0 R >>");
 obj(5,"<< /Length "+std::to_string(content.size())+" >>\nstream\n"+content+"\nendstream");
 const std::string cmap="2 beginbfchar <41> <0041> <42> <0042> endbfchar";
 obj(6,"<< /Length "+std::to_string(cmap.size())+" >>\nstream\n"+cmap+"\nendstream");
 const size_t xref=pdf.size();
 pdf+="xref\n0 7\n0000000000 65535 f \n";
 for(int i=1;i<=6;i++){
  char line[32];std::snprintf(line,sizeof(line),"%010zu 00000 n \n",offsets[i]);
  pdf+=line;
 }
 pdf+="trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n"+
      std::to_string(xref)+"\n%%EOF\n";
 return pdf;
}
int main(){
 const auto check=[](const std::string& content,const std::string& expected){
  const std::string pdf=make_pdf(content);
  std::string actual;
  return extract_page_text(reinterpret_cast<const uint8_t*>(pdf.data()),
      pdf.size(),0,actual)&&actual==expected;
 };
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm <41> Tj ET "
           "BT /F1 10 Tf 1 0 0 1 20 0 Tm <42> Tj ET","A B"))return 1;
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm <41> Tj ET "
           "BT /F1 10 Tf 1 0 0 1 8 0 Tm <42> Tj ET","AB"))return 2;
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm <41> Tj ET "
           "BT /F1 10 Tf 1 0 0 1 20 15 Tm <42> Tj ET","AB"))return 3;
 return 0;
}
