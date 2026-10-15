#include "rinpdf/rinpdf.h"
#include <cstdio>
#include <cstring>
static int check(bool condition,int code){return condition?0:code;}
int main() {
 const char *path="rinpdf-smoke.pdf";
 if(rinpdf_write_text_file(path,"Hello (RinOS) \\ PDF")!=RINPDF_OK)return 1;
 rinpdf_document *doc=nullptr;
 if(rinpdf_document_open_file(path,&doc)!=RINPDF_OK)return 2;
 unsigned major=0,minor=0;
 if(rinpdf_document_version(doc,&major,&minor)!=RINPDF_OK||major!=1||minor!=4)return 3;
 size_t pages=0;
 if(rinpdf_document_page_count(doc,&pages)!=RINPDF_OK||pages!=1)return 4;
 rinpdf_document_close(doc);
 std::remove(path);
 const unsigned char header[]={'%','P','D','F','-','1','.','7'};
 if(rinpdf_document_open_memory(header,sizeof(header),&doc)!=RINPDF_OK)return 5;
 if(rinpdf_document_page_count(doc,&pages)!=RINPDF_UNSUPPORTED)return 6;
 rinpdf_document_close(doc);
 const unsigned char invalid[]={'n','o','t',' ','p','d','f','!'};
 if(rinpdf_document_open_memory(invalid,sizeof(invalid),&doc)!=RINPDF_INVALID_PDF)return 7;
 if(doc!=nullptr)return 8;
 if(rinpdf_document_open_memory(nullptr,0,&doc)!=RINPDF_INVALID_PDF)return 9;
 if(rinpdf_document_page_count(nullptr,&pages)!=RINPDF_INVALID_ARGUMENT)return 10;
 if(rinpdf_write_text_file(nullptr,"test")!=RINPDF_INVALID_ARGUMENT)return 11;
 return 0;
}
