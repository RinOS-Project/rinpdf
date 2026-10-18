#include "../src/parser/text_extract.h"
#include <cstdio>
#include <map>
#include <utility>
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
 // Same-column Td must not create a newline. Replace the Tm command
 // with an equal-length Td plus whitespace to retain all xref offsets.
 std::string same_column=pdf;
 const std::string from="1 0 0 1 20 0 Tm";
 const std::string to="0 -5 Td        ";
 static_assert(sizeof("1 0 0 1 20 0 Tm")==sizeof("0 -5 Td        "));
 const auto at=same_column.find(from);
 if(at==std::string::npos)return 6;
 same_column.replace(at,from.size(),to);
 std::string same_column_text;
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(same_column.data()),
      same_column.size(),0,same_column_text))return 7;
 if(same_column_text!="日本")return 8;
 // Separate BT/ET objects: the vertical endpoint must be retained
 // for column movement without applying horizontal space heuristics.
 const auto make_two_objects=[&](int x){
  const std::string stream=
    "BT /F1 10 Tf 1 0 0 1 0 0 Tm <0041> Tj ET "
    "BT /F1 10 Tf 1 0 0 1 "+std::to_string(x)+
    " -8 Tm <0042> Tj ET";
  std::string doc="%PDF-1.5\n";
  size_t at_obj[8]{};
  auto write=[&](unsigned id,const std::string& body){
   at_obj[id]=doc.size();
   doc+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
  };
  write(1,"<< /Type /Catalog /Pages 2 0 R >>");
  write(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] /Resources << /Font << /F1 4 0 R >> >> >>");
  write(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Contents 6 0 R >>");
  write(4,"<< /Type /Font /Subtype /Type0 /BaseFont /Example /Encoding /Identity-V /DescendantFonts [5 0 R] /ToUnicode 7 0 R >>");
  write(5,"<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Example /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /DW 1000 /DW2 [880 -900] /W2 [65 [-800 500 900]] >>");
  write(6,"<< /Length "+std::to_string(stream.size())+" >>\nstream\n"+stream+"\nendstream");
  write(7,"<< /Length "+std::to_string(cmap.size())+" >>\nstream\n"+cmap+"\nendstream");
  const auto xref_at=doc.size();
  doc+="xref\n0 8\n0000000000 65535 f \n";
  for(unsigned id=1;id<=7;++id){
   char row[32];std::snprintf(row,sizeof(row),"%010zu 00000 n \n",at_obj[id]);
   doc+=row;
  }
  doc+="trailer\n<< /Root 1 0 R /Size 8 >>\nstartxref\n"+
       std::to_string(xref_at)+"\n%%EOF\n";
  return doc;
 };
 const std::pair<int,const char*> cases[]={{0,"日本"},{20,"日\n本"}};
 for(const auto& [x,want]:cases){
  const auto test_pdf=make_two_objects(x);
  std::string output;
  if(!extract_page_text(reinterpret_cast<const uint8_t*>(test_pdf.data()),
       test_pdf.size(),0,output))return 9;
  if(output!=want)return 10;
 }
 // Identity-V T* follows the vertical writing axis and must not
 // synthesize a column break. Preserve /Length and xref offsets.
 std::string line_move=pdf;
 const std::string old_move="1 0 0 1 20 0 Tm";
 const std::string new_move="10 TL T*       ";
 static_assert(sizeof("1 0 0 1 20 0 Tm")==sizeof("10 TL T*       "));
 const auto move_at=line_move.find(old_move);
 if(move_at==std::string::npos)return 11;
 line_move.replace(move_at,old_move.size(),new_move);
 std::string line_text;
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(line_move.data()),
      line_move.size(),0,line_text))return 12;
 if(line_text!="日本")return 13;
 // Unlike the in-column movement above, a Td perpendicular to the
 // Identity-V writing axis starts another column.
 std::string other_column=pdf;
 const std::string column_move="20 0 Td         ";
 static_assert(sizeof("1 0 0 1 20 0 Tm")==sizeof("20 0 Td         "));
 const auto column_at=other_column.find(from);
 if(column_at==std::string::npos)return 14;
 other_column.replace(column_at,from.size(),column_move);
 std::string column_text;
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(other_column.data()),
      other_column.size(),0,column_text))return 15;
 if(column_text!="日\n本")return 16;
 return 0;
}
