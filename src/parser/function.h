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
 unsigned order=1;
 double domain[2]{0,1};
 double encode[2]{};
 std::array<double,8> decode{};
 std::array<double,8> range{};
};

// Evaluates a previously decoded sampled function by linear interpolation.
// The caller owns and retains the decoded stream bytes.
bool evaluate_sampled_function(const SampledFunction& function,
                               const std::vector<uint8_t>& samples,
                               double input,double* result,
                               const std::vector<double>* expanded=nullptr) noexcept;

// Multi-input sampled functions used by DeviceN and Separation tint maps.
// The first input dimension varies fastest in the PDF sample table.
struct MultidimensionalSampledFunction {
 unsigned inputs=0,outputs=0,bits=0,order=1;
 std::array<unsigned,4> sizes{};
 std::array<double,8> domain{};
 std::array<double,8> encode{};
 std::array<double,8> decode{};
 std::array<double,8> range{};
};
bool evaluate_multidimensional_sampled_function(
 const MultidimensionalSampledFunction& function,
 const std::vector<uint8_t>& samples,const double* inputs,
 double* result,const std::vector<double>* expanded=nullptr) noexcept;

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
                                const std::map<std::string,uint8_t>* operators=nullptr,
                                const std::vector<struct CalculatorInstruction>* instructions=nullptr);

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
 // Matching brace instruction index, or SIZE_MAX for non-braces.
 size_t matching_brace=SIZE_MAX;
};
struct CalculatorProgram {
 // Complete validated program. Execution does not require source tokens.
 std::vector<CalculatorInstruction> instructions;
};
// Compile without changing the destination on failure.
bool compile_calculator_function(const std::vector<uint8_t>& source,
                                 CalculatorProgram& output);
bool evaluate_calculator_function(const CalculatorProgram& program,
                                  double input,unsigned outputs,double* result);

// Common, compiled one-input PDF Function representation.
// Multi-input sampled functions and recursive Type 3 compilation will use
// separate extensions; unsupported kinds fail closed.
struct PdfFunction {
 enum class Kind : uint8_t { Sampled=0, Exponential=2, Stitching=3, Calculator=4, MultiSampled=5 };
 Kind kind=Kind::Exponential;
 unsigned outputs=0;
 double domain[2]{0,1};
 std::array<double,8> range{};
 ExponentialFunction exponential;
 SampledFunction sampled;
 MultidimensionalSampledFunction multidimensional;
 std::vector<uint8_t> samples;
 // Optional predecoded normalized grid, bounded at compile time.
 std::vector<double> expanded_samples;
 CalculatorProgram calculator;
 std::vector<double> stitch_bounds,stitch_encode;
 std::vector<PdfFunction> stitch_children;
};

bool compile_multidimensional_pdf_function(const MultidimensionalSampledFunction& parameters,
                                           const std::vector<uint8_t>& decoded_samples,
                                           PdfFunction& output);
bool evaluate_pdf_function_inputs(const PdfFunction& function,
                                  const double* inputs,unsigned count,
                                  double* result);

bool compile_sampled_pdf_function(const SampledFunction& parameters,
                                  const std::vector<uint8_t>& decoded_samples,
                                  PdfFunction& output);
bool compile_stitching_pdf_function(const std::vector<PdfFunction>& children,
                                    const std::vector<double>& bounds,
                                    const std::vector<double>& encode,
                                    unsigned outputs,double domain_min,
                                    double domain_max,
                                    const std::array<double,8>& range,
                                    PdfFunction& output);

bool compile_exponential_pdf_function(const ExponentialFunction& parameters,
                                      unsigned outputs,double domain_min,
                                      double domain_max,
                                      const std::array<double,8>& range,
                                      PdfFunction& output);
bool compile_calculator_pdf_function(const std::vector<uint8_t>& source,
                                     unsigned outputs,double domain_min,
                                     double domain_max,
                                     const std::array<double,8>& range,
                                     PdfFunction& output);

bool evaluate_pdf_function(const PdfFunction& function,
                           double input,double* result);

} // namespace rinpdf::parser
