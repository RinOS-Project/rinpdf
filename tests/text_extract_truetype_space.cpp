#include "../src/parser/text_extract.h"
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
using namespace rinpdf::parser;
static void w16(std::vector<uint8_t>& a,size_t p,uint16_t v){
 a[p]=uint8_t(v>>8);a[p+1]=uint8_t(v);
}
static void w32(std::vector<uint8_t>& a,size_t p,uint32_t v){
 a[p]=uint8_t(v>>24);a[p+1]=uint8_t(v>>16);
 a[p+2]=uint8_t(v>>8);a[p+3]=uint8_t(v);
}
int main(){
 // A small SFNT whose Unicode space glyph has a 700/1000 advance.
 std::vector<uint8_t> font(308,0);
 w32(font,0,0x00010000);w16(font,4,7);
 const uint32_t tags[]={0x68656164,0x68686561,0x686d7478,0x6d617870,
                        0x6c6f6361,0x676c7966,0x636d6170};
 const uint32_t off[]={124,178,214,222,228,234,268};
 const uint32_t len[]={54,36,8,6,6,34,40};
 for(size_t i=0;i<7;++i){
  size_t p=12+i*16;w32(font,p,tags[i]);w32(font,p+8,off[i]);
  w32(font,p+12,len[i]);
 }
 w16(font,124+18,1000);
 w16(font,178+34,2);
 w16(font,214,500);w16(font,218,700);
 w32(font,222,0x00010000);w16(font,226,2);
 w16(font,228,0);w16(font,230,0);w16(font,232,17);
 w16(font,234,1);w16(font,240,500);w16(font,242,500);
 w16(font,244,3);for(size_t i=0;i<4;++i)font[248+i]=1;
 w16(font,254,500);w16(font,258,uint16_t(-500));
 w16(font,264,500);
 w16(font,268,0);w16(font,270,1);
 w16(font,272,3);w16(font,274,10);w32(font,276,12);
 w16(font,280,12);w32(font,284,28);w32(font,292,1);
 w32(font,296,32);w32(font,300,32);w32(font,304,1);
 const std::string content="BT /F1 12 Tf [<41> -600 <42>] TJ ET";
 const std::string cmap="3 beginbfchar <20> <0020> <41> <0041> <42> <0042> endbfchar";
 std::string pdf="%PDF-1.5\n";
 size_t offsets[10]{};
 auto append=[&](unsigned id,const std::string& body){
  offsets[id]=pdf.size();
  pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 append(1,"<< /Type /Catalog /Pages 2 0 R >>");
 append(2,"<< /Type /Pages /Count 1 /Kids [3 0 R] /Resources << /Font << /F1 4 0 R >> >> >>");
 append(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Contents 5 0 R >>");
 append(4,"<< /Type /Font /Subtype /TrueType /BaseFont /Example /Encoding /WinAnsiEncoding /FontDescriptor 8 0 R /ToUnicode 6 0 R >>");
 append(5,"<< /Length "+std::to_string(content.size())+" >>\nstream\n"+content+"\nendstream");
 append(6,"<< /Length "+std::to_string(cmap.size())+" >>\nstream\n"+cmap+"\nendstream");
 append(7,"<< /Length "+std::to_string(font.size())+" >>\nstream\n"+
       std::string(reinterpret_cast<const char*>(font.data()),font.size())+
       "\nendstream");
 append(8,"<< /Type /FontDescriptor /FontName /Example /FontFile2 7 0 R >>");
 const size_t xref=pdf.size();
 pdf+="xref\n0 9\n0000000000 65535 f \n";
 for(unsigned id=1;id<=8;++id){
  char row[32];std::snprintf(row,sizeof(row),"%010zu 00000 n \n",offsets[id]);
  pdf+=row;
 }
 pdf+="trailer\n<< /Root 1 0 R /Size 9 >>\nstartxref\n"+
      std::to_string(xref)+"\n%%EOF\n";
 std::string out;
 if(!extract_page_text(reinterpret_cast<const uint8_t*>(pdf.data()),
      pdf.size(),0,out))return 1;
 // The 600/1000-em gap is narrower than this embedded font's space (700).
 if(out!="AB")return 2;
 return 0;
}
