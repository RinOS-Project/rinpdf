#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace rinpdf::parser {
// Decode a zlib-wrapped FlateDecode stream. Output is cleared on failure.
bool flate_decode(const uint8_t *data,size_t size,std::vector<uint8_t>& output,size_t max_output=64*1024*1024);
}
