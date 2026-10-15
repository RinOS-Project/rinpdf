#include "../src/parser/stream.h"
#include <cstring>
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
 return 0;
}
