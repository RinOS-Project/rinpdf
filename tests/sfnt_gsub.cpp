#include "../src/renderer/sfnt_gsub.h"
#include <vector>
using namespace rinpdf::renderer;
static void w16(std::vector<uint8_t>& b,unsigned p,unsigned v){
 b[p]=static_cast<uint8_t>(v>>8);b[p+1]=static_cast<uint8_t>(v);
}
static void w32(std::vector<uint8_t>& b,unsigned p,uint32_t v){
 b[p]=uint8_t(v>>24);b[p+1]=uint8_t(v>>16);
 b[p+2]=uint8_t(v>>8);b[p+3]=uint8_t(v);
}
int main(){
 std::vector<uint8_t> d(50);
 w16(d,0,1);w16(d,2,0);w16(d,6,10);w16(d,8,24);
 w16(d,10,1);w32(d,12,0x76727432);w16(d,16,8); // vrt2
 w16(d,18,0);w16(d,20,1);w16(d,22,0);
 w16(d,24,1);w16(d,26,4);
 w16(d,28,1);w16(d,30,0);w16(d,32,1);w16(d,34,8);
 w16(d,36,2);w16(d,38,8);w16(d,40,1);w16(d,42,2);
 w16(d,44,1);w16(d,46,1);w16(d,48,1);
 SfntFont f{d.data(),d.size(),4,{{0x47535542,0,50}}};
 uint16_t result=0;
 if(!sfnt_vertical_substitution(f,1,result)||result!=2)return 1;
 if(!sfnt_vertical_substitution(f,3,result)||result!=3)return 2;
 d[43]=5; // invalid substitution exceeding maxp
 if(sfnt_vertical_substitution(f,1,result))return 3;
 d[43]=2;
 d[39]=0xff; // coverage points outside GSUB bounds
 if(sfnt_vertical_substitution(f,1,result))return 4;
 SfntFont no_gsub{d.data(),d.size(),4,{}};
 if(!sfnt_vertical_substitution(no_gsub,1,result)||result!=1)return 5;
 return 0;
}
