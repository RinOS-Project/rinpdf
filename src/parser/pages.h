#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "xref.h"
#include "xref_stream.h"
#include "value.h"
namespace rinpdf::parser {
// Classic xref only, unencrypted documents, uncompressed page-tree dictionaries.
bool count_page_tree(const uint8_t *data,size_t size,size_t& count);
// Reuse a previously parsed cross-reference table and trailer.
bool count_page_tree_indexed(const uint8_t* data,size_t size,
 const std::vector<XrefEntry>& entries,
 const std::vector<XrefStreamEntry>& compressed,
 const Value& trailer,size_t& count);
}
