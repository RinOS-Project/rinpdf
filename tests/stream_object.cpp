#include "../src/parser/stream_object.h"
#include "../src/parser/stream.h"
#include <cstring>
#include <string>
#include <vector>
using namespace rinpdf::parser;
int main(){
 std::string pdf="%PDF-1.4\n";
 size_t stream_offset=pdf.size();
 pdf+="1 0 obj\n<< /Length 2 0 R >>\nstream\nABCDEF\nendstream\nendobj\n";
 size_t length_offset=pdf.size();
 pdf+="2 0 obj\n6\nendobj\n";
 std::vector<XrefEntry> xref={{1,stream_offset,0,true},{2,length_offset,0,true}};
 StreamRange range;
 if(!locate_indirect_stream(reinterpret_cast<const unsigned char*>(pdf.data()),pdf.size(),xref[0],xref,range))return 1;
 if(range.length!=6||std::memcmp(pdf.data()+range.offset,"ABCDEF",6)!=0)return 2;
 std::vector<unsigned char> bytes;
 if(!decode_stream(reinterpret_cast<const unsigned char*>(pdf.data()),pdf.size(),range,bytes)||bytes.size()!=6)return 3;
 xref[1].offset=pdf.size()+10;
 if(locate_indirect_stream(reinterpret_cast<const unsigned char*>(pdf.data()),pdf.size(),xref[0],xref,range))return 4;
 return 0;
}
