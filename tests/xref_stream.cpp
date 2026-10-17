#include "../src/parser/xref_stream.h"
#include <vector>
#include <cstdint>
#include <string>
using namespace rinpdf::parser;
int main(){
 std::vector<XrefStreamEntry> out{{99,1,4,0}};
 // W=[1,2,1], /Index=[0 3]: free, ordinary, object stream.
 const uint8_t sample[]={0,0,0,255, 1,0,42,0, 2,0,7,3};
 if(!decode_xref_stream_entries(sample,sizeof(sample),{1,2,1},{0,3},out))return 1;
 if(out.size()!=3||out[0].type!=0||out[1].field1!=42||
    out[2].type!=2||out[2].field1!=7||out[2].field2!=3)return 2;
 const uint8_t implicit[]={0,3,0,0};
 if(!decode_xref_stream_entries(implicit,sizeof(implicit),{0,3,1},{5,1},out))return 3;
 if(out.size()!=1||out[0].object!=5||out[0].type!=1||out[0].field1!=768)return 4;
 if(decode_xref_stream_entries(sample,sizeof(sample),{1,2,1},{0,4},out))return 5;
 if(decode_xref_stream_entries(sample,sizeof(sample),{1,9,1},{0,3},out))return 6;
 if(decode_xref_stream_entries(sample,sizeof(sample),{1,2,1},{2,2,3,1},out))return 7;
 if(decode_xref_stream_entries(sample,sizeof(sample),{1,2,1},{0,1000001},out))return 8;
 if(out.size()!=1||out[0].object!=5)return 9; // failed calls preserve output
 const uint8_t unsupported[]={3,0,0,0};
 if(decode_xref_stream_entries(unsupported,sizeof(unsupported),{1,2,1},{0,1},out))return 10;
 const uint8_t excessive_generation[]={1,0,0,1,0,0};
 if(decode_xref_stream_entries(excessive_generation,sizeof(excessive_generation),
                               {1,2,3},{1,1},out))return 11;
 // Read an actual indirect XRef stream with a direct /Length.
 std::string pdf="%PDF-1.5\n";
 const size_t offset=pdf.size();
 pdf+="5 0 obj\n<< /Type /XRef /Size 3 /W [1 2 1] /Length 12 >>\nstream\n";
 pdf.append(reinterpret_cast<const char*>(sample),sizeof(sample));
 pdf+="\nendstream\nendobj\n";
 Value trailer;
 if(!read_xref_stream_at(reinterpret_cast<const uint8_t*>(pdf.data()),
        pdf.size(),offset,out,trailer))return 12;
 if(out.size()!=3||out[2].type!=2||
    trailer.dictionary.find("Size")==trailer.dictionary.end())return 13;
 std::string damaged=pdf;
 damaged.replace(damaged.find("/Length 12"),10,"/Length 11");
 if(read_xref_stream_at(reinterpret_cast<const uint8_t*>(damaged.data()),
        damaged.size(),offset,out,trailer))return 14;
 return 0;
}
