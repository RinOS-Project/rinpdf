#include "function.h"
#include <cmath>
#include <algorithm>
#include <limits>

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

bool evaluate_sampled_function(const SampledFunction& function,
                               const std::vector<uint8_t>& samples,
                               double input,double* result) noexcept {
 const unsigned bits=function.bits, outputs=function.outputs;
 if(!result||outputs<1||outputs>4||function.count<2||
    (bits!=1&&bits!=2&&bits!=4&&bits!=8&&bits!=12&&
     bits!=16&&bits!=24&&bits!=32)||
    !std::isfinite(input)||!std::isfinite(function.domain[0])||
    !std::isfinite(function.domain[1])||
    function.domain[1]<=function.domain[0]||
    !std::isfinite(function.encode[0])||
    !std::isfinite(function.encode[1])||
    function.count>std::numeric_limits<size_t>::max()/outputs/bits)
  return false;
 const size_t total_bits=function.count*outputs*bits;
 if(total_bits>samples.size()*uint64_t(8))return false;
 const double clamped=std::clamp(input,function.domain[0],
                                      function.domain[1]);
 const double normalized=(clamped-function.domain[0])/
                          (function.domain[1]-function.domain[0]);
 const double position=std::clamp(function.encode[0]+normalized*
     (function.encode[1]-function.encode[0]),0.0,
     double(function.count-1));
 if(!std::isfinite(position))return false;
 const size_t index=size_t(position);
 const size_t next=std::min(index+1,function.count-1);
 const double fraction=position-double(index);
 auto sample_at=[&](size_t sample_index)->double {
  const size_t first_bit=sample_index*bits;
  uint64_t value=0;
  for(unsigned bit=0;bit<bits;++bit){
   const size_t bit_pos=first_bit+bit;
   value=(value<<1)|((samples[bit_pos/8]>>(7-(bit_pos%8)))&1u);
  }
  return double(value)/double((uint64_t(1)<<bits)-1);
 };
 std::array<double,4> computed{};
 for(unsigned j=0;j<outputs;++j){
  const double lo=function.range[2*j],hi=function.range[2*j+1];
  const double d0=function.decode[2*j],d1=function.decode[2*j+1];
  if(!std::isfinite(lo)||!std::isfinite(hi)||lo>hi||
     !std::isfinite(d0)||!std::isfinite(d1))return false;
  const double v=(1-fraction)*sample_at(index*outputs+j)+
                 fraction*sample_at(next*outputs+j);
  computed[j]=std::clamp(d0+v*(d1-d0),lo,hi);
  if(!std::isfinite(computed[j]))return false;
 }
 for(unsigned j=0;j<outputs;++j)result[j]=computed[j];
 return true;
}

bool map_stitching_input(const std::vector<double>& bounds,
                         const std::vector<double>& encode,
                         double input,size_t& segment,
                         double& mapped_input) noexcept {
 if(!std::isfinite(input)||bounds.size()<2||bounds.size()>33||
    encode.size()!=(bounds.size()-1)*2)return false;
 for(size_t i=0;i<bounds.size();++i){
  if(!std::isfinite(bounds[i])||(i>0&&bounds[i]<=bounds[i-1]))
   return false;
 }
 for(double v:encode)if(!std::isfinite(v))return false;
 const double clamped=std::clamp(input,bounds.front(),bounds.back());
 const auto it=std::upper_bound(bounds.begin()+1,bounds.end()-1,clamped);
 const size_t selected=size_t(it-(bounds.begin()+1));
 const double left=bounds[selected],right=bounds[selected+1];
 const double e0=encode[2*selected],e1=encode[2*selected+1];
 const double mapped=e0+(clamped-left)/(right-left)*(e1-e0);
 if(!std::isfinite(mapped))return false;
 segment=selected;
 mapped_input=mapped;
 return true;
}

} // namespace rinpdf::parser
