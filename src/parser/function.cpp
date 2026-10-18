#include "function.h"
#include <cmath>

namespace rinpdf::parser {

bool evaluate_exponential_function(const ExponentialFunction& function,
                                   double input,unsigned outputs,
                                   double* result) noexcept {
 if(!result||outputs==0||outputs>4||!std::isfinite(input)||
    !std::isfinite(function.exponent)||function.exponent<=0)
  return false;
 const double powered=std::pow(input,function.exponent);
 if(!std::isfinite(powered))return false;
 std::array<double,4> computed{};
 for(unsigned j=0;j<outputs;++j){
  if(!std::isfinite(function.c0[j])||!std::isfinite(function.c1[j]))
   return false;
  computed[j]=function.c0[j]+powered*(function.c1[j]-function.c0[j]);
  if(!std::isfinite(computed[j]))return false;
 }
 for(unsigned j=0;j<outputs;++j)result[j]=computed[j];
 return true;
}

} // namespace rinpdf::parser
