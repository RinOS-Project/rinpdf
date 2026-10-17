#include "../src/renderer/glyph_outline.h"
#include <cstdint>
#include <vector>
using namespace rinpdf::renderer;
int main(){
 SimpleGlyph g;
 g.contour_ends={3};
 g.points={{0,0,true},{8,0,false},{8,8,true},{0,8,true}};
 std::vector<std::vector<MaskPoint>> paths;
 if(!flatten_simple_glyph(g,4,8,48,0.25,paths))return 1;
 if(paths.size()!=1||paths[0].size()<5)return 2;
 std::vector<uint8_t> coverage;
 if(!rasterize_path_coverage_2x(paths,64,64,FillRule::NonZero,coverage))
  return 3;
 bool painted=false;
 for(const auto value:coverage)if(value){painted=true;break;}
 if(!painted)return 4;
 // Consecutive off-curve points require implicit on-curve midpoints.
 g.points={{0,0,false},{8,0,false},{8,8,true},{0,8,false}};
 if(!flatten_simple_glyph(g,4,8,48,0.25,paths))return 5;
 g.contour_ends={9};
 if(flatten_simple_glyph(g,4,8,48,0.25,paths))return 6;
 if(paths.size()!=1)return 7; // Failed calls preserve prior output
 return 0;
}
