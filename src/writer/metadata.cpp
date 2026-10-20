#include "rinpdf/rinpdf.h"
#include "../parser/xref.h"
#include "../parser/lexer.h"
#include "../parser/value.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {
bool valid(const char* s){
 if(!s)return true;
 for(const unsigned char* p=(const unsigned char*)s;*p;++p)
  if(*p<32 && *p!='\n' && *p!='\r' && *p!='\t')return false;
 return true;
}
std::string hex_string(const char* s){
 static const char* hex="0123456789ABCDEF";
 std::string result="<";
 for(const unsigned char* p=(const unsigned char*)s;*p;++p){
  result+=hex[*p>>4];result+=hex[*p&15];
 }
 return result+">";
}
rinpdf_status write_metadata(const uint8_t* bytes,size_t length,
 const rinpdf_metadata* metadata,uint8_t** out,size_t* out_size){
 if(!out||!out_size)return RINPDF_INVALID_ARGUMENT;
 *out=nullptr;*out_size=0;
 if(!bytes||length<8||!metadata||!valid(metadata->title)||
    !valid(metadata->author)||!valid(metadata->subject)||
    !valid(metadata->keywords))return RINPDF_INVALID_ARGUMENT;
 // This is a bounded incremental update of the Info dictionary.
 // A new revision preserves original page, image and font objects.
 try {
  std::vector<rinpdf::parser::XrefEntry> entries;
  size_t trailer_offset=0;
  if(!rinpdf::parser::read_classic_xref(bytes,length,entries,&trailer_offset))
   return RINPDF_UNSUPPORTED;
  size_t max_id=0;
  for(const auto& entry:entries)max_id=std::max(max_id,entry.object);
  if(max_id>=999999)return RINPDF_UNSUPPORTED;
  const size_t info_id=max_id+1;
  std::string output((const char*)bytes,length);
  output+="\n";
  const size_t position=output.size();
  output+=std::to_string(info_id)+" 0 obj\n<< /Producer (rinpdf)";
  if(metadata->title)output+=" /Title "+hex_string(metadata->title);
  if(metadata->author)output+=" /Author "+hex_string(metadata->author);
  if(metadata->subject)output+=" /Subject "+hex_string(metadata->subject);
  if(metadata->keywords)output+=" /Keywords "+hex_string(metadata->keywords);
  output+=" >>\nendobj\n";
  const size_t xref=output.size();
  char buffer[64];
  std::snprintf(buffer,sizeof(buffer),"%010zu 00000 n \n",position);
  output+="xref\n"+std::to_string(info_id)+" 1\n"+buffer;
  // Retain the latest catalog reference by copying the trailer dictionary
  // excluding its closing >> and overriding /Info in a later update.
  // We instead resolve the latest /Root directly from parsed trailer syntax.
  rinpdf::parser::Lexer lexer(bytes+trailer_offset,length-trailer_offset);
  rinpdf::parser::Value trailer;
  if(!rinpdf::parser::parse_value(lexer,trailer)||
     trailer.kind!=rinpdf::parser::Value::Kind::Dictionary)
   return RINPDF_UNSUPPORTED;
  auto root=trailer.dictionary.find("Root");
  if(root==trailer.dictionary.end()||
     root->second.kind!=rinpdf::parser::Value::Kind::Reference)
   return RINPDF_UNSUPPORTED;
  size_t prev=0;
  if(!rinpdf::parser::find_startxref(bytes,length,prev))return RINPDF_UNSUPPORTED;
  output+="trailer\n<< /Size "+std::to_string(info_id+1)+
    " /Root "+std::to_string(root->second.integer)+" "+
    std::to_string(root->second.generation)+" R /Info "+
    std::to_string(info_id)+" 0 R /Prev "+std::to_string(prev)+
    " >>\nstartxref\n"+std::to_string(xref)+"\n%%EOF\n";
  uint8_t* allocated=(uint8_t*)std::malloc(output.size());
  if(!allocated)return RINPDF_OUT_OF_MEMORY;
  std::memcpy(allocated,output.data(),output.size());
  *out=allocated;*out_size=output.size();
  return RINPDF_OK;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
}
extern "C" rinpdf_status rinpdf_metadata_update_memory(
 const uint8_t* pdf,size_t pdf_size,const rinpdf_metadata* metadata,
 uint8_t** output,size_t* output_size){
 return write_metadata(pdf,pdf_size,metadata,output,output_size);
}
extern "C" rinpdf_status rinpdf_metadata_update_file(
 const char* output_path,const uint8_t* pdf,size_t pdf_size,
 const rinpdf_metadata* metadata){
 if(!output_path)return RINPDF_INVALID_ARGUMENT;
 uint8_t* result=nullptr;size_t size=0;
 auto status=write_metadata(pdf,pdf_size,metadata,&result,&size);
 if(status!=RINPDF_OK)return status;
 FILE* f=std::fopen(output_path,"wb");
 if(!f){std::free(result);return RINPDF_IO_ERROR;}
 bool ok=std::fwrite(result,1,size,f)==size;
 if(std::fclose(f)!=0)ok=false;
 std::free(result);
 return ok?RINPDF_OK:RINPDF_IO_ERROR;
}
