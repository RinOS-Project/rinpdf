#include "../src/parser/function.h"
#include <cmath>
#include <cstdio>
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

static bool expect(const char* source,bool success,double expected=0) {
 double output=-100;
 const bool actual=run(source,output);
 if(actual!=success || (success&&std::abs(output-expected)>1e-12)) {
  std::fprintf(stderr,"Unexpected Type 4 result for %s: success=%d value=%.17g\n",
               source,int(actual),output);
  return false;
 }
 return true;
}

int main() {
 bool ok=true;
 // Div returns a real even if its result happens to be an integer.
 // Integer-only operators must reject the result in compiled mode.
 ok=expect("{ pop 5 2 div 2 mod }",false)&&ok;
 ok=expect("{ pop 8 2 div 1 bitshift }",false)&&ok;
 ok=expect("{ pop 5 2 div 2 add }",true,4.5)&&ok;
 ok=expect("{ pop 8 2 div 2 add }",true,6.0)&&ok;
 // Integer arithmetic keeps integer types for subsequent operators.
 ok=expect("{ pop 5 2 add 3 mod }",true,1.0)&&ok;
 ok=expect("{ pop 8 2 sub 3 mod }",true,0.0)&&ok;
 ok=expect("{ pop 5 2 mul 3 mod }",true,1.0)&&ok;
 // cvi truncates before enforcing the signed 32-bit bounds.
 ok=expect("{ pop 2147483647.9 cvi }",true,2147483647.0)&&ok;
 ok=expect("{ pop -2147483648.9 cvi }",true,-2147483648.0)&&ok;
 ok=expect("{ pop 2147483648.0 cvi }",false)&&ok;
 ok=expect("{ pop -2147483649.0 cvi }",false)&&ok;
 ok=expect("{ pop -1.9 cvi 2 mod }",true,-1.0)&&ok;
 return ok?0:1;
}
