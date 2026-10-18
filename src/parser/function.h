#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rinpdf::parser {

// Immutable, already parsed parameters for a one-input exponential function.
// The caller is responsible for applying the function's Domain and Range.
struct ExponentialFunction {
 double exponent=1;
 std::array<double,4> c0{{0,0,0,0}};
 std::array<double,4> c1{{1,1,1,1}};
};

// Evaluates an exponential PDF function using caller-clamped input.
// Returns false for invalid parameters, non-finite results or output count.
bool evaluate_exponential_function(const ExponentialFunction& function,
                                   double input,unsigned outputs,
                                   double* result) noexcept;

// Parsed one-input sampled PDF FunctionType 0. Raw samples use PDF
// MSB-first packed representation, without per-sample byte alignment.
struct SampledFunction {
 size_t count=0;
 unsigned bits=0;
 unsigned outputs=0;
 double domain[2]{0,1};
 double encode[2]{};
 std::array<double,8> decode{};
 std::array<double,8> range{};
};

// Evaluates a previously decoded sampled function by linear interpolation.
// The caller owns and retains the decoded stream bytes.
bool evaluate_sampled_function(const SampledFunction& function,
                               const std::vector<uint8_t>& samples,
                               double input,double* result) noexcept;

// Select a Type 3 stitching segment and map its input into its child domain.
// Bounds contain the initial and final domain endpoints.
bool map_stitching_input(const std::vector<double>& bounds,
                         const std::vector<double>& encode,
                         double input,size_t& segment,
                         double& mapped_input) noexcept;

} // namespace rinpdf::parser
