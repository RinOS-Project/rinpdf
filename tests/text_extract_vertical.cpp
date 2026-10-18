#include "../src/parser/text_extract.h"
#include <cstdio>
#include <map>
#include <string>
using namespace rinpdf::parser;
int main(){
 const std::string content="BT /F1 10 Tf [<0041> -200] TJ 1 0 0 1 20 0 Tm <0042> Tj ET";
 const std::string cmap="2 beginbfchar <0041> <65E5> <0042> <672C> endbfchar";
 std::string pdf="%PDF-1.5\n";
 size_t offsets[8]{};
 auto obj=[&](unsigned id,const std::string& body){
  offsets[id]=pdf.size();
  pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 obj(1,"<< /Type /Catalog /Pages 2 0 R >>");
 obj(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] /Resources << /Font << /F1 4 0 R >> >> >>");
 obj(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Contents 6 0 R >>");
 obj(4,"<< /Type /Font /Subtype /Type0 /BaseFont /Example /Encoding /Identity-V /DescendantFonts [5 0 R] /ToUnicode 7 0 R >>");
 obj(5,"<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Example /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /DW 1000 /DW2 [880 -900] /W2 [65 [-800 500 900]] >>");
 obj(6,"<< /Length "+std::to_string(content.size())+" >>\nstream\n"+content+"\nendstream");
 obj(7,"<< /Length "+std::to_string(cmap.size())+" >>\nstream\n"+cmap+"\nendstream");
 const size_t xref=pdf.size();
 pdf+="xref\n0 8\n0000000000 65535 f \n";
 for(unsigned id=1;id<=7;++id){
  char row[32];std::snprintf(row,sizeof(row),"%010zu 00000 n \n",offsets[id]);
  pdf+=row;
 }
 pdf+="trailer\n<< /Root 1 0 R /Size 8 >>\nstartxref\n"+
      std::to_string(xref)+"\n%%EOF\n";
 std::map<std::string,PageFont> fonts;
 if(!read_page_fonts(reinterpret_cast<const uint8_t*>(pdf.data()),
      pdf.size(),0,fonts)||fonts.count("F1")!=1)return 1;
 CIDVerticalMetric first,second;
 const auto& font=fonts.at("F1");
 if(font.encoding!="Identity-V"||
    !cid_vertical_metric(font.vertical_metrics,font.cid_widths,65,first)||
    !cid_vertical_metric(font.vertical_metrics,font.cid_widths,66,second))
  return 2;
 if(first.w1y!=-800||first.v1x!=500||first.v1y!=900||
    second.w1y!=-900||second.v1x!=500||second.v1y!=880)return 3;
 std::string extracted;
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(pdf.data()),
      pdf.size(),0,extracted))return 4;
 if(extracted!="日\n本")return 5;
 return 0;
}
