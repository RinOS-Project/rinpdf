#include "../src/parser/text_extract.h"
#include <cstdio>
#include <string>
#include <vector>
using namespace rinpdf::parser;
static std::string make_pdf(const std::string& content,bool missing_b_width=false){
 std::string pdf="%PDF-1.5\n";
 size_t offsets[8]{};
 auto obj=[&](int id,const std::string& body){
  offsets[id]=pdf.size();
  pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 obj(1,"<< /Type /Catalog /Pages 2 0 R >>");
 obj(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] /Resources << /Font << /F1 4 0 R /F2 7 0 R >> >> >>");
 obj(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 5 0 R >>");
 obj(4,missing_b_width?
  "<< /Type /Font /Subtype /TrueType /BaseFont /Example /Encoding /WinAnsiEncoding /FirstChar 65 /LastChar 65 /Widths [600] /ToUnicode 6 0 R >>":
  "<< /Type /Font /Subtype /TrueType /BaseFont /Example /Encoding /WinAnsiEncoding /FirstChar 65 /LastChar 66 /Widths [600 600] /ToUnicode 6 0 R >>");
 obj(5,"<< /Length "+std::to_string(content.size())+" >>\nstream\n"+content+"\nendstream");
 const std::string cmap="2 beginbfchar <41> <0041> <42> <0042> endbfchar";
 obj(6,"<< /Length "+std::to_string(cmap.size())+" >>\nstream\n"+cmap+"\nendstream");
 // F2 has its own PDF widths: A=300, B=800, unlike F1 (600 each).
 obj(7,"<< /Type /Font /Subtype /TrueType /BaseFont /Example2 /Encoding /WinAnsiEncoding /FirstChar 65 /LastChar 66 /Widths [300 800] /ToUnicode 6 0 R >>");
 const size_t xref=pdf.size();
 pdf+="xref\n0 8\n0000000000 65535 f \n";
 for(int i=1;i<=7;i++){
  char line[32];std::snprintf(line,sizeof(line),"%010zu 00000 n \n",offsets[i]);
  pdf+=line;
 }
 pdf+="trailer\n<< /Size 8 /Root 1 0 R >>\nstartxref\n"+
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
           "BT /F1 10 Tf 1 0 0 1 20 15 Tm <42> Tj ET","A\nB"))return 3;
 if(!check("BT /F1 10 Tf 0 1 -1 0 0 0 Tm <41> Tj ET "
           "BT /F1 10 Tf 0 1 -1 0 15 20 Tm <42> Tj ET","A\nB"))return 4;
 if(!check("BT /F1 10 Tf 0 1 -1 0 0 0 Tm <41> Tj ET "
           "BT /F1 10 Tf 0 1 -1 0 0 20 Tm <42> Tj ET","A B"))return 5;
 // A small baseline offset is positioning jitter, not a new line.
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm <41> Tj ET "
           "BT /F1 10 Tf 1 0 0 1 8 0.5 Tm <42> Tj ET","AB"))return 6;
 // With ten-point text, a two-point baseline move exceeds tolerance.
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm <41> Tj ET "
           "BT /F1 10 Tf 1 0 0 1 8 2 Tm <42> Tj ET","A\nB"))return 7;
 // An empty BT/ET must not destroy the last glyph position.
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm <41> Tj ET "
           "BT ET "
           "BT /F1 10 Tf 1 0 0 1 20 0 Tm <42> Tj ET","A B"))return 8;
 // Empty objects must also preserve a genuine line break.
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm <41> Tj ET "
           "BT 1 0 0 1 99 99 Tm ET "
           "BT /F1 10 Tf 1 0 0 1 20 15 Tm <42> Tj ET","A\nB"))return 9;
 // Widthless B must invalidate A's earlier endpoint. Without this
 // guard, the final A may receive a bogus space based on stale geometry.
 const std::string unknown_pdf=make_pdf(
   "BT /F1 10 Tf 1 0 0 1 0 0 Tm <41> Tj ET "
   "BT /F1 10 Tf 1 0 0 1 20 0 Tm <42> Tj ET "
   "BT /F1 10 Tf 1 0 0 1 40 0 Tm <41> Tj ET",true);
 std::string unknown_result;
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(unknown_pdf.data()),
      unknown_pdf.size(),0,unknown_result))return 10;
 if(unknown_result!="A BA")return 11;
 // A positioning operator after an unknown-width glyph resets the
 // current run state but must not make the text object appear empty.
 const std::string reset_pdf=make_pdf(
   "BT /F1 10 Tf 1 0 0 1 0 0 Tm <41> Tj ET "
   "BT /F1 10 Tf 1 0 0 1 20 0 Tm <42> Tj "
   "1 0 0 1 25 0 Tm ET "
   "BT /F1 10 Tf 1 0 0 1 40 0 Tm <41> Tj ET",true);
 std::string reset_result;
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(reset_pdf.data()),
      reset_pdf.size(),0,reset_result))return 12;
 if(reset_result!="A BA")return 13;
 // Position-only Tm commands before the first glyph should not create
 // a newline, even if the final baseline has sub-glyph positioning jitter.
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm <41> Tj ET "
           "BT /F1 10 Tf 1 0 0 1 8 0 Tm "
           "1 0 0 1 8 0.5 Tm <42> Tj ET","AB"))return 14;
 // Two individually permitted Td displacements must fail once their
 // accumulated origin exceeds the supported coordinate budget.
 const std::string overflowing=make_pdf(
   "BT /F1 10 Tf 600000000000 0 Td 600000000000 0 Td <41> Tj ET");
 std::string retained="unchanged";
 if(extract_page_text(reinterpret_cast<const uint8_t*>(overflowing.data()),
      overflowing.size(),0,retained))return 15;
 if(retained!="unchanged")return 16;
 // Changing fonts within a text object must retain glyph advances and
 // classify TJ spacing using the position before the following glyph.
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm <41> Tj "
           "/F2 20 Tf [ -600 <42>] TJ ET","A B"))return 17;
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm <41> Tj "
           "/F2 20 Tf [ -300 <42>] TJ ET","AB"))return 18;
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm <41> Tj "
           "/F2 20 Tf [ -600 <42>] TJ <41> Tj ET","A BA"))return 19;
 // Cursor advance must use F2's actual widths (B=800 at size 10).
 // F1 A advances 6, F2 B advances 8, so the next absolute origin
 // at x=20 leaves a 6-unit gap; x=17 leaves only a 3-unit gap.
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm <41> Tj "
           "/F2 10 Tf <42> Tj 1 0 0 1 20 0 Tm <41> Tj ET",
           "AB A"))return 20;
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm <41> Tj "
           "/F2 10 Tf <42> Tj 1 0 0 1 17 0 Tm <41> Tj ET",
           "ABA"))return 21;
 // A TJ gap is expressed in the font size active when TJ executes.
 // Switching to a larger font must not turn a six-unit gap into twelve.
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm "
           "[<41> -600] TJ /F2 20 Tf <42> Tj ET","AB"))return 22;
 // A twelve-unit gap remains large enough after the font size change.
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm "
           "[<41> -1200] TJ /F2 20 Tf <42> Tj ET","A B"))return 23;
 // A larger initial font preserves its gap on a smaller next font.
 if(!check("BT /F1 20 Tf 1 0 0 1 0 0 Tm "
           "[<41> -600] TJ /F2 10 Tf <42> Tj ET","A B"))return 24;
 // TJ displacements must retain their physical length across Tz changes.
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm "
           "[<41> -600] TJ 200 Tz <42> Tj ET","AB"))return 25;
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm "
           "[<41> -1200] TJ 200 Tz <42> Tj ET","A B"))return 26;
 if(!check("BT /F1 10 Tf 1 0 0 1 0 0 Tm "
           "[<41> -600] TJ 50 Tz <42> Tj ET","A B"))return 27;
 return 0;
}
