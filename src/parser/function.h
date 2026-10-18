#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include <map>

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

// Execute a pre-tokenized, bounded PDF Type 4 calculator program.
bool evaluate_calculator_tokens(const std::vector<std::string>& tokens,
                                double input,unsigned outputs,
                                double* result,
                                const std::map<std::string,std::pair<double,uint8_t>>* constants=nullptr,
                                const std::map<std::string,uint8_t>* operators=nullptr);

// Lex a decoded PDF Type 4 calculator stream into a validated token program.
// Output is replaced only on success.
bool tokenize_calculator_function(const std::vector<uint8_t>& program,
                                  std::vector<std::string>& tokens);

// An immutable, tokenized Type 4 calculator program, reusable per document.
struct CalculatorInstruction {
 // 0 numeric literal, 1 boolean, 2 procedure open, 3 procedure close,
 // 4 operator.
 uint8_t kind=0;
 uint8_t opcode=0;
 uint8_t numeric_kind=0;
 double number=0;
};
struct CalculatorProgram {
 std::vector<std::string> tokens;
 std::vector<CalculatorInstruction> instructions;
 // Parsed numeric constants keyed by their literal text, reused in branches.
 std::map<std::string,std::pair<double,uint8_t>> numeric_constants;
 // Prevalidated operator spellings; unsupported names are rejected at compile time.
 std::map<std::string,uint8_t> operator_codes;
};
// Compile without changing the destination on failure.
bool compile_calculator_function(const std::vector<uint8_t>& source,
                                 CalculatorProgram& output);
bool evaluate_calculator_function(const CalculatorProgram& program,
                                  double input,unsigned outputs,double* result);

} // namespace rinpdf::parser
