#include "../src/parser/text_extract.h"
#include <cstdio>
#include <map>
#include <string>
using namespace rinpdf::parser;
int main(){
 const std::string content="BT /F1 12 Tf 50 Tz 10 Tw [<0041> -300 <0042>] TJ 12 TL T* <0041> Tj ET";
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
 // Verify explicit and default descendant CID widths used by the
 // Identity-H glyph advance path.
 std::map<std::string,PageFont> fonts;
 if(!read_page_fonts(reinterpret_cast<const uint8_t*>(pdf.data()),
      pdf.size(),0,fonts)||fonts.count("F1")!=1)return 3;
 double space_width=0,letter_width=0;
 if(!cid_width(fonts.at("F1").cid_widths,32,space_width)||
    !cid_width(fonts.at("F1").cid_widths,65,letter_width)||
    space_width!=250||letter_width!=1000)return 4;
 std::string extracted;
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(pdf.data()),
     pdf.size(),0,extracted))return 1;
 if(extracted!="A B\nA")return 2;
 // Remove the explicit space override while preserving object byte counts.
 // /DW=1000 must now define the space width, preventing the -300 TJ
 // movement from being mistaken for a full inferred space.
 std::string default_space_pdf=pdf;
 const std::string explicit_width="/W [32 [250]]";
 const std::string unrelated_width="/W [65 [250]]";
 static_assert(sizeof("/W [32 [250]]")==sizeof("/W [65 [250]]"));
 const auto width_at=default_space_pdf.find(explicit_width);
 if(width_at==std::string::npos)return 5;
 default_space_pdf.replace(width_at,explicit_width.size(),unrelated_width);
 std::string default_result;
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(default_space_pdf.data()),
     default_space_pdf.size(),0,default_result))return 6;
 if(default_result!="AB\nA")return 7;
 // A 100-unit CID space is narrower than the previous 250-unit one.
 // Keep all PDF stream lengths and xref offsets unchanged in both edits.
 std::string narrow_pdf=pdf;
 const auto narrow_width_at=narrow_pdf.find("/W [32 [250]]");
 const auto narrow_gap_at=narrow_pdf.find("-300");
 if(narrow_width_at==std::string::npos||
    narrow_gap_at==std::string::npos)return 8;
 narrow_pdf.replace(narrow_width_at,sizeof("/W [32 [250]]")-1,
                    "/W [32 [100]]");
 narrow_pdf.replace(narrow_gap_at,4,"-150");
 std::string narrow_result;
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(narrow_pdf.data()),
      narrow_pdf.size(),0,narrow_result))return 9;
 if(narrow_result!="A B\nA")return 10;
 // The same gap is insufficient with the original 250-unit space.
 std::string regular_gap_pdf=pdf;
 regular_gap_pdf.replace(narrow_gap_at,4,"-150");
 std::string regular_gap_result;
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(regular_gap_pdf.data()),
      regular_gap_pdf.size(),0,regular_gap_result))return 11;
 if(regular_gap_result!="AB\nA")return 12;
 // One Identity-H CID may decode to two Unicode characters, but it
 // still has exactly one CID advance. Rebuild offsets for the longer CMap.
 const std::string multi_content=
  "BT /F1 10 Tf 1 0 0 1 0 0 Tm <0041> Tj "
  "1 0 0 1 15 0 Tm <0042> Tj ET";
 const std::string multi_cmap=
  "2 beginbfchar <0041> <65E5672C> <0042> <0042> endbfchar";
 std::string multi_pdf="%PDF-1.5\n";
 size_t multi_offsets[8]{};
 auto add_multi=[&](unsigned id,const std::string& body){
  multi_offsets[id]=multi_pdf.size();
  multi_pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 add_multi(1,"<< /Type /Catalog /Pages 2 0 R >>");
 add_multi(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] /Resources << /Font << /F1 4 0 R >> >> >>");
 add_multi(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Contents 6 0 R >>");
 add_multi(4,"<< /Type /Font /Subtype /Type0 /BaseFont /Example /Encoding /Identity-H /DescendantFonts [5 0 R] /ToUnicode 7 0 R >>");
 add_multi(5,"<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Example /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /CIDToGIDMap /Identity /DW 1000 /W [65 [500]] >>");
 add_multi(6,"<< /Length "+std::to_string(multi_content.size())+" >>\nstream\n"+multi_content+"\nendstream");
 add_multi(7,"<< /Length "+std::to_string(multi_cmap.size())+" >>\nstream\n"+multi_cmap+"\nendstream");
 const size_t multi_xref=multi_pdf.size();
 multi_pdf+="xref\n0 8\n0000000000 65535 f \n";
 for(unsigned id=1;id<=7;++id){
  char row[32];
  std::snprintf(row,sizeof(row),"%010zu 00000 n \n",multi_offsets[id]);
  multi_pdf+=row;
 }
 multi_pdf+="trailer\n<< /Root 1 0 R /Size 8 >>\nstartxref\n"+
            std::to_string(multi_xref)+"\n%%EOF\n";
 std::string multi_result;
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(multi_pdf.data()),
      multi_pdf.size(),0,multi_result))return 13;
 if(multi_result!="日本 B")return 14;
 // Identity-V uses vertical displacements: a horizontal TJ-space
 // heuristic must not synthesize spaces between vertical CID glyphs.
 std::string vertical_pdf=pdf;
 const auto encoding_at=vertical_pdf.find("/Identity-H");
 const auto adjustment_at=vertical_pdf.find("-300");
 if(encoding_at==std::string::npos||
    adjustment_at==std::string::npos)return 15;
 vertical_pdf.replace(encoding_at,sizeof("/Identity-H")-1,"/Identity-V");
 vertical_pdf.replace(adjustment_at,4,"-900");
 std::string vertical_text;
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(vertical_pdf.data()),
      vertical_pdf.size(),0,vertical_text))return 16;
 if(vertical_text!="AB\nA")return 17;
 return 0;
}
