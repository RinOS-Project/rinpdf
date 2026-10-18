#include "function.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <charconv>
#include <string>
#include <cstdint>
#include <utility>

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

static bool parse_calculator_number(const std::string& token,double& value,uint8_t& kind) {
    const bool numeric_start=inst?inst->kind==0:
      token[0]=='+'||token[0]=='-'||token[0]=='.'||
      (token[0]>='0'&&token[0]<='9');
    if(!numeric_start)return false;
     const char* first=token.data();
     const char* last=first+token.size();
     bool negative=false;
     if(*first=='+'||*first=='-'){
      negative=*first=='-';++first;
     }
     if(first==last)return false;
     value=0;
     bool integer_literal=true;
     const char* hash=std::find(first,last,'#');
     if(hash!=last){
      // Radix integers use a 32-bit bit pattern; values with the high
      // bit set are interpreted as negative signed integers.
      if(negative||first==hash||hash+1==last)return false;
      unsigned base=0;
      const auto radix=std::from_chars(first,hash,base);
      if(radix.ec!=std::errc()||radix.ptr!=hash||base<2||base>36)
       return false;
      uint64_t accumulated=0;
      for(const char* p=hash+1;p!=last;++p){
       const char ch=*p;
       const unsigned digit=ch>='0'&&ch<='9'?unsigned(ch-'0'):
          ch>='A'&&ch<='Z'?unsigned(ch-'A'+10):
          ch>='a'&&ch<='z'?unsigned(ch-'a'+10):36u;
       if(digit>=base||
          accumulated>(uint64_t(UINT32_MAX)-digit)/base)return false;
       accumulated=accumulated*base+digit;
      }
      value=accumulated>=0x80000000ULL?
        double(int64_t(accumulated)-4294967296LL):double(accumulated);
     }else if(std::find_first_of(first,last,".eE",".eE"+3)==last){
      int64_t whole=0;
      const auto parsed=std::from_chars(first,last,whole);
      if(parsed.ec!=std::errc()||parsed.ptr!=last)return false;
      if(negative)whole=-whole;
      if(whole<INT32_MIN||whole>INT32_MAX)return false;
      value=double(whole);
     }else{
      integer_literal=false;
      const auto parsed=std::from_chars(first,last,value,
                                        std::chars_format::general);
      if(parsed.ec!=std::errc()||parsed.ptr!=last)return false;
      if(negative)value=-value;
      if(!std::isfinite(value))return false;
     }
 kind=integer_literal?2:0;
 return true;
}

