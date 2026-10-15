#include "../src/parser/xref.h"
#include "../src/parser/object.h"
#include "rinpdf/rinpdf.h"
#include <cstdio>
#include <vector>
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
 const unsigned char bad[]="%PDF-1.4\nstartxref\n999999\n%%EOF\n";
 if(rinpdf::parser::read_classic_xref(bad,sizeof(bad)-1,entries))return 10;
 return 0;
}
