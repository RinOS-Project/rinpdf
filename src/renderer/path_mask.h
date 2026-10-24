#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace rinpdf::renderer {
struct MaskPoint {double x,y;};
enum class FillRule { NonZero, EvenOdd };
// Coordinates are already in top-left pixel space. Pixel-center sampling.
// Returns false for malformed geometry or excessive work.
bool rasterize_path_mask(const std::vector<std::vector<MaskPoint>>& subpaths,
 unsigned width,unsigned height,FillRule rule,std::vector<uint8_t>& mask);
// Sparse antialiased coverage; pixels are stored row-major only within
// [x,x+width) x [y,y+height). Empty bounds have an empty pixels vector.
struct CoverageRegion {
 unsigned x=0,y=0,width=0,height=0;
 std::vector<uint8_t> pixels;
};
bool rasterize_path_coverage_region_2x(
 const std::vector<std::vector<MaskPoint>>& subpaths,
 unsigned width,unsigned height,FillRule rule,CoverageRegion& region);
// 2x2 supersampled coverage in [0,255], limited to 262144 output pixels.
bool rasterize_path_coverage_2x(const std::vector<std::vector<MaskPoint>>& subpaths,
 unsigned width,unsigned height,FillRule rule,std::vector<uint8_t>& coverage);
bool intersect_path_masks(std::vector<uint8_t>& current,const std::vector<uint8_t>& incoming);
}
