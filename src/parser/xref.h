#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace rinpdf::parser {
struct XrefEntry { size_t object; size_t offset; unsigned generation; bool in_use; };
// Locate the last startxref pointer; does not resolve incremental /Prev chains.
bool find_startxref(const uint8_t *data,size_t size,size_t& offset);
/* Classic xref only; trailer_offset points to the opening << token. */
bool read_classic_xref(const uint8_t *data,size_t size,std::vector<XrefEntry>& entries,size_t *trailer_offset=nullptr);
}
