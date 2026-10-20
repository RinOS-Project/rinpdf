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
bool utf8_to_utf16(const char* input,std::string& output){
 if(!input)return true;
 const auto* p=(const unsigned char*)input;
 output.clear();output.push_back(char(0xFE));output.push_back(char(0xFF));
 size_t length=0;
 while(*p){
  if(++length>1024*1024)return false;
  uint32_t cp=0;
  unsigned count=0;
  if(*p<0x80){cp=*p++;count=0;}
  else if(*p>=0xC2&&*p<=0xDF){cp=*p++&0x1F;count=1;}
  else if(*p>=0xE0&&*p<=0xEF){cp=*p++&0x0F;count=2;}
  else if(*p>=0xF0&&*p<=0xF4){cp=*p++&0x07;count=3;}
  else return false;
  for(unsigned j=0;j<count;++j){
   if(*p<0x80||*p>0xBF)return false;
   cp=(cp<<6)|(*p++&0x3F);
  }
  if((count==1&&cp<0x80)||(count==2&&cp<0x800)||
     (count==3&&cp<0x10000)||cp>0x10FFFF||
     (cp>=0xD800&&cp<=0xDFFF))return false;
  auto unit=[&](uint16_t u){output.push_back(char(u>>8));output.push_back(char(u&255));};
  if(cp<0x10000)unit((uint16_t)cp);
  else {cp-=0x10000;unit((uint16_t)(0xD800+(cp>>10)));unit((uint16_t)(0xDC00+(cp&1023)));}
 }
 return true;
}
std::string hex_bytes(const std::string& input){
 static constexpr char hex[]="0123456789ABCDEF";
 std::string output="<";
 for(unsigned char c:input){output+=hex[c>>4];output+=hex[c&15];}
 return output+">";
}
bool encoded_metadata(const char* input,std::string& output){
 if(!input)return true;
 std::string utf16;
 if(!utf8_to_utf16(input,utf16))return false;
 output=hex_bytes(utf16);
 return true;
}
rinpdf_status write_metadata(const uint8_t* bytes,size_t length,
 const rinpdf_metadata* metadata,uint8_t** out,size_t* out_size){
 if(!out||!out_size)return RINPDF_INVALID_ARGUMENT;
 *out=nullptr;*out_size=0;
 if(!bytes||length<8||!metadata)return RINPDF_INVALID_ARGUMENT;
 // This is a bounded incremental update of the Info dictionary.
 // A new revision preserves original page, image and font objects.
 try {
  std::string title,author,subject,keywords;
  if(!encoded_metadata(metadata->title,title)||!encoded_metadata(metadata->author,author)||
     !encoded_metadata(metadata->subject,subject)||!encoded_metadata(metadata->keywords,keywords))
   return RINPDF_INVALID_ARGUMENT;
  std::vector<rinpdf::parser::XrefEntry> entries;
  size_t trailer_offset=0;
  if(!rinpdf::parser::read_classic_xref(bytes,length,entries,&trailer_offset))
   return RINPDF_UNSUPPORTED;
  size_t max_id=0;
  for(const auto& entry:entries)max_id=std::max(max_id,entry.object);
  if(max_id>=999999)return RINPDF_UNSUPPORTED;
  // /Size covers objects from earlier incremental revisions as well.
  rinpdf::parser::Lexer trailer_reader(bytes+trailer_offset,length-trailer_offset);
  rinpdf::parser::Value old_trailer;
  if(!rinpdf::parser::parse_value(trailer_reader,old_trailer)||
     old_trailer.kind!=rinpdf::parser::Value::Kind::Dictionary)
   return RINPDF_UNSUPPORTED;
  auto size_field=old_trailer.dictionary.find("Size");
  if(size_field==old_trailer.dictionary.end()||
     size_field->second.kind!=rinpdf::parser::Value::Kind::Integer||
     size_field->second.integer<1||size_field->second.integer>999999)
   return RINPDF_UNSUPPORTED;
  max_id=std::max(max_id,(size_t)size_field->second.integer-1);
  const size_t info_id=max_id+1;
  std::string output((const char*)bytes,length);
  output+="\n";
  const size_t position=output.size();
  output+=std::to_string(info_id)+" 0 obj\n<< /Producer (rinpdf)";
  if(metadata->title)output+=" /Title "+title;
  if(metadata->author)output+=" /Author "+author;
  if(metadata->subject)output+=" /Subject "+subject;
  if(metadata->keywords)output+=" /Keywords "+keywords;
  output+=" >>\nendobj\n";
  const size_t xref=output.size();
  char buffer[64];
  std::snprintf(buffer,sizeof(buffer),"%010zu 00000 n \n",position);
  output+="xref\n"+std::to_string(info_id)+" 1\n"+buffer;
  auto root=old_trailer.dictionary.find("Root");
  if(root==old_trailer.dictionary.end()||
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
