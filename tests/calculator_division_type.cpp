#include "../src/parser/function.h"
#include <cmath>
#include <limits>
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
 // Reject malformed externally supplied compiled IR, without touching output.
 using rinpdf::parser::CalculatorInstruction;
 CalculatorProgram malformed;
 CalculatorInstruction literal;
 literal.kind=0;
 literal.numeric_kind=2;
 literal.number=1.5;
 malformed.instructions.push_back(literal);
 double untouched=42.0;
 ok=(!evaluate_calculator_function(malformed,0.0,1,&untouched)&&untouched==42.0)&&ok;
 malformed.instructions[0].number=2147483648.0;
 ok=(!evaluate_calculator_function(malformed,0.0,1,&untouched)&&untouched==42.0)&&ok;
 malformed.instructions[0].numeric_kind=0;
 malformed.instructions[0].number=std::numeric_limits<double>::quiet_NaN();
 ok=(!evaluate_calculator_function(malformed,0.0,1,&untouched)&&untouched==42.0)&&ok;
 malformed.instructions[0].kind=1;
 malformed.instructions[0].number=2.0;
 ok=(!evaluate_calculator_function(malformed,0.0,1,&untouched)&&untouched==42.0)&&ok;
 // Compiled procedure braces must form properly nested, noncrossing pairs.
 ok=expect("{ pop true { 3 } if }",true,3.0)&&ok;
 CalculatorProgram crossing;
 rinpdf::parser::CalculatorInstruction open0,open1,close2,close3;
 open0.kind=2;open0.matching_brace=2;
 open1.kind=2;open1.matching_brace=3;
 close2.kind=3;close2.matching_brace=0;
 close3.kind=3;close3.matching_brace=1;
 crossing.instructions={open0,open1,close2,close3};
 untouched=42.0;
 ok=(!evaluate_calculator_function(crossing,0.0,1,&untouched)&&untouched==42.0)&&ok;
 crossing.instructions.pop_back();
 ok=(!evaluate_calculator_function(crossing,0.0,1,&untouched)&&untouched==42.0)&&ok;
 // Maximum supported procedure nesting is eight levels.
 for(unsigned depth : {8u,9u}){
  std::string source="{ ";
  for(unsigned i=0;i<depth;++i)source+="{ ";
  for(unsigned i=0;i<depth;++i)source+="} ";
  source+="}";
  CalculatorProgram nested;
  const std::vector<uint8_t> bytes(source.begin(),source.end());
  const bool compiled=compile_calculator_function(bytes,nested);
  if(compiled!=(depth==8u)){
   std::fprintf(stderr,"Unexpected nesting acceptance at depth %u\\n",depth);
   ok=false;
  }
 }
 // PostScript round chooses the greater integer at exact half-way ties.
 ok=expect("{ pop -6.5 round }",true,-6.0)&&ok;
 ok=expect("{ pop -1.5 round }",true,-1.0)&&ok;
 ok=expect("{ pop -1.6 round }",true,-2.0)&&ok;
 ok=expect("{ pop -1.4 round }",true,-1.0)&&ok;
 ok=expect("{ pop 1.5 round }",true,2.0)&&ok;
 ok=expect("{ pop 2.5 round }",true,3.0)&&ok;
 ok=expect("{ pop -1.5 truncate }",true,-1.0)&&ok;
 ok=expect("{ pop -1.5 floor }",true,-2.0)&&ok;
 // A literal-only program must enforce the same finite magnitude budget
 // as calculations, without modifying the caller's output on failure.
 ok=expect("{ pop 1000000000000 }",true,1000000000000.0)&&ok;
 ok=expect("{ pop 1000000000001 }",false)&&ok;
 ok=expect("{ pop -1000000000001 }",false)&&ok;
 double large_output=17.0;
 ok=(!run("{ pop 1000000000001 }",large_output)&&large_output==17.0)&&ok;
 return ok?0:1;
}
