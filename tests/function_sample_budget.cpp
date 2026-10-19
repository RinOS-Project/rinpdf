#include "../src/parser/function.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

using rinpdf::parser::MultidimensionalSampledFunction;
using rinpdf::parser::evaluate_multidimensional_sampled_function;

int main() {
 MultidimensionalSampledFunction f;
 f.inputs=2; f.outputs=1; f.bits=8; f.order=1;
 f.sizes[0]=2; f.sizes[1]=2;
 f.domain={0,1,0,1,0,0,0,0};
 f.encode={0,1,0,1,0,0,0,0};
 f.decode={0,1,0,0,0,0,0,0};
 f.range={0,1,0,0,0,0,0,0};
 const double xy[2]={1.0,0.0};
 double result=91.0;
 const std::vector<uint8_t> samples={0,255,0,255};
 if(!evaluate_multidimensional_sampled_function(f,samples,xy,&result)||
    result!=1.0){
  std::fprintf(stderr,"small multidimensional sampled function failed\n");
  return 1;
 }
 // A direct evaluator call must reject the same oversized grid that
 // compile-time validation rejects, while preserving the output.
 f.sizes[0]=4096;
 f.sizes[1]=2048;
 f.encode[0]=4095;
 f.encode[2]=2047;
 const std::vector<uint8_t> oversized(4096u*2048u,255);
 result=91.0;
 if(evaluate_multidimensional_sampled_function(f,oversized,xy,&result)||
    result!=91.0){
  std::fprintf(stderr,"oversized multidimensional grid accepted\n");
  return 1;
 }
 return 0;
}
