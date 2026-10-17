#include "rinpdf/rinpdf.h"
#include <string>
#include <vector>
#include <cstdint>
int main(){
 std::string pdf="%PDF-1.5\n";
 std::vector<size_t> offset(6);
 auto add=[&](unsigned id,const std::string& body){
  offset[id]=pdf.size();
  pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 add(2,"<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
 add(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 20 20] >>");
 const std::string packed="1 0 << /Type /Catalog /Pages 2 0 R >>";
 offset[4]=pdf.size();
 pdf+="4 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Length "+
      std::to_string(packed.size())+" >>\nstream\n";
 pdf+=packed;
 pdf+="\nendstream\nendobj\n";
 offset[5]=pdf.size();
 std::string bytes;
 auto xref=[&](unsigned type,size_t a,unsigned b){
  bytes.push_back(static_cast<char>(type));
  bytes.push_back(static_cast<char>((a>>16)&255));
  bytes.push_back(static_cast<char>((a>>8)&255));
  bytes.push_back(static_cast<char>(a&255));
  bytes.push_back(static_cast<char>(b));
 };
 xref(0,0,255);
 xref(2,4,0);
 for(unsigned i=2;i<=5;++i)xref(1,offset[i],0);
 pdf+="5 0 obj\n<< /Type /XRef /Root 1 0 R /Size 6 /W [1 3 1] /Length 30 >>\nstream\n";
 pdf+=bytes;
 pdf+="\nendstream\nendobj\nstartxref\n"+
      std::to_string(offset[5])+"\n%%EOF\n";
 rinpdf_document* doc=nullptr;
 if(rinpdf_document_open_memory(reinterpret_cast<const uint8_t*>(pdf.data()),
       pdf.size(),&doc)!=RINPDF_OK)return 1;
 size_t count=0;
 const auto result=rinpdf_document_page_count(doc,&count);
 rinpdf_document_close(doc);
 if(result!=RINPDF_OK||count!=1)return 2;
 // Hybrid PDF: the appended Classic xref refers to the preceding
 // XRef stream containing compressed Catalog object 1.
 const size_t classic_position=pdf.size();
 auto width10=[](size_t v){
  std::string text=std::to_string(v);
  return std::string(10-text.size(),'0')+text;
 };
 pdf+="xref\n0 1\n0000000000 65535 f \n2 4\n";
 for(unsigned id=2;id<=5;++id)
  pdf+=width10(offset[id])+" 00000 n \n";
 pdf+="trailer\n<< /Size 6 /Root 1 0 R /XRefStm "+
      std::to_string(offset[5])+" >>\nstartxref\n"+
      std::to_string(classic_position)+"\n%%EOF\n";
 if(rinpdf_document_open_memory(reinterpret_cast<const uint8_t*>(pdf.data()),
       pdf.size(),&doc)!=RINPDF_OK)return 3;
 count=0;
 const auto hybrid_status=rinpdf_document_page_count(doc,&count);
 rinpdf_document_close(doc);
 if(hybrid_status!=RINPDF_OK||count!=1)return 4;
 return 0;
}
