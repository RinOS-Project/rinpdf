#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace rinpdf::parser {
// Decode a baseline/progressive JPEG image to 8-bit Gray/RGB/CMYK.
// Four-channel output retains the JPEG CMYK sample representation; callers
// must apply PDF Decode arrays and color-space conversion separately.
// The caller validates PDF Image XObject color space and dimensions.
bool decode_dct_image(const uint8_t* bytes,size_t length,
 unsigned width,unsigned height,unsigned channels,
 std::vector<uint8_t>& pixels);
}
