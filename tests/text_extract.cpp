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
 // Extra numeric operands before Tj are malformed. Keep the byte length
 // unchanged so the stream /Length and following xref offsets stay valid.
 std::string stray_tj_operands=pdf;
 const std::string good_show="0 Tw <42> '";
 const std::string bad_show="1 2 <42> Tj";
 static_assert(sizeof("0 Tw <42> '")==sizeof("1 2 <42> Tj"));
 const auto show_pos=stray_tj_operands.find(good_show);
 if(show_pos==std::string::npos)return 21;
 stray_tj_operands.replace(show_pos,good_show.size(),bad_show);
 if(extract_page_text(reinterpret_cast<const uint8_t*>(stray_tj_operands.data()),
      stray_tj_operands.size(),0,result))return 22;
 if(result!=previous)return 23;
 // TJ must not silently discard numeric operands preceding its array.
 std::string stray_tj_array=pdf;
 const std::string proper_tj="0 Tz [<42>";
 const std::string invalid_tj="1 2  [<42>";
 static_assert(sizeof("0 Tz [<42>")==sizeof("1 2  [<42>"));
 const auto array_pos=stray_tj_array.find(proper_tj);
 if(array_pos==std::string::npos)return 24;
 stray_tj_array.replace(array_pos,proper_tj.size(),invalid_tj);
 if(extract_page_text(reinterpret_cast<const uint8_t*>(stray_tj_array.data()),
      stray_tj_array.size(),0,result))return 25;
 if(result!=previous)return 26;
 // A text array without a following TJ must not emit glyphs.
 // The mutation has equal length, preserving stream lengths and xref.
 std::string missing_tj=pdf;
 const std::string original_array_end="] TJ 0 Tc";
 const std::string invalid_array_end="] Tj 0 Tc";
 static_assert(sizeof("] TJ 0 Tc")==sizeof("] Tj 0 Tc"));
 const auto end_pos=missing_tj.find(original_array_end);
 if(end_pos==std::string::npos)return 27;
 missing_tj.replace(end_pos,original_array_end.size(),invalid_array_end);
 if(extract_page_text(reinterpret_cast<const uint8_t*>(missing_tj.data()),
      missing_tj.size(),0,result))return 28;
 if(result!=previous)return 29;
 // Reject nested BT and unmatched ET without exposing partial output.
 // These replacements preserve the stream length and xref offsets.
 const auto reject_boundary=[&](const std::string& from,
      const std::string& to,int not_found,int accepted,int mutated){
  if(from.size()!=to.size())return not_found;
  std::string modified=pdf;
  const auto at=modified.find(from);
  if(at==std::string::npos)return not_found;
  modified.replace(at,from.size(),to);
  if(extract_page_text(reinterpret_cast<const uint8_t*>(modified.data()),
       modified.size(),0,result))return accepted;
  if(result!=previous)return mutated;
  return 0;
 };
 if(const int error=reject_boundary("BDC BT /F1","BDC ET /F1",30,31,32))
  return error;
 if(const int error=reject_boundary("Tj ET EMC","Tj BT EMC",33,34,35))
  return error;
 if(const int error=reject_boundary("<41> Tj ET EMC","<41> Tj BT EMC",36,37,38))
  return error;
 // T* does not accept operands. Preserve the PDF stream byte count.
 if(const int error=reject_boundary("T* <42> Tj","1 T*<42> Tj",
      39,40,41))return error;
 // TL sets leading while leaving the existing text runs unchanged.
 // Same-length PDF mutations preserve /Length and classic xref offsets.
 std::string explicit_leading=pdf;
 const std::string original_leading="0 Tw <42> '";
 const std::string valid_leading="8 TL <42> '";
 static_assert(sizeof("0 Tw <42> '")==sizeof("8 TL <42> '"));
 const auto leading_pos=explicit_leading.find(original_leading);
 if(leading_pos==std::string::npos)return 42;
 explicit_leading.replace(leading_pos,original_leading.size(),valid_leading);
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(explicit_leading.data()),
      explicit_leading.size(),0,result))return 43;
 if(result!=previous)return 44;
 // TL with no numeric operand must fail without publishing partial text.
 std::string missing_leading=pdf;
 const std::string invalid_leading="x TL <42> '";
 static_assert(sizeof("0 Tw <42> '")==sizeof("x TL <42> '"));
 missing_leading.replace(leading_pos,original_leading.size(),invalid_leading);
 if(extract_page_text(reinterpret_cast<const uint8_t*>(missing_leading.data()),
      missing_leading.size(),0,result))return 45;
 if(result!=previous)return 46;
 // The UTF-16BE ActualText payload must reject lone low surrogates.
 // Replace equal-length hex digits so all xref offsets remain valid.
 std::string unpaired_low=pdf;
 const std::string valid_actual="<FEFF65E5672C>";
 const auto actual_pos=unpaired_low.find(valid_actual);
 if(actual_pos==std::string::npos)return 16;
 unpaired_low.replace(actual_pos,valid_actual.size(),"<FEFFDC00672C>");
 if(extract_page_text(reinterpret_cast<const uint8_t*>(unpaired_low.data()),
      unpaired_low.size(),0,result))return 17;
 if(result!=previous)return 18;
 // A high surrogate without its required low surrogate must also fail.
 std::string unpaired_high=pdf;
 unpaired_high.replace(actual_pos,valid_actual.size(),"<FEFFD800672C>");
 if(extract_page_text(reinterpret_cast<const uint8_t*>(unpaired_high.data()),
      unpaired_high.size(),0,result))return 19;
 if(result!=previous)return 20;
 return 0;
}
