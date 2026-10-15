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
 std::vector<uint8_t> first(64,255),second(64,0);
 second[10]=255;
 if(!intersect_path_masks(first,second)||first[10]!=255||first[11]!=0)return 5;
 if(intersect_path_masks(first,std::vector<uint8_t>(5,255)))return 6;
 std::vector<uint8_t> half{128},quarter{128};
 if(!intersect_path_masks(half,quarter)||half[0]!=64)return 12;
 paths={{{0,0},{8,0},{8,8},{0,8}}};
 if(!rasterize_path_mask(paths,8,8,FillRule::NonZero,mask)||mask[63]!=1)return 7;
 paths.clear();
 if(!rasterize_path_mask(paths,8,8,FillRule::NonZero,mask)||mask.size()!=64)return 8;
 for(auto v:mask)if(v!=0)return 9;
 paths={{{4,4}}};
 if(!rasterize_path_mask(paths,8,8,FillRule::EvenOdd,mask))return 10;
 for(auto v:mask)if(v!=0)return 11;
 std::vector<uint8_t> coverage;
 paths={{{0,0},{1.5,0},{1.5,2},{0,2}}};
 if(!rasterize_path_coverage_2x(paths,2,2,FillRule::NonZero,coverage))return 12;
 if(coverage.size()!=4||coverage[0]!=255||coverage[1]!=128||coverage[2]!=255||coverage[3]!=128)return 13;
 if(rasterize_path_coverage_2x(paths,1024,1024,FillRule::NonZero,coverage))return 14;
 std::vector<std::vector<MaskPoint>> too_many_paths(4097,{{0,0}});
 if(rasterize_path_mask(too_many_paths,8,8,FillRule::NonZero,mask))return 15;
 std::vector<std::vector<MaskPoint>> too_many_vertices(17,std::vector<MaskPoint>(4096,{0,0}));
 if(rasterize_path_mask(too_many_vertices,8,8,FillRule::EvenOdd,mask))return 16;
 std::vector<std::vector<MaskPoint>> max_vertices(16,std::vector<MaskPoint>(4096,{0,0}));
 if(!rasterize_path_mask(max_vertices,8,8,FillRule::NonZero,mask))return 17;
 for(auto value:mask)if(value!=0)return 18;
 return 0;
}
