#include "rinpdf/rinpdf.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace {
constexpr size_t max_pages=10000;
constexpr size_t max_input=64*1024*1024;
struct Page { std::vector<std::string> lines; };
bool valid_ascii(const char *s) {
 if(!s)return true;
 for(const unsigned char *p=(const unsigned char*)s;*p;++p)
  if(*p>=127 || (*p<32 && *p!='\n' && *p!='\r' && *p!='\t' && *p!='\f'))return false;
 return true;
}
std::string literal(const std::string& s) {
 std::string out="(";
 for(unsigned char c:s){
  if(c=='('||c==')'||c=='\\')out+='\\';
  out+=(char)c;
 }
 return out+")";
}
std::string info_literal(const char* s) {
 std::string v=s?s:"";
 for(char& c:v)if(c=='\n'||c=='\r'||c=='\t'||c=='\f')c=' ';
 return literal(v);
}
bool add_wrapped_line(std::vector<Page>& pages,const std::string& text,
                       size_t columns,size_t rows) {
 size_t pos=0;
 do {
  if(pages.back().lines.size()>=rows){
   if(pages.size()>=max_pages)return false;
   pages.emplace_back();
  }
  size_t take=std::min(columns,text.size()-pos);
  if(pos+take<text.size()){
   size_t space=text.rfind(' ',pos+take);
   if(space!=std::string::npos && space>pos)take=space-pos+1;
  }
  pages.back().lines.push_back(text.substr(pos,take));
  pos+=take;
 }while(pos<text.size());
 return true;
}
bool layout(const char* input,unsigned columns,unsigned rows,std::vector<Page>& pages) {
 pages.emplace_back();
 const std::string s(input);
 size_t start=0;
 while(start<s.size()){
  size_t end=s.find_first_of("\r\n\f",start);
  if(end==std::string::npos)end=s.size();
  std::string line=s.substr(start,end-start);
  for(char& c:line)if(c=='\t')c=' ';
  if(!add_wrapped_line(pages,line,columns,rows))return false;
  if(end==s.size())break;
  char sep=s[end];
  start=end+1;
  if(sep=='\r' && start<s.size() && s[start]=='\n')++start;
  if(sep=='\f') {
   if(pages.size()>=max_pages)return false;
   pages.emplace_back();
  }
 }
 return true;
}
bool append_object(std::string& pdf,std::vector<size_t>& offsets,const std::string& value) {
 offsets.push_back(pdf.size());
 pdf+=std::to_string(offsets.size()-1)+" 0 obj\n"+value+"\nendobj\n";
 return true;
}
}
extern "C" rinpdf_status rinpdf_write_text_document(const char* path,const char* ascii_text,
 const rinpdf_text_options* options) {
 if(!path||!ascii_text)return RINPDF_INVALID_ARGUMENT;
 if(!valid_ascii(ascii_text))return RINPDF_UNSUPPORTED;
 if(options && (!valid_ascii(options->title)||!valid_ascii(options->author)||
                 !valid_ascii(options->subject)))return RINPDF_UNSUPPORTED;
 try {
  const size_t len=std::strlen(ascii_text);
  if(len>max_input)return RINPDF_UNSUPPORTED;
  const unsigned width=options && options->page_width?options->page_width:595;
  const unsigned height=options && options->page_height?options->page_height:842;
  const unsigned margin=options && options->margin?options->margin:50;
  const unsigned font=options && options->font_size?options->font_size:12;
  if(width<100||height<100||width>14400||height>14400||font<4||font>144||
     margin>width/3||margin>height/3)return RINPDF_INVALID_ARGUMENT;
  const unsigned leading=font+font/3+2;
  const unsigned columns=std::max(1u,(width-2*margin)*10/(font*6));
  const unsigned rows=(height-2*margin)/leading;
  if(rows==0)return RINPDF_INVALID_ARGUMENT;
  std::vector<Page> pages;
  if(!layout(ascii_text,columns,rows,pages))return RINPDF_UNSUPPORTED;
  const size_t page_count=pages.size();
  const size_t info_id=4+page_count*2;
  const size_t object_count=info_id;
  std::string pdf="%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
  std::vector<size_t> offsets(1,0);
  append_object(pdf,offsets,"<< /Type /Catalog /Pages 2 0 R >>");
  std::string kids="[";
  for(size_t i=0;i<page_count;++i)kids+=std::to_string(4+2*i)+" 0 R ";
  kids+="]";
  append_object(pdf,offsets,"<< /Type /Pages /Kids "+kids+" /Count "+std::to_string(page_count)+" >>");
  append_object(pdf,offsets,"<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>");
  for(size_t i=0;i<page_count;++i){
   const size_t page_id=4+2*i;
   const size_t stream_id=page_id+1;
   append_object(pdf,offsets,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "+
     std::to_string(width)+" "+std::to_string(height)+
     "] /Resources << /Font << /F1 3 0 R >> >> /Contents "+
     std::to_string(stream_id)+" 0 R >>");
   std::string stream="BT\n/F1 "+std::to_string(font)+" Tf\n"+
     std::to_string(leading)+" TL\n"+
     std::to_string(margin)+" "+std::to_string(height-margin-font)+" Td\n";
   for(const auto& line:pages[i].lines)stream+=literal(line)+" Tj\nT*\n";
   stream+="ET\n";
   append_object(pdf,offsets,"<< /Length "+std::to_string(stream.size())+
     " >>\nstream\n"+stream+"endstream");
  }
  const char* title=options?options->title:nullptr;
  const char* author=options?options->author:nullptr;
  const char* subject=options?options->subject:nullptr;
  std::string info="<< /Producer (rinpdf)";
  if(title)info+=" /Title "+info_literal(title);
  if(author)info+=" /Author "+info_literal(author);
  if(subject)info+=" /Subject "+info_literal(subject);
  info+=" >>";
  append_object(pdf,offsets,info);
  const size_t xref=pdf.size();
  pdf+="xref\n0 "+std::to_string(object_count+1)+"\n0000000000 65535 f \n";
  char buf[32];
  for(size_t i=1;i<offsets.size();++i){
   std::snprintf(buf,sizeof(buf),"%010zu 00000 n \n",offsets[i]);
   pdf+=buf;
  }
  pdf+="trailer\n<< /Size "+std::to_string(object_count+1)+
    " /Root 1 0 R /Info "+std::to_string(info_id)+
    " 0 R >>\nstartxref\n"+std::to_string(xref)+"\n%%EOF\n";
  FILE* f=std::fopen(path,"wb");
  if(!f)return RINPDF_IO_ERROR;
  bool ok=std::fwrite(pdf.data(),1,pdf.size(),f)==pdf.size();
  if(std::fclose(f)!=0)ok=false;
  return ok?RINPDF_OK:RINPDF_IO_ERROR;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
