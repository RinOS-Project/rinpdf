#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace rinpdf::parser {
bool decode_ccitt_image(const uint8_t* data,size_t length,
 unsigned width,unsigned height,int k,bool byte_align,bool black_is_one,
 std::vector<uint8_t>& grayscale);
}
