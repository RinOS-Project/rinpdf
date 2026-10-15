#include "../src/renderer/rectangles.h"
#include <cstdint>
#include <string>
#include <vector>
int main(){
 constexpr unsigned side=1024;
 std::vector<uint8_t> pixels((size_t)side*side*4);
 const std::string prefix="0 0 1024 1024 re W n ";
 std::string valid=prefix;
 for(unsigned i=0;i<32;++i)valid+="q ";
 for(unsigned i=0;i<32;++i)valid+="Q ";
 if(!rinpdf::renderer::render_rectangles(
  reinterpret_cast<const uint8_t*>(valid.data()),valid.size(),
  side,side,pixels.data(),side*4))return 1;
 std::string excess=prefix;
 for(unsigned i=0;i<33;++i)excess+="q ";
 if(rinpdf::renderer::render_rectangles(
  reinterpret_cast<const uint8_t*>(excess.data()),excess.size(),
  side,side,pixels.data(),side*4))return 2;
 return 0;
}
