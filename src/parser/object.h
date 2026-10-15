#pragma once
#include "xref.h"
namespace rinpdf::parser {
// Resolve an uncompressed indirect object by its exact xref entry.
// Returns the byte range of its body, excluding the obj/endobj delimiters.
bool resolve_object(const uint8_t *data,size_t size,const XrefEntry& entry,size_t& begin,size_t& end);
}