enum class CalculatorOp : uint8_t {
 Op0, // if
 Op1, // ifelse
 Op2, // copy
 Op3, // index
 Op4, // roll
 Op5, // dup
 Op6, // exch
 Op7, // pop
 Op8, // cvi
 Op9, // cvr
 Op10, // floor
 Op11, // ceiling
 Op12, // round
 Op13, // truncate
 Op14, // ln
 Op15, // log
 Op16, // sin
 Op17, // cos
 Op18, // idiv
 Op19, // mod
 Op20, // exp
 Op21, // atan
 Op22, // bitshift
 Op23, // neg
 Op24, // abs
 Op25, // sqrt
 Op26, // add
 Op27, // sub
 Op28, // mul
 Op29, // div
 Op30, // min
 Op31, // max
 Op32, // eq
 Op33, // ne
 Op34, // gt
 Op35, // ge
 Op36, // lt
 Op37, // le
 Op38, // and
 Op39, // or
 Op40, // xor
 Op41 // not
};
static const std::vector<std::string>& calculator_operator_names(){
 static const std::vector<std::string> names={
 "if", "ifelse", "copy", "index", "roll", "dup", "exch", "pop", "cvi", "cvr", "floor", "ceiling", "round", "truncate", "ln", "log", "sin", "cos", "idiv", "mod", "exp", "atan", "bitshift", "neg", "abs", "sqrt", "add", "sub", "mul", "div", "min", "max", "eq", "ne", "gt", "ge", "lt", "le", "and", "or", "xor", "not"
 };
 return names;
}
bool evaluate_calculator_tokens(const std::vector<std::string>& parsed_tokens,
                                double input,unsigned outputs,
                                double* result,
                                const std::map<std::string,std::pair<double,uint8_t>>* constants,
                                const std::map<std::string,uint8_t>* operators,
                                const std::vector<CalculatorInstruction>* instructions) {
 if(!result||outputs<1||outputs>4||!std::isfinite(input))return false;
 const size_t program_size=instructions?instructions->size():parsed_tokens.size();
 if(program_size>4096)return false;
 if(instructions&&!parsed_tokens.empty()&&
    instructions->size()!=parsed_tokens.size())return false;
 if(instructions){
  // Reject malformed IR before execution; do not fall back to textual
  // interpretation for compiled programs.
  for(size_t i=0;i<instructions->size();++i){
   const auto& instruction=(*instructions)[i];
   if(instruction.kind>4)return false;
   if(instruction.kind==4&&instruction.opcode>=calculator_operator_names().size())
    return false;
   if(instruction.kind==2||instruction.kind==3){
    if(instruction.matching_brace>=instructions->size())return false;
    const auto& other=(*instructions)[instruction.matching_brace];
    if(other.matching_brace!=i||
       other.kind!=(instruction.kind==2?3:2))return false;
   }
  }
 }
 const auto& tokens=parsed_tokens;
 struct CalcValue { double number; uint8_t kind; size_t procedure; };
 struct ProcedureSpan { size_t begin,end; };
 struct ExecutionFrame { size_t pc,end; uint8_t depth; };
 std::vector<CalcValue> stack{{input,0,0}};
 std::vector<ProcedureSpan> procedures;
 std::vector<ExecutionFrame> frames{{0,program_size,0}};
 size_t operations=0;
 std::string token;
 while(!frames.empty()){
  auto& frame=frames.back();
  if(frame.pc>=frame.end){frames.pop_back();continue;}
  const size_t pc=frame.pc++;
  const CalculatorInstruction* inst=instructions?&(*instructions)[pc]:nullptr;
  if(++operations>4096)return false;
  if(!inst){token=tokens[pc];if(token.empty()||token.size()>64)return false;}
  if(inst?inst->kind==2:token=="{"){
   size_t end=SIZE_MAX;
   if(inst&&inst->kind==2&&inst->matching_brace<frame.end&&
      inst->matching_brace>pc&&(*instructions)[inst->matching_brace].kind==3)
    end=inst->matching_brace;
   else{
    // A compiled program must have validated matching braces. Only the
    // legacy token-only path performs runtime delimiter discovery.
    if(inst)return false;
    unsigned nested=1;
    for(size_t i=pc+1;i<frame.end;++i){
     if(tokens[i]=="{"){if(++nested>8)return false;}
     else if(tokens[i]=="}"&&--nested==0){end=i;break;}
    }
   }
   if(end==SIZE_MAX||end-pc-1>4096||procedures.size()>=64||
      stack.size()>=64)return false;
   procedures.push_back({pc+1,end});
   stack.push_back({0,3,procedures.size()});
   frame.pc=end+1;
   continue;
  }
  if(inst?inst->kind==3:token=="}")return false;
  if(inst?(inst->kind==4&&(inst->opcode==0||inst->opcode==1)):
     (token=="if"||token=="ifelse")){
   const size_t needed=(inst?inst->opcode==0:token=="if")?1:2;
   if(stack.size()<needed+1||
      stack[stack.size()-needed-1].kind!=1)return false;
   for(size_t j=0;j<needed;++j)
    if(stack[stack.size()-needed+j].kind!=3)return false;
   const bool condition=stack[stack.size()-needed-1].number!=0;
   const size_t id=needed==1?stack.back().procedure:
     stack[stack.size()-(condition?2:1)].procedure;
   if(id==0||id>procedures.size())return false;
   const ProcedureSpan span=procedures[id-1];
   for(size_t j=0;j<needed+1;++j)stack.pop_back();
   if((needed==1&&!condition)||span.begin==span.end)continue;
   if(frame.depth>=8||span.end>program_size)return false;
   frames.push_back({span.begin,span.end,uint8_t(frame.depth+1)});
   continue;
  }
    const bool numeric_start=inst?inst->kind==0:
      token[0]=='+'||token[0]=='-'||token[0]=='.'||
      (token[0]>='0'&&token[0]<='9');
    if(numeric_start){
     double value=0;uint8_t kind=0;
     if(inst){value=inst->number;kind=inst->numeric_kind;}
     else if(constants){
      const auto hit=constants->find(token);
      if(hit==constants->end())return false;
      value=hit->second.first;kind=hit->second.second;
     }else if(!parse_calculator_number(token,value,kind))return false;
     if(stack.size()>=64)return false;
     stack.push_back({value,kind,0});
     continue;
    }
    if(inst?inst->kind==1:(token=="true"||token=="false")){
     if(stack.size()>=64)return false;
     stack.push_back({inst?inst->number:
       (token=="true"?1.0:0.0),1,0});continue;
    }
    uint8_t opcode=0;
    if(inst){
     if(inst->kind!=4)return false;
     opcode=inst->opcode;
    }else if(operators){
     const auto hit=operators->find(token);
     if(hit==operators->end()||hit->second>=calculator_operator_names().size())
      return false;
     opcode=hit->second;
    }else{
     const auto& names=calculator_operator_names();
     const auto it=std::find(names.begin(),names.end(),token);
     if(it==names.end())return false;
     opcode=uint8_t(it-names.begin());
    }
    if(opcode==uint8_t(CalculatorOp::Op2)){
     if(stack.empty())return false;
     if(stack.back().kind!=2)return false;
     const double n=stack.back().number;stack.pop_back();
     if(n<0||n!=std::floor(n)||n>double(stack.size())||
        stack.size()+size_t(n)>64)return false;
     const size_t count=size_t(n);
     const std::vector<CalcValue> copied(stack.end()-count,stack.end());
     stack.insert(stack.end(),copied.begin(),copied.end());
     
     continue;
    }
    if(opcode==uint8_t(CalculatorOp::Op3)){
     if(stack.empty()||stack.back().kind!=2)return false;
     const double n=stack.back().number;stack.pop_back();
     if(n<0||n!=std::floor(n)||n>=double(stack.size())||
        stack.size()>=64)return false;
     stack.push_back(stack[stack.size()-1-size_t(n)]);
     continue;
    }
    if(opcode==uint8_t(CalculatorOp::Op4)){
     if(stack.size()<2)return false;
     if(stack.back().kind!=2||stack[stack.size()-2].kind!=2)return false;
     const double shift=stack.back().number;stack.pop_back();
     const double n=stack.back().number;stack.pop_back();
     if(n<0||n!=std::floor(n)||n>double(stack.size())||
        shift!=std::trunc(shift)||!std::isfinite(shift)||
        std::abs(shift)>2147483647.0)return false;
     const size_t count=size_t(n);
     if(count){
      const long long distance=static_cast<long long>(std::fmod(
         shift,double(count)));
      const size_t right=size_t((distance+long long(count))%long long(count));
      std::rotate(stack.end()-count,stack.end()-right,stack.end());
      
     }
     continue;
    }
    if(opcode==uint8_t(CalculatorOp::Op5)){
     if(stack.empty()||stack.size()>=64)return false;
     stack.push_back(stack.back());continue;
    }
    if(opcode==uint8_t(CalculatorOp::Op6)){
     if(stack.size()<2)return false;
     std::swap(stack[stack.size()-1],stack[stack.size()-2]);
     continue;
    }
    if(opcode==uint8_t(CalculatorOp::Op7)){
     if(stack.empty())return false;
     stack.pop_back();continue;
    }
    if(opcode==uint8_t(CalculatorOp::Op8)||opcode==uint8_t(CalculatorOp::Op9)||opcode==uint8_t(CalculatorOp::Op10)||
       opcode==uint8_t(CalculatorOp::Op11)||opcode==uint8_t(CalculatorOp::Op12)||opcode==uint8_t(CalculatorOp::Op13)||
       opcode==uint8_t(CalculatorOp::Op14)||opcode==uint8_t(CalculatorOp::Op15)||opcode==uint8_t(CalculatorOp::Op16)||opcode==uint8_t(CalculatorOp::Op17)){
     if(stack.empty())return false;
     if(stack.back().kind==1||stack.back().kind==3)return false;
     double& value=stack.back().number;
     if(opcode==uint8_t(CalculatorOp::Op8)){
      if(value< -2147483648.0||value>2147483647.0)return false;
      value=std::trunc(value);
     }else if(opcode==uint8_t(CalculatorOp::Op13))value=std::trunc(value);
     else if(opcode==uint8_t(CalculatorOp::Op10))value=std::floor(value);
     else if(opcode==uint8_t(CalculatorOp::Op11))value=std::ceil(value);
     else if(opcode==uint8_t(CalculatorOp::Op12))
      value=value<0? -std::floor(-value+0.5):std::floor(value+0.5);
     else if(opcode==uint8_t(CalculatorOp::Op14)||opcode==uint8_t(CalculatorOp::Op15)){
      if(value<=0)return false;
      value=opcode==uint8_t(CalculatorOp::Op14)?std::log(value):std::log10(value);
     }else if(opcode==uint8_t(CalculatorOp::Op16)||opcode==uint8_t(CalculatorOp::Op17)){
      const double radians=value*(3.14159265358979323846/180.0);
      value=opcode==uint8_t(CalculatorOp::Op16)?std::sin(radians):std::cos(radians);
     }
     if(!std::isfinite(value)||std::abs(value)>1e12)return false;
     if(opcode==uint8_t(CalculatorOp::Op8)){
      stack.back().kind=2;
     }else if(opcode==uint8_t(CalculatorOp::Op9)||opcode==uint8_t(CalculatorOp::Op14)||opcode==uint8_t(CalculatorOp::Op15)||
              opcode==uint8_t(CalculatorOp::Op16)||opcode==uint8_t(CalculatorOp::Op17))stack.back().kind=0;
     continue;
    }
    if(opcode==uint8_t(CalculatorOp::Op18)||opcode==uint8_t(CalculatorOp::Op19)||opcode==uint8_t(CalculatorOp::Op20)||
       opcode==uint8_t(CalculatorOp::Op21)||opcode==uint8_t(CalculatorOp::Op22)){
     if(stack.size()<2)return false;
     if(stack.back().kind==1||stack.back().kind==3||stack[stack.size()-2].kind==1||stack[stack.size()-2].kind==3)return false;
     const bool rhs_integer=stack.back().kind==2;
     const double rhs=stack.back().number;stack.pop_back();
     double& lhs=stack.back().number;
     if(opcode==uint8_t(CalculatorOp::Op18)||opcode==uint8_t(CalculatorOp::Op19)){
      if(stack.back().kind!=2||!rhs_integer||rhs==0)return false;
      if(lhs!=std::trunc(lhs)||rhs!=std::trunc(rhs)||
         lhs< -2147483648.0||lhs>2147483647.0||
         rhs< -2147483648.0||rhs>2147483647.0||
         (lhs==-2147483648.0&&rhs==-1))return false;
      if(opcode==uint8_t(CalculatorOp::Op18))lhs=std::trunc(lhs/rhs);
      else lhs=std::fmod(lhs,rhs);
     }else if(opcode==uint8_t(CalculatorOp::Op20)){
      if(lhs<0&&rhs!=std::trunc(rhs))return false;
      lhs=std::pow(lhs,rhs);
     }else if(opcode==uint8_t(CalculatorOp::Op21)){
      if(lhs==0&&rhs==0)return false;
      lhs=std::atan2(lhs,rhs)*(180.0/3.14159265358979323846);
      if(lhs<0)lhs+=360.0;
     }else{
      if(stack.back().kind!=2||!rhs_integer||
         lhs!=std::trunc(lhs)||lhs< -2147483648.0||
         lhs>2147483647.0||std::abs(rhs)>31)return false;
      const uint32_t n=uint32_t(int32_t(lhs));
      const int shift=int(rhs);
      // Calculate signed 32-bit shifts explicitly without implementation-
      // defined unsigned-to-signed narrowing on out-of-range values.
      const uint32_t shifted=shift>=0?n<<shift:
        (n>>(-shift))|(lhs<0?(~uint32_t(0)<<(32+shift)):0u);
      lhs=shifted>=0x80000000u?
          double(int64_t(shifted)-4294967296LL):double(shifted);
     }
     if(!std::isfinite(lhs)||std::abs(lhs)>1e12)return false;
     stack.back().kind=(opcode==uint8_t(CalculatorOp::Op18)||opcode==uint8_t(CalculatorOp::Op19)||opcode==uint8_t(CalculatorOp::Op22))?2:0;
     continue;
    }
    if(opcode==uint8_t(CalculatorOp::Op23)||opcode==uint8_t(CalculatorOp::Op24)||opcode==uint8_t(CalculatorOp::Op25)){
     if(stack.empty())return false;
     if(stack.back().kind==1||stack.back().kind==3)return false;
     double& top=stack.back().number;
     if(stack.back().kind==2&&top==-2147483648.0&&
        (opcode==uint8_t(CalculatorOp::Op23)||opcode==uint8_t(CalculatorOp::Op24)))return false;
     if(opcode==uint8_t(CalculatorOp::Op25))stack.back().kind=0;
     if(opcode==uint8_t(CalculatorOp::Op23))top=-top;
     else if(opcode==uint8_t(CalculatorOp::Op24))top=std::abs(top);
     else{
      if(top<0)return false;
      top=std::sqrt(top);
     }
    }else if(opcode==uint8_t(CalculatorOp::Op26)||opcode==uint8_t(CalculatorOp::Op27)||opcode==uint8_t(CalculatorOp::Op28)||
             opcode==uint8_t(CalculatorOp::Op29)||opcode==uint8_t(CalculatorOp::Op30)||opcode==uint8_t(CalculatorOp::Op31)){
     if(stack.size()<2)return false;
     if(stack.back().kind==1||stack.back().kind==3||stack[stack.size()-2].kind==1||stack[stack.size()-2].kind==3)return false;
     const bool integer_pair=stack.back().kind==2&&stack[stack.size()-2].kind==2;
     const double rhs=stack.back().number;stack.pop_back();
     double& lhs=stack.back().number;
     if(opcode==uint8_t(CalculatorOp::Op26))lhs+=rhs;
     else if(opcode==uint8_t(CalculatorOp::Op27))lhs-=rhs;
     else if(opcode==uint8_t(CalculatorOp::Op28))lhs*=rhs;
     else if(opcode==uint8_t(CalculatorOp::Op29)){
      if(rhs==0)return false;
      lhs/=rhs;
     }else if(opcode==uint8_t(CalculatorOp::Op30))lhs=std::min(lhs,rhs);
     else lhs=std::max(lhs,rhs);
     const bool in_integer_range=std::isfinite(lhs)&&
       lhs>=-2147483648.0&&lhs<=2147483647.0;
     if(integer_pair&&token!="div"&&!in_integer_range)return false;
     const bool keep_integer=integer_pair&&token!="div"&&
       lhs==std::trunc(lhs)&&in_integer_range;
     stack.back().kind=keep_integer?2:0;
    }else if(opcode==uint8_t(CalculatorOp::Op32)||opcode==uint8_t(CalculatorOp::Op33)||opcode==uint8_t(CalculatorOp::Op34)||
             opcode==uint8_t(CalculatorOp::Op35)||opcode==uint8_t(CalculatorOp::Op36)||opcode==uint8_t(CalculatorOp::Op37)||
             opcode==uint8_t(CalculatorOp::Op38)||opcode==uint8_t(CalculatorOp::Op39)||opcode==uint8_t(CalculatorOp::Op40)){
     if(stack.size()<2)return false;
     if(stack.back().kind==3||stack[stack.size()-2].kind==3)return false;
     const bool lb=stack[stack.size()-2].kind==1;
     const bool rb=stack.back().kind==1;
     const bool logic=opcode==uint8_t(CalculatorOp::Op38)||opcode==uint8_t(CalculatorOp::Op39)||opcode==uint8_t(CalculatorOp::Op40);
     const bool equality=opcode==uint8_t(CalculatorOp::Op32)||opcode==uint8_t(CalculatorOp::Op33);
     const bool integers=stack.back().kind==2&&
                         stack[stack.size()-2].kind==2;
     if((logic&&!((lb&&rb)||integers))||
        (equality&&lb!=rb)||
        (!logic&&!equality&&(lb||rb)))return false;
     const double rhs=stack.back().number;stack.pop_back();
     double& lhs=stack.back().number;
     if(opcode==uint8_t(CalculatorOp::Op32))lhs=lhs==rhs;
     else if(opcode==uint8_t(CalculatorOp::Op33))lhs=lhs!=rhs;
     else if(opcode==uint8_t(CalculatorOp::Op34))lhs=lhs>rhs;
     else if(opcode==uint8_t(CalculatorOp::Op35))lhs=lhs>=rhs;
     else if(opcode==uint8_t(CalculatorOp::Op36))lhs=lhs<rhs;
     else if(opcode==uint8_t(CalculatorOp::Op37))lhs=lhs<=rhs;
     else if(integers){
      if(lhs< -2147483648.0||lhs>2147483647.0||
         rhs< -2147483648.0||rhs>2147483647.0)return false;
      const uint32_t a=uint32_t(int32_t(lhs));
      const uint32_t b=uint32_t(int32_t(rhs));
      const uint32_t bits=opcode==uint8_t(CalculatorOp::Op38)?(a&b):
                          opcode==uint8_t(CalculatorOp::Op39)?(a|b):(a^b);
      lhs=bits>=0x80000000u?double(int64_t(bits)-4294967296LL):
                             double(bits);
     }else if(opcode==uint8_t(CalculatorOp::Op38))lhs=lhs!=0&&rhs!=0;
     else if(opcode==uint8_t(CalculatorOp::Op39))lhs=lhs!=0||rhs!=0;
     else lhs=(lhs!=0)!=(rhs!=0);
     stack.back().kind=logic&&integers?2:1;
    }else if(opcode==uint8_t(CalculatorOp::Op41)){
     if(stack.empty())return false;
     if(stack.back().kind==1)
      stack.back().number=stack.back().number==0?1:0;
     else if(stack.back().kind==2){
      const double n=stack.back().number;
      if(n< -2147483648.0||n>2147483647.0)return false;
      const uint32_t bits=~uint32_t(int32_t(n));
      stack.back().number=bits>=0x80000000u?
           double(int64_t(bits)-4294967296LL):double(bits);
     }else return false;
    }else return false;
    // Empty intermediary stacks are valid (for example after pop).
    if(stack.size()>64)return false;
    if(!stack.empty()&&(!std::isfinite(stack.back().number)||
       std::abs(stack.back().number)>1e12))return false;
   }
   if(stack.size()!=outputs)return false;
   for(const CalcValue& value:stack)
    if(value.kind==1||value.kind==3)return false;
   for(unsigned j=0;j<outputs;++j)result[j]=stack[j].number;
 return true;
}

