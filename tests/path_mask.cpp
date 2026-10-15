#include "../src/renderer/path_mask.h"
#include <vector>
using namespace rinpdf::renderer;
int main(){
 std::vector<std::vector<MaskPoint>> paths={
  {{0,0},{8,0},{8,8},{0,8}},
  {{2,2},{6,2},{6,6},{2,6}}
 };
 std::vector<uint8_t> mask;
 if(!rasterize_path_mask(paths,8,8,FillRule::EvenOdd,mask))return 1;
 if(mask[0]!=1||mask[3*8+3]!=0)return 2;
 if(!rasterize_path_mask(paths,8,8,FillRule::NonZero,mask))return 3;
 if(mask[3*8+3]!=1)return 4;
 std::vector<uint8_t> first(64,1),second(64,0);
 second[10]=1;
 if(!intersect_path_masks(first,second)||first[10]!=1||first[11]!=0)return 5;
 if(intersect_path_masks(first,std::vector<uint8_t>(5,1)))return 6;
 paths={{{0,0},{8,0},{8,8},{0,8}}};
 if(!rasterize_path_mask(paths,8,8,FillRule::NonZero,mask)||mask[63]!=1)return 7;
 return 0;
}
