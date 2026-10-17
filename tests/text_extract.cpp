#include "../src/parser/text_extract.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using namespace rinpdf::parser;
int main(){
 const std::string content=
  "/Span /P1 BDC BT /F1 12 Tf <41> Tj ET EMC "
  "/Span << /ActualText <FEFF65E5672C> >> BDC "
  "BT /F1 12 Tf <41> Tj ET EMC "
  "BT /F1 12 Tf <41> Tj <42> Tj T* <42> Tj 0 -16 Td <42> Tj 1 0 0 1 0 30 Tm <42> Tj "
  "0 1 -1 0 50 0 Tm <42> Tj "
  "0 1 -1 0 50 20 Tm <42> Tj "
  "0 1 -1 0 40 20 Tm <42> Tj [<42> -600 <> <42>] TJ [<42> -700 250 <42>] TJ [<42> -600] TJ <42> Tj 2 Tc [<42> -600 <42>] TJ 0 Tc 0 Tz [<42> -900 <42>] TJ 100 Tz 2 Tw [<42> -600 <42>] TJ 0 Tw <42> ' 3 1 <42> \" ET";
 std::string pdf="%PDF-1.5\n";
 size_t offsets[8]{};
 auto append=[&](unsigned id,const std::string& body){
  offsets[id]=pdf.size();
  pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 append(1,"<< /Type /Catalog /Pages 2 0 R >>");
 append(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] /Resources << /Font << /F1 4 0 R >> /CMap << /Parent 7 0 R >> /Properties << /P1 << /ActualText <FEFF4E16> >> >> >> >>");
 append(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Contents 5 0 R >>");
 append(4,"<< /Type /Font /Subtype /TrueType /BaseFont /Example /Encoding /WinAnsiEncoding /ToUnicode 6 0 R >>");
 append(5,"<< /Length "+std::to_string(content.size())+" >>\nstream\n"+content+"\nendstream");
 const std::string cmap="2 beginbfchar <20> <0020> <41> <00410042> endbfchar";
 append(6,"<< /UseCMap /Parent /CMapName /Child /Length "+std::to_string(cmap.size())+" >>\nstream\n"+cmap+"\nendstream");
 const std::string parent_cmap="1 beginbfchar <42> <0042> endbfchar";
 append(7,"<< /CMapName /Parent /Length "+std::to_string(parent_cmap.size())+" >>\nstream\n"+parent_cmap+"\nendstream");
 const size_t xref=pdf.size();
 pdf+="xref\n0 8\n0000000000 65535 f \n";
 for(unsigned i=1;i<=7;++i){
  char row[32];std::snprintf(row,sizeof(row),"%010zu 00000 n \n",offsets[i]);
  pdf+=row;
 }
 pdf+="trailer\n<< /Root 1 0 R /Size 8 >>\nstartxref\n"+
      std::to_string(xref)+"\n%%EOF\n";
 // Diagnose whether page streams, fonts/CMap, or named Properties fail.
 std::vector<uint8_t> decoded_contents;
 if(!read_page_contents(reinterpret_cast<const uint8_t*>(pdf.data()),
      pdf.size(),0,decoded_contents))return 10;
 std::map<std::string,PageFont> resolved_fonts;
 if(!read_page_fonts(reinterpret_cast<const uint8_t*>(pdf.data()),
      pdf.size(),0,resolved_fonts))return 11;
 if(resolved_fonts.count("F1")!=1)return 12;
 const auto& unicode=resolved_fonts.at("F1").unicode_sequences;
 if(unicode.count({1,0x41})!=1||unicode.count({1,0x42})!=1)return 13;
 PageProperties page_properties;
 if(!read_page_properties(reinterpret_cast<const uint8_t*>(pdf.data()),
      pdf.size(),0,page_properties))return 14;
 if(page_properties.count("P1")!=1||
    !page_properties.at("P1").has_actual_text)return 15;
 std::string result;
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(pdf.data()),pdf.size(),0,result))return 1;
 if(result!=std::string("\xe4\xb8\x96\xe6\x97\xa5\xe6\x9c\xac")+"ABB\nB\nB\nB\nBB\nBB BBBB BBBBBB\nB\nB")return 2;
 // Changing only the resource key keeps object offsets intact and must
 // cause a clean failure rather than silently using an unresolved parent.
 std::string missing_parent=pdf;
 const std::string lookup="/CMap << /Parent 7 0 R >>";
 const auto pos=missing_parent.find(lookup);
 if(pos==std::string::npos)return 3;
 missing_parent.replace(pos,lookup.size(),"/CMap << /Absent 7 0 R >>");
 const std::string previous=result;
 if(extract_page_text(reinterpret_cast<const uint8_t*>(missing_parent.data()),
      missing_parent.size(),0,result))return 4;
 if(result!=previous)return 5;
 // Keep the object size and xref offsets unchanged when testing
 // a parent whose declared CMapName does not match the resource key.
 std::string mismatched_name=pdf;
 const std::string declaration="/CMapName /Parent";
 const auto name_pos=mismatched_name.find(declaration);
 if(name_pos==std::string::npos)return 6;
 mismatched_name.replace(name_pos,declaration.size(),"/CMapName /NoName");
 if(extract_page_text(reinterpret_cast<const uint8_t*>(mismatched_name.data()),
      mismatched_name.size(),0,result))return 7;
 if(result!=previous)return 8;
 return 0;
}
