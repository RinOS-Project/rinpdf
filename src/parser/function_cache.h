#pragma once
#include "value.h"
#include "function.h"
#include "stream_object.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rinpdf::parser {
// Parsed PDF Function descriptions. These own their data independently of
// the source dictionary and can be reused across evaluations.
struct CachedFunction {
 Value dictionary;
 StreamRange stream;
 bool is_stream=false;
};
struct CachedStitch {
 std::vector<Value> functions;
 std::vector<double> bounds;
 std::vector<double> encode;
};
using CachedExponent = ExponentialFunction;
struct CachedSample {
 size_t count=0;
 unsigned bits=0;
 double encode[2]{};
 std::array<double,8> decode{};
 std::array<double,8> range{};
};
} // namespace rinpdf::parser
