#pragma once
#include "value.h"
#include <cstddef>
#include <cstdint>
namespace rinpdf::parser {
struct StreamRange { Value dictionary; size_t offset=0; size_t length=0; };
bool locate_stream(const uint8_t *data,size_t size,StreamRange& out);
}
