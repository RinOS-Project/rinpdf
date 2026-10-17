#include "rinpdf/rinpdf.h"
#include <charconv>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

static bool number(std::string_view input,size_t& result){
 if(input.empty())return false;
 const auto parsed=std::from_chars(input.data(),input.data()+input.size(),result);
 return parsed.ec==std::errc{}&&parsed.ptr==input.data()+input.size();
}
static bool expectation(std::string_view name,rinpdf_status actual){
 if(name=="ok")return actual==RINPDF_OK;
 if(name=="unsupported")return actual==RINPDF_UNSUPPORTED;
 if(name=="invalid_pdf")return actual==RINPDF_INVALID_PDF;
 if(name=="io_error")return actual==RINPDF_IO_ERROR;
 return false;
}
int main(int argc,char** argv){
 if(argc!=2){
  std::cerr<<"Usage: rinpdf-corpus /path/to/manifest.tsv\n";
  return 2;
 }
 std::ifstream manifest(argv[1]);
 if(!manifest){std::cerr<<"Cannot open manifest\n";return 2;}
 size_t checked=0,failed=0;
 std::string line;
 for(size_t line_no=1;std::getline(manifest,line);++line_no){
  if(line.empty()||line[0]=='#')continue;
  std::vector<std::string> fields;
  size_t first=0;
  while(true){
   const size_t end=line.find('\t',first);
   fields.push_back(line.substr(first,end==std::string::npos?end:end-first));
   if(end==std::string::npos)break;
   first=end+1;
  }
  if(fields.size()!=4||fields[0].empty()||
     fields[0][0]=='-'||fields[1]!="ok"&&fields[1]!="invalid_pdf"&&
     fields[1]!="io_error"||
     fields[2]!="ok"&&fields[2]!="unsupported"&&fields[2]!="skip"||
     fields[3]!="ok"&&fields[3]!="unsupported"&&fields[3]!="skip"){
   std::cerr<<"Invalid manifest row "<<line_no<<"\n";return 2;
  }
  ++checked;
  rinpdf_document* doc=nullptr;
  const auto open=rinpdf_document_open_file(fields[0].c_str(),&doc);
  bool pass=expectation(fields[1],open);
  if(open==RINPDF_OK&&doc){
   size_t count=0;
   if(fields[2]!="skip"){
    const auto status=rinpdf_document_page_count(doc,&count);
    pass=pass&&expectation(fields[2],status);
    if(status!=RINPDF_OK)count=0;
   }
   if(fields[3]!="skip"){
    if(fields[2]!="ok"||count==0)pass=false;
    else{
     std::vector<unsigned char> rgba(64*64*4,0xa5);
     const auto status=rinpdf_render_page(doc,0,64,64,rgba.data(),64*4);
     pass=pass&&expectation(fields[3],status);
     if(status==RINPDF_UNSUPPORTED){
      for(unsigned char c:rgba)if(c!=0xa5){pass=false;break;}
     }
    }
   }
  }else if(doc){pass=false;}
  rinpdf_document_close(doc);
  std::cout<<(pass?"PASS":"FAIL")<<" line="<<line_no
           <<" path="<<fields[0]<<" open="<<rinpdf_status_string(open)<<"\n";
  if(!pass)++failed;
 }
 if(!manifest.eof()&&manifest.fail()){std::cerr<<"Manifest read failure\n";return 2;}
 if(!checked){std::cerr<<"No PDF cases listed; refusing an empty PASS\n";return 2;}
 std::cout<<"Corpus: "<<checked-failed<<"/"<<checked<<" PASS\n";
 return failed?1:0;
}
