#pragma once
#include "value.h"
#include <cstddef>
#include <cstdint>
#include <vector>
namespace rinpdf::parser {
struct ObjectStreamItem {
 size_t object=0;
 Value value;
};
// Operates on an already decoded /ObjStm payload. Checks /N and /First,
// object-number/offset pairs, ranges, duplicates and parsed boundaries.
// This is a pure decoder, not yet connected to page reference resolution.
bool decode_object_stream(const uint8_t* payload,size_t length,
 size_t count,size_t first,std::vector<ObjectStreamItem>& output);
}
