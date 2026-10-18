#pragma once
#include <array>

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

} // namespace rinpdf::parser