bool tokenize_calculator_function(const std::vector<uint8_t>& program,
                                  std::vector<std::string>& output) {
   if(program.size()>16384)return false;
   // Lex calculator source as PDF/PostScript tokens. Percent starts a
   // comment through end-of-line; braces delimit procedures even when adjacent.
   std::vector<std::string> tokens;
   std::string token;
   auto emit=[&]()->bool{
    if(token.empty())return true;
    if(token.size()>64||tokens.size()>=4096)return false;
    tokens.push_back(std::move(token));token.clear();
    return true;
   };
   bool comment=false;
   for(uint8_t byte:program){
    const char ch=char(byte);
    if(comment){
     if(ch=='\n'||ch=='\r')comment=false;
     continue;
    }
    if(ch=='%'){
     if(!emit())return false;
     comment=true;continue;
    }
    if(ch=='{'||ch=='}'){
     if(!emit()||tokens.size()>=4096)return false;
     tokens.emplace_back(1,ch);
    }else if(ch==' '||ch=='\t'||ch=='\n'||ch=='\r'||
             ch=='\f'||ch=='\0'){
     if(!emit())return false;
    }else{
     if(byte<33||byte>126||token.size()>=64)return false;
     token.push_back(ch);
    }
   }
   if(!emit())return false;
   if(tokens.size()<2||tokens.front()!="{"||tokens.back()!="}")
    return false;
   tokens.erase(tokens.begin());
   tokens.pop_back();
   output=std::move(tokens);
   return true;
}

