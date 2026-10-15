#include "rinpdf/rinpdf.h"
#include <cstdio>
int main() {
 if (rinpdf_write_text_file("sample.pdf", "Hello RinOS") != RINPDF_OK) return 1;
 rinpdf_document *doc = nullptr;
 if (rinpdf_document_open_file("sample.pdf", &doc) != RINPDF_OK) return 2;
 unsigned major=0, minor=0;
 rinpdf_status s=rinpdf_document_version(doc,&major,&minor);
 rinpdf_document_close(doc);
 std::remove("sample.pdf");
 return s==RINPDF_OK && major==1 && minor==4 ? 0 : 3;
}
