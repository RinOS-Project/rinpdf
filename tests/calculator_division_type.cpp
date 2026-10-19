#include "../src/parser/function.h"
#include <cassert>
#include <cmath>
#include <string>
#include <vector>

using rinpdf::parser::CalculatorProgram;
using rinpdf::parser::compile_calculator_function;
using rinpdf::parser::evaluate_calculator_function;

static bool run(const char* source, double& result) {
 const std::string text(source);
 const std::vector<uint8_t> bytes(text.begin(),text.end());
 CalculatorProgram program;
 if(!compile_calculator_function(bytes,program))return false;
 return evaluate_calculator_function(program,0.0,1,&result);
}

int main() {
 double output=-100;
 // div returns a real value, even when both operands were integers.
 // A later integer-only operator must reject it.
 assert(!run("{ pop 5 2 div 2 mod }",output));
 assert(!run("{ pop 8 2 div 1 bitshift }",output));
 assert(run("{ pop 5 2 div 2 add }",output));
 assert(std::abs(output-4.5)<1e-12);
 // Non-division integer arithmetic retains integer operands for mod.
 assert(run("{ pop 5 2 add 3 mod }",output));
 assert(output==1.0);
 return 0;
}
