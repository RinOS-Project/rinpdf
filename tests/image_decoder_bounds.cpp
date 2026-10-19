#include "../src/parser/ccitt_image.h"
#include "../src/parser/dct_image.h"
#include <cstdint>
#include <cstdio>
#include <vector>

int main(){
 const uint8_t malformed[]={0xff,0x00,0xff,0x00};
 std::vector<uint8_t> pixels(3,123);
 const auto rejected=[&](bool success,const char* name)->bool{
  if(success||!pixels.empty()){
   std::fprintf(stderr,"invalid image accepted or output retained: %s\n",name);
   return false;
  }
  return true;
 };
 if(!rejected(rinpdf::parser::decode_ccitt_image(
      nullptr,0,10,10,0,false,false,pixels),"null CCITT"))return 1;
 pixels={1};
 if(!rejected(rinpdf::parser::decode_ccitt_image(
      malformed,sizeof(malformed),4097,1,0,false,false,pixels),"CCITT width"))return 1;
 pixels={1};
 if(!rejected(rinpdf::parser::decode_ccitt_image(
      malformed,sizeof(malformed),1,1,2,false,false,pixels),"CCITT K"))return 1;
 pixels={1};
 if(!rejected(rinpdf::parser::decode_dct_image(
      malformed,sizeof(malformed),1,1,4,pixels),"unsupported JPEG channels"))return 1;
 pixels={1};
 if(!rejected(rinpdf::parser::decode_dct_image(
      malformed,sizeof(malformed),4097,1,3,pixels),"JPEG width"))return 1;
 pixels={1};
 if(!rejected(rinpdf::parser::decode_dct_image(
      nullptr,0,1,1,3,pixels),"null JPEG"))return 1;
 return 0;
}
