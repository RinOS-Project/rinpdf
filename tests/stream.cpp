#include "../src/parser/stream.h"
#include "../src/parser/flate.h"
#include <cstring>
#include <vector>
#include <zlib.h>
using namespace rinpdf::parser;
int main(){
 const char good[]="<< /Length 6 >>\nstream\nABCDEF\nendstream";
 StreamRange range;
 if(!locate_stream(reinterpret_cast<const unsigned char*>(good),sizeof(good)-1,range))return 1;
 if(range.length!=6||std::memcmp(good+range.offset,"ABCDEF",6)!=0)return 2;
 const char bad[]="<< /Length 7 >>\nstream\nABCDEF\nendstream";
 if(locate_stream(reinterpret_cast<const unsigned char*>(bad),sizeof(bad)-1,range))return 3;
 const char indirect[]="<< /Length 2 0 R >>\nstream\nABCDEF\nendstream";
 if(locate_stream(reinterpret_cast<const unsigned char*>(indirect),sizeof(indirect)-1,range))return 4;
 std::vector<unsigned char> output;
 // Reparse the valid input because failed calls clear the range.
 if(!locate_stream(reinterpret_cast<const unsigned char*>(good),sizeof(good)-1,range))return 6;
 if(!decode_stream(reinterpret_cast<const unsigned char*>(good),sizeof(good)-1,range,output)||output.size()!=6)return 7;
 if(decode_stream(reinterpret_cast<const unsigned char*>(good),sizeof(good)-1,range,output,5))return 8;
 const unsigned char compressed[]={0x78,0x9c,0x73,0x74,0x72,0x76,0x71,0x75,0x03,0x00,0x05,0x7e,0x01,0x96};
 if(!rinpdf::parser::flate_decode(compressed,sizeof(compressed),output,1024))return 9;
 if(output.size()!=6||std::memcmp(output.data(),"ABCDEF",6)!=0)return 10;
 if(rinpdf::parser::flate_decode(compressed,sizeof(compressed),output,5))return 11;
 return 0;
}
