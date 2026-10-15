#include "rinpdf/rinpdf.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
extern "C" rinpdf_status rinpdf_write_text_file(const char *path,const char *text){
 if(!path||!text)return RINPDF_INVALID_ARGUMENT;
 std::string escaped;
 try {
  for(const unsigned char *p=(const unsigned char*)text;*p;++p){
   if(*p>127 || (*p<32 && *p!='\n' && *p!='\r' && *p!='\t'))return RINPDF_UNSUPPORTED;
   if(*p=='('||*p==')'||*p=='\\')escaped+='\\';
   if(*p=='\n'||*p=='\r'||*p=='\t')escaped+=' ';
   else escaped+=(char)*p;
  }
  std::string stream="BT /F1 12 Tf 50 780 Td ("+escaped+") Tj ET\n";
  std::vector<std::string> obj={
   "<< /Type /Catalog /Pages 2 0 R >>",
   "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
   "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
   "<< /Length "+std::to_string(stream.size())+" >>\nstream\n"+stream+"endstream",
   "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
  };
  std::string pdf="%PDF-1.4\n";std::vector<size_t> offsets(1,0);
  for(size_t i=0;i<obj.size();++i){offsets.push_back(pdf.size());pdf+=std::to_string(i+1)+" 0 obj\n"+obj[i]+"\nendobj\n";}
  size_t xref=pdf.size();pdf+="xref\n0 6\n0000000000 65535 f \n";
  char line[32];for(size_t i=1;i<offsets.size();++i){std::snprintf(line,sizeof(line),"%010zu 00000 n \n",offsets[i]);pdf+=line;}
  pdf+="trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"+std::to_string(xref)+"\n%%EOF\n";
  FILE *f=std::fopen(path,"wb");if(!f)return RINPDF_IO_ERROR;
  bool ok=std::fwrite(pdf.data(),1,pdf.size(),f)==pdf.size();
  if(std::fclose(f)!=0)ok=false;return ok?RINPDF_OK:RINPDF_IO_ERROR;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
