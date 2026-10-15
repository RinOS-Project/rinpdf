#include "../src/parser/contents.h"
#include <cstdio>
#include <string>
#include <vector>
using rinpdf::parser::MediaBox;
static std::string make_pdf(const char* crop){
 std::string pdf="%PDF-1.4\n";
 std::vector<size_t> offsets{0};
 auto add=[&](const std::string& value){
  offsets.push_back(pdf.size());
  pdf+=std::to_string(offsets.size()-1)+" 0 obj\n"+value+"\nendobj\n";
 };
 add("<< /Type /Catalog /Pages 2 0 R >>");
 add(std::string("<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 600 800] ")+crop+" >>");
 add("<< /Type /Page /Parent 2 0 R >>");
 size_t xref=pdf.size();
 pdf+="xref\n0 4\n0000000000 65535 f \n";
 char line[32];
 for(size_t i=1;i<offsets.size();++i){
  std::snprintf(line,sizeof(line),"%010zu 00000 n \n",offsets[i]);pdf+=line;
 }
 pdf+="trailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n"+std::to_string(xref)+"\n%%EOF\n";
 return pdf;
}
int main(){
 auto pdf=make_pdf("/CropBox [100 200 500 600]");
 MediaBox box{};
 if(!rinpdf::parser::read_page_box(reinterpret_cast<const unsigned char*>(pdf.data()),pdf.size(),0,box))return 1;
 if(box.x0!=100||box.y0!=200||box.x1!=500||box.y1!=600)return 2;
 pdf=make_pdf("/CropBox [700 900 800 1000]");
 if(rinpdf::parser::read_page_box(reinterpret_cast<const unsigned char*>(pdf.data()),pdf.size(),0,box))return 3;
 pdf=make_pdf("");
 if(!rinpdf::parser::read_page_box(reinterpret_cast<const unsigned char*>(pdf.data()),pdf.size(),0,box))return 4;
 if(box.x0!=0||box.y0!=0||box.x1!=600||box.y1!=800)return 5;
 return 0;
}
