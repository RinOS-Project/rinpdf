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
 append(4,"<< /Type /Font /Subtype /TrueType /BaseFont /Example /Encoding /WinAnsiEncoding /FirstChar 65 /LastChar 66 /Widths [600 500] /ToUnicode 6 0 R >>");
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
 if(resolved_fonts.at("F1").simple_widths.at(65)!=600||
    resolved_fonts.at("F1").simple_widths.at(66)!=500)return 69;
 PageProperties page_properties;
 if(!read_page_properties(reinterpret_cast<const uint8_t*>(pdf.data()),
      pdf.size(),0,page_properties))return 14;
 if(page_properties.count("P1")!=1||
    !page_properties.at("P1").has_actual_text)return 15;
 std::string result;
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(pdf.data()),pdf.size(),0,result))return 1;
 const std::string expected=
  std::string("\\xe4\\xb8\\x96\\xe6\\x97\\xa5\\xe6\\x9c\\xac")+
  "ABB\\nB\\nB\\nB\\nB B\\nBB BBBB BBBBBB\\nB\\nB";
 if(result!=expected){
  size_t first=0;
  while(first<result.size()&&first<expected.size()&&
        result[first]==expected[first])++first;
  std::fprintf(stderr,
      "rinpdf-text-extract mismatch at byte %zu (actual %zu, expected %zu)\\n",
      first,result.size(),expected.size());
  const auto show=[](const char* title,const std::string& bytes){
   std::fprintf(stderr,"%s: ",title);
   for(unsigned char c:bytes){
    if(c=='\\n')std::fputs("\\\\n",stderr);
    else if(c=='\\r')std::fputs("\\\\r",stderr);
    else if(c=='\\t')std::fputs("\\\\t",stderr);
    else if(c>=32&&c<127)std::fputc(c,stderr);
    else std::fprintf(stderr,"\\\\x%02X",unsigned(c));
   }
   std::fputc('\\n',stderr);
  };
  show("expected",expected);
  show("actual",result);
  return 2;
 }
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
 // Tf requires exactly a font name and size, not preceding numeric operands.
 // Replace with the same byte count to keep /Length and xref valid.
 if(const int error=reject_boundary("BT /F1 12 Tf","BT 1/F1 2 Tf",
      47,48,49))return error;
 // An unknown Tf resource must not fall back to the previously selected font.
 // Keep the PDF byte layout unchanged while replacing F1 with F9.
 if(const int error=reject_boundary("/F1 12 Tf <41> Tj <42> Tj",
      "/F9 12 Tf <41> Tj <42> Tj",50,51,52))return error;
 // Text rendering mode (Tr) and rise (Ts) are accepted as text state.
 // They do not change the decoded Unicode text.
 const auto check_valid_state=[&](const std::string& replacement,int missing,int failed){
  const std::string needle="0 Tw <42> '";
  if(needle.size()!=replacement.size())return missing;
  std::string modified=pdf;
  const auto pos=modified.find(needle);
  if(pos==std::string::npos)return missing;
  modified.replace(pos,needle.size(),replacement);
  if(!extract_page_text(reinterpret_cast<const uint8_t*>(modified.data()),
      modified.size(),0,result))return failed;
  return result==previous?0:failed;
 };
 if(const int error=check_valid_state("3 Tr <42> '",53,54))return error;
 if(const int error=check_valid_state("4 Ts <42> '",55,56))return error;
 // Tr is an integer from 0 through 7.
 if(const int error=reject_boundary("0 Tw <42> '","9 Tr <42> '",
      57,58,59))return error;
 if(const int error=reject_boundary("0 Tw <42> '","1.5Tr<42> '",
      60,61,62))return error;
 // A standalone TJ or Tf has no valid operand sequence. These two-byte
 // substitutions preserve the content length and classic xref positions.
 if(const int error=reject_boundary("T* <42> Tj","TJ <42> Tj",
      63,64,65))return error;
 if(const int error=reject_boundary("T* <42> Tj","Tf <42> Tj",
      66,67,68))return error;
 // Non-uniform text matrices must preserve text extraction. The
 // replacement keeps /Length and all xref offsets unchanged.
 std::string scaled_baseline=pdf;
 const std::string original_tm="0 1 -1 0 50 0 Tm";
 const std::string scaled_tm="0 2 -1 0 50 0 Tm";
 static_assert(sizeof("0 1 -1 0 50 0 Tm")==sizeof("0 2 -1 0 50 0 Tm"));
 const auto tm_pos=scaled_baseline.find(original_tm);
 if(tm_pos==std::string::npos)return 69;
 scaled_baseline.replace(tm_pos,original_tm.size(),scaled_tm);
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(scaled_baseline.data()),
      scaled_baseline.size(),0,result))return 70;
 if(result!=previous)return 71;
 // Rebuild the fixture with a longer invalid Td displacement, keeping
 // /Length, xref offsets and startxref correct rather than editing bytes.
 const auto large_td_content_pos=content.find("0 -16 Td");
 if(large_td_content_pos==std::string::npos)return 72;
 std::string large_td_content=content;
 large_td_content.replace(large_td_content_pos,8,"1000000000001 0 Td");
 std::string large_td_pdf="%PDF-1.5\n";
 size_t large_offsets[8]{};
 auto large_append=[&](unsigned id,const std::string& body){
  large_offsets[id]=large_td_pdf.size();
  large_td_pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 large_append(1,"<< /Type /Catalog /Pages 2 0 R >>");
 large_append(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] /Resources << /Font << /F1 4 0 R >> /CMap << /Parent 7 0 R >> /Properties << /P1 << /ActualText <FEFF4E16> >> >> >> >>");
 large_append(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Contents 5 0 R >>");
 large_append(4,"<< /Type /Font /Subtype /TrueType /BaseFont /Example /Encoding /WinAnsiEncoding /FirstChar 65 /LastChar 66 /Widths [600 500] /ToUnicode 6 0 R >>");
 large_append(5,"<< /Length "+std::to_string(large_td_content.size())+" >>\nstream\n"+large_td_content+"\nendstream");
 large_append(6,"<< /UseCMap /Parent /CMapName /Child /Length "+std::to_string(cmap.size())+" >>\nstream\n"+cmap+"\nendstream");
 large_append(7,"<< /CMapName /Parent /Length "+std::to_string(parent_cmap.size())+" >>\nstream\n"+parent_cmap+"\nendstream");
 const size_t large_xref=large_td_pdf.size();
 large_td_pdf+="xref\n0 8\n0000000000 65535 f \n";
 for(unsigned id=1;id<=7;++id){
  char row[32];std::snprintf(row,sizeof(row),"%010zu 00000 n \n",large_offsets[id]);
  large_td_pdf+=row;
 }
 large_td_pdf+="trailer\n<< /Root 1 0 R /Size 8 >>\nstartxref\n"+
               std::to_string(large_xref)+"\n%%EOF\n";
 if(extract_page_text(reinterpret_cast<const uint8_t*>(large_td_pdf.data()),
      large_td_pdf.size(),0,result))return 73;
 if(result!=previous)return 74;
 // Same-line Td moves the next glyph beyond the previous glyph's
 // advance; infer a space without introducing a newline.
 std::string same_line_td=pdf;
 const std::string old_td="0 -16 Td";
 const std::string new_td="20  0 Td";
 static_assert(sizeof("0 -16 Td")==sizeof("20  0 Td"));
 const auto same_line_pos=same_line_td.find(old_td);
 if(same_line_pos==std::string::npos)return 75;
 same_line_td.replace(same_line_pos,old_td.size(),new_td);
 std::string spaced_expected=previous;
 const auto line_pos=spaced_expected.find("ABB\nB\nB\nB");
 if(line_pos==std::string::npos)return 76;
 spaced_expected.replace(line_pos,std::string("ABB\nB\nB\nB").size(),
                         "ABB\nB B\nB");
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(same_line_td.data()),
      same_line_td.size(),0,result))return 77;
 if(result!=spaced_expected)return 78;
 result=previous;
 // Two identity-matrix Tm positions on the same baseline can have
 // a visible gap even when no TJ displacement is present.
 std::string same_line_tm=pdf;
 const std::string td_before="0 -16 Td";
 const std::string td_after="0   0 Td";
 const std::string tm_before="1 0 0 1 0 30 Tm";
 const std::string tm_after="1 0 0 1 90 0 Tm";
 static_assert(sizeof("0 -16 Td")==sizeof("0   0 Td"));
 static_assert(sizeof("1 0 0 1 0 30 Tm")==sizeof("1 0 0 1 90 0 Tm"));
 const auto td_at=same_line_tm.find(td_before);
 const auto tm_at=same_line_tm.find(tm_before);
 if(td_at==std::string::npos||tm_at==std::string::npos)return 79;
 same_line_tm.replace(td_at,td_before.size(),td_after);
 same_line_tm.replace(tm_at,tm_before.size(),tm_after);
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(same_line_tm.data()),
      same_line_tm.size(),0,result))return 80;
 if(result.find("ABB\nBB B")==std::string::npos)return 81;
 result=previous;
 // A greater gap along the rotated vertical baseline still represents
 // a space. Keep the PDF byte length and xref positions unchanged.
 std::string rotated_gap=pdf;
 const std::string old_rotated="0 1 -1 0 50 20 Tm";
 const std::string new_rotated="0 1 -1 0 50 90 Tm";
 static_assert(sizeof("0 1 -1 0 50 20 Tm")==
               sizeof("0 1 -1 0 50 90 Tm"));
 const auto rotated_at=rotated_gap.find(old_rotated);
 if(rotated_at==std::string::npos)return 82;
 rotated_gap.replace(rotated_at,old_rotated.size(),new_rotated);
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(rotated_gap.data()),
      rotated_gap.size(),0,result))return 83;
 if(result!=previous)return 84;
 result=previous;
 // The Tm gap calculation must see the old cursor BEFORE resetting it.
 // A small forward move after a 6-point glyph should not invent a space.
 std::string short_tm_gap=pdf;
 const auto short_tm_pos=short_tm_gap.find(tm_before);
 if(short_tm_pos==std::string::npos)return 85;
 short_tm_gap.replace(short_tm_pos,tm_before.size(),"1 0 0 1 10 0 Tm");
 // Move the earlier line onto the same baseline without changing offsets.
 const auto short_td_pos=short_tm_gap.find(td_before);
 if(short_td_pos==std::string::npos)return 86;
 short_tm_gap.replace(short_td_pos,td_before.size(),td_after);
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(short_tm_gap.data()),
      short_tm_gap.size(),0,result))return 87;
 if(result.find("ABB\\nBB B")!=std::string::npos)return 88;
 result=previous;
 // Off-baseline Td moves must not be interpreted as a horizontal gap.
 // This also exercises the shared projection helper's line separation.
 std::string different_line_td=pdf;
 const std::string td_source="0 -16 Td";
 const std::string td_shift="20 -1 Td";
 static_assert(sizeof("0 -16 Td")==sizeof("20 -1 Td"));
 const auto shifted_at=different_line_td.find(td_source);
 if(shifted_at==std::string::npos)return 89;
 different_line_td.replace(shifted_at,td_source.size(),td_shift);
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(different_line_td.data()),
      different_line_td.size(),0,result))return 90;
 if(result!=previous)return 91;
 result=previous;
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
