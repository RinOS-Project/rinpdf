#pragma once
#include "contents.h"
#include <cstddef>
#include <cstdint>
#include <string>
namespace rinpdf::parser {
// Extract a bounded subset of PDF text as UTF-8: Tj/TJ with ToUnicode,
// including inline BDC /ActualText replacement.
bool extract_page_text(const uint8_t* pdf,size_t size,size_t page,
 std::string& utf8);
}
