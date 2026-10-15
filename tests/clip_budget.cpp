#include "../src/renderer/rectangles.h"
#include <cstdint>
#include <string>
#include <vector>
int main(){
 constexpr unsigned side=1024;
 std::vector<uint8_t> pixels((size_t)side*side*4);
 const std::string prefix="0 0 1024 1024 re W n ";
 std::string valid=prefix;
 for(unsigned i=0;i<64;++i)valid+="q ";
 for(unsigned i=0;i<64;++i)valid+="Q ";
 if(!rinpdf::renderer::render_rectangles(
  reinterpret_cast<const uint8_t*>(valid.data()),valid.size(),
  side,side,pixels.data(),side*4))return 1;
 std::string excess=prefix;
 for(unsigned i=0;i<65;++i)excess+="q ";
 if(rinpdf::renderer::render_rectangles(
  reinterpret_cast<const uint8_t*>(excess.data()),excess.size(),
  side,side,pixels.data(),side*4))return 2;
 const std::string cow="q 0 0 1024 1024 re W n q 0 0 512 512 re W n Q 0 g 700 700 50 50 re f Q";
 if(!rinpdf::renderer::render_rectangles(
  reinterpret_cast<const uint8_t*>(cow.data()),cow.size(),
  side,side,pixels.data(),side*4))return 3;
 const size_t pixel=((size_t)300*side+725)*4;
 if(pixels[pixel]!=0)return 4;
 return 0;
}
