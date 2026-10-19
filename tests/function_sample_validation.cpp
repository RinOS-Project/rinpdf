#include "../src/parser/function.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

int main(){
 rinpdf::parser::SampledFunction f;
 f.count=2;f.bits=8;f.outputs=1;f.order=1;
 f.domain[0]=0;f.domain[1]=1;
 f.encode[0]=0;f.encode[1]=1;
 f.decode[0]=0;f.decode[1]=1;
 f.range[0]=0;f.range[1]=1;
 const std::vector<uint8_t> samples{0,255};
 std::vector<double> expanded{0.0,1.0};
 double result=99.0;
 if(!rinpdf::parser::evaluate_sampled_function(f,samples,1.0,&result,&expanded)||
    result!=1.0)return 1;
 for(double bad:{std::numeric_limits<double>::quiet_NaN(),
                 std::numeric_limits<double>::infinity(),-0.25,1.25}){
  expanded[1]=bad;
  result=99.0;
  if(rinpdf::parser::evaluate_sampled_function(f,samples,1.0,&result,&expanded)||
     result!=99.0){
   std::fprintf(stderr,"invalid expanded sample accepted\n");
   return 1;
  }
 }
 return 0;
}
