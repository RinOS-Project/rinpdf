#include "rinpdf/rinpdf.h"
#include <cstdio>
#include <string>
#include <vector>
static std::string pdf(int rotation){
 std::string d="%PDF-1.4\n";std::vector<size_t> offsets{0};
 auto add=[&](const std::string& value){
  offsets.push_back(d.size());d+=std::to_string(offsets.size()-1)+" 0 obj\n"+value+"\nendobj\n";
 };
 add("<< /Type /Catalog /Pages 2 0 R >>");
 add("<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 4 4] /Rotate "+std::to_string(rotation)+" >>");
 add("<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>");
 const std::string content="0 g 0 0 2 2 re f\n";
 add("<< /Length "+std::to_string(content.size())+" >>\nstream\n"+content+"endstream");
 size_t xref=d.size();d+="xref\n0 5\n0000000000 65535 f \n";
 char line[32];for(size_t i=1;i<offsets.size();++i){std::snprintf(line,sizeof(line),"%010zu 00000 n \n",offsets[i]);d+=line;}
 d+="trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n"+std::to_string(xref)+"\n%%EOF\n";
 return d;
}
int main(){
 for(int rotation: {0,90,180,270}){
  auto bytes=pdf(rotation);rinpdf_document *doc=nullptr;
  if(rinpdf_document_open_memory((const unsigned char*)bytes.data(),bytes.size(),&doc)!=RINPDF_OK)return 1;
  std::vector<unsigned char> rgba(4*4*4);
  if(rinpdf_render_page(doc,0,4,4,rgba.data(),16)!=RINPDF_OK)return 2;
  auto at=[&](int x,int y){return rgba[(y*4+x)*4];};
  int x=rotation==0||rotation==90?0:3;
  int y=rotation==0||rotation==270?3:0;
  if(at(x,y)!=0||at(3-x,3-y)!=255)return 3;
  rinpdf_document_close(doc);
 }
 return 0;
}
