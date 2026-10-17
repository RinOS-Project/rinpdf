#include "../src/parser/xref.h"
#include "../src/parser/xref_stream.h"
#include "../src/parser/object.h"
#include "rinpdf/rinpdf.h"
#include <cstdio>
#include <vector>
#include <string>
#include <cstring>
int main(){
 const char *path="rinpdf-xref.pdf";
 if(rinpdf_write_text_file(path,"xref test")!=RINPDF_OK)return 1;
 FILE *f=std::fopen(path,"rb");if(!f)return 2;
 std::vector<unsigned char> data;unsigned char buf[4096];size_t n;
 while((n=std::fread(buf,1,sizeof(buf),f))!=0)data.insert(data.end(),buf,buf+n);
 if(std::ferror(f)){std::fclose(f);return 3;}
 std::fclose(f);std::remove(path);
 std::vector<rinpdf::parser::XrefEntry> entries;
 if(!rinpdf::parser::read_classic_xref(data.data(),data.size(),entries))return 4;
 if(entries.size()!=6||entries[0].in_use||!entries[1].in_use||entries[1].object!=1)return 5;
 size_t begin=0,end=0;
 if(!rinpdf::parser::resolve_object(data.data(),data.size(),entries[1],begin,end))return 6;
 if(begin>=end||end>data.size())return 7;
 if(rinpdf::parser::resolve_object(data.data(),data.size(),entries[0],begin,end))return 8;
 auto corrupt=entries[1];corrupt.object=999;
 if(rinpdf::parser::resolve_object(data.data(),data.size(),corrupt,begin,end))return 9;
 rinpdf_document *doc=nullptr;
 if(rinpdf_document_open_memory(data.data(),data.size(),&doc)!=RINPDF_OK)return 11;
 size_t pages=0;
 if(rinpdf_document_page_count(doc,&pages)!=RINPDF_OK||pages!=1)return 12;
 rinpdf_document_close(doc);
 std::string modified(reinterpret_cast<const char*>(data.data()),data.size());
 size_t root=modified.rfind("/Root 1 0 R");
 if(root==std::string::npos)return 13;
 modified.replace(root,11,"/Root 9 0 R");
 if(rinpdf_document_open_memory(reinterpret_cast<const unsigned char*>(modified.data()),modified.size(),&doc)!=RINPDF_OK)return 14;
 if(rinpdf_document_page_count(doc,&pages)!=RINPDF_UNSUPPORTED)return 15;
 rinpdf_document_close(doc);
 const unsigned char bad[]="%PDF-1.4\nstartxref\n999999\n%%EOF\n";
 if(rinpdf::parser::read_classic_xref(bad,sizeof(bad)-1,entries))return 10;
 // Two Classic xref revisions: the later definition wins.
 std::string incremental="%PDF-1.4\n";
 const size_t old_object=incremental.size();
 incremental+="1 0 obj\n(Old)\nendobj\n";
 const size_t old_xref=incremental.size();
 auto offset10=[](size_t offset){
  char text[32];std::snprintf(text,sizeof(text),"%010zu",offset);
  return std::string(text);
 };
 incremental+="xref\n0 2\n0000000000 65535 f \n"+
  offset10(old_object)+" 00000 n \ntrailer\n<< /Size 2 /Root 1 0 R >>\n"+
  "startxref\n"+std::to_string(old_xref)+"\n%%EOF\n";
 const size_t new_object=incremental.size();
 incremental+="1 0 obj\n(New)\nendobj\n";
 const size_t new_xref=incremental.size();
 incremental+="xref\n1 1\n"+offset10(new_object)+
  " 00000 n \ntrailer\n<< /Size 2 /Prev "+
  std::to_string(old_xref)+" >>\nstartxref\n"+
  std::to_string(new_xref)+"\n%%EOF\n";
 std::vector<rinpdf::parser::XrefStreamEntry> compressed;
 rinpdf::parser::Value latest;
 if(!rinpdf::parser::read_object_xref(
  reinterpret_cast<const uint8_t*>(incremental.data()),incremental.size(),
  entries,compressed,latest))return 16;
 if(entries.size()!=2||entries[1].offset!=new_object||
    !entries[1].in_use||!latest.dictionary.count("Root"))return 17;
 // A free entry in a newer revision must mask an older live entry.
 const size_t free_xref=incremental.size();
 incremental+="xref\n1 1\n0000000000 00001 f \ntrailer\n<< /Size 2 /Prev "+
  std::to_string(new_xref)+" >>\nstartxref\n"+
  std::to_string(free_xref)+"\n%%EOF\n";
 if(!rinpdf::parser::read_object_xref(
  reinterpret_cast<const uint8_t*>(incremental.data()),incremental.size(),
  entries,compressed,latest))return 18;
 if(entries.size()!=2||entries[1].in_use)return 19;
 // Non-backward /Prev pointers must fail instead of creating a cycle.
 const size_t bad_xref=incremental.size();
 incremental+="xref\n1 1\n"+offset10(new_object)+
  " 00000 n \ntrailer\n<< /Size 2 /Prev "+
  std::to_string(bad_xref)+" >>\nstartxref\n"+
  std::to_string(bad_xref)+"\n%%EOF\n";
 if(rinpdf::parser::read_object_xref(
  reinterpret_cast<const uint8_t*>(incremental.data()),incremental.size(),
  entries,compressed,latest))return 20;
 return 0;
}
