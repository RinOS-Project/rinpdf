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
 // Interleaved RGB samples use one interpolation position, but each
 // output must read its own channel from the sample table.
 rinpdf::parser::SampledFunction rgb;
 rgb.count=2;rgb.bits=8;rgb.outputs=3;rgb.order=1;
 rgb.domain[0]=0;rgb.domain[1]=1;
 rgb.encode[0]=0;rgb.encode[1]=1;
 for(unsigned i=0;i<3;++i){
  rgb.decode[2*i]=0;rgb.decode[2*i+1]=1;
  rgb.range[2*i]=0;rgb.range[2*i+1]=1;
 }
 // Distinct channel ramps catch indexing bugs masked by equal RGB outputs.
 const std::vector<uint8_t> rgb_samples{0,32,64,255,128,192};
 double channels[3]={-1,-1,-1};
 for(unsigned order:{1u,3u}){
  rgb.order=order;
  if(!rinpdf::parser::evaluate_sampled_function(
        rgb,rgb_samples,0.5,channels)||
     std::abs(channels[0]-0.5)>1e-12||
     std::abs(channels[1]-(80.0/255.0))>1e-12||
     std::abs(channels[2]-(128.0/255.0))>1e-12){
   std::fprintf(stderr,"RGB channel interpolation mismatch for order %u\\n",order);
   return 1;
  }
 }
 return 0;
}
