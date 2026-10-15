#pragma once
#include <cstddef>
#include <cstdint>
namespace rinpdf::parser {
// Classic xref only, unencrypted documents, uncompressed page-tree dictionaries.
bool count_page_tree(const uint8_t *data,size_t size,size_t& count);
}
