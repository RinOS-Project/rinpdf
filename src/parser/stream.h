#pragma once
#include "value.h"
#include <cstddef>
#include <cstdint>
#include <vector>
namespace rinpdf::parser {
struct StreamRange { Value dictionary; size_t offset=0; size_t length=0; };
bool decode_stream(const uint8_t *data,size_t size,const StreamRange& range,std::vector<uint8_t>& output,size_t max_output=64*1024*1024);
bool locate_stream(const uint8_t *data,size_t size,StreamRange& out);
}
