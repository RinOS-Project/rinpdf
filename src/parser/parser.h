#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace rinpdf::parser {
// A deliberately restricted PDF subset: classic indirect objects and /Type /Page.
bool count_pages(const uint8_t *data, size_t size, size_t &pages);
}