bool compile_calculator_function(const std::vector<uint8_t>& source,
                                 CalculatorProgram& output) {
 std::vector<std::string> tokens;
 if(!tokenize_calculator_function(source,tokens))return false;
 std::map<std::string,std::pair<double,uint8_t>> constants;
 for(const auto& token:tokens){
  if(token.empty())return false;
  const bool numeric=token[0]=='+'||token[0]=='-'||token[0]=='.'||
    (token[0]>='0'&&token[0]<='9');
  if(numeric&&!constants.count(token)){
   double value=0;uint8_t kind=0;
   if(!parse_calculator_number(token,value,kind))return false;
   constants.emplace(token,std::make_pair(value,kind));
  }
 }
 // Resolve supported operators once. Braces and booleans are literals.
 const auto& operators=calculator_operator_names();
 std::map<std::string,uint8_t> codes;
 for(const auto& token:tokens){
  if(token=="{"||token=="}"||token=="true"||token=="false"||
     constants.find(token)!=constants.end())continue;
  const auto it=std::find(operators.begin(),operators.end(),token);
  if(it==operators.end())return false;
  codes.emplace(token,uint8_t(it-operators.begin()));
 }
 std::vector<CalculatorInstruction> instructions;
 instructions.reserve(tokens.size());
 for(const auto& token:tokens){
  CalculatorInstruction instruction;
  if(token=="{")instruction.kind=2;
  else if(token=="}")instruction.kind=3;
  else if(token=="true"||token=="false"){
   instruction.kind=1;
   instruction.number=token=="true"?1.0:0.0;
  }else if(const auto hit=constants.find(token);hit!=constants.end()){
   instruction.kind=0;
   instruction.number=hit->second.first;
   instruction.numeric_kind=hit->second.second;
  }else{
   const auto hit=codes.find(token);
   if(hit==codes.end())return false;
   instruction.kind=4;
   instruction.opcode=hit->second;
  }
  instructions.push_back(instruction);
 }
 // Resolve matching braces once and reject malformed nested procedures.
 std::vector<size_t> openings;
 for(size_t i=0;i<instructions.size();++i){
  if(instructions[i].kind==2){
   if(openings.size()>=8)return false;
   openings.push_back(i);
  }else if(instructions[i].kind==3){
   if(openings.empty())return false;
   const size_t start=openings.back();openings.pop_back();
   instructions[start].matching_brace=i;
   instructions[i].matching_brace=start;
  }
 }
 if(!openings.empty())return false;
 output.instructions=std::move(instructions);
 return true;
}
bool evaluate_calculator_function(const CalculatorProgram& program,
                                  double input,unsigned outputs,double* result) {
 return evaluate_calculator_tokens({},input,outputs,result,
                                   nullptr,nullptr,&program.instructions);
}

} // namespace rinpdf::parser
