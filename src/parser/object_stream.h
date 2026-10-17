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
// Resolve a type-2 XRef entry through its type-1 /ObjStm container.
// Caller supplies validated XRef stream entries; fails closed for
// indirect /Length and unsupported compression filters.
struct XrefStreamEntry;
bool resolve_compressed_object(const uint8_t* data,size_t size,
 const std::vector<XrefStreamEntry>& xref,size_t object,Value& output);

}
