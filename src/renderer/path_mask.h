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
bool intersect_path_masks(std::vector<uint8_t>& current,const std::vector<uint8_t>& incoming);
}
