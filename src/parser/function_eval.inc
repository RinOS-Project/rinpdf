// PDF Function evaluator (Reader member definition).
// Included in Reader until its resolver dependencies are extracted.
 bool eval_tint_function(const Value& reference,double x,unsigned outputs,
                         double* result,unsigned depth=0) const {
  if(depth>=8||outputs<1||outputs>4||!std::isfinite(x))return false;
  // Image tint inputs are 8-bit values. Memoize only exact byte-grid
  // evaluations; arbitrary floating-point inputs retain full precision.
  bool cacheable=false;
  unsigned byte_input=0;
  if(reference.kind==Value::Kind::Reference&&x>=0&&x<=1){
   const double quantized=std::round(x*255.0);
   if(x==quantized/255.0){
    cacheable=true;byte_input=unsigned(quantized);
    const auto key=std::make_tuple(reference.integer,
                                  reference.generation,outputs,byte_input);
    const auto hit=calculator_result_cache.find(key);
    if(hit!=calculator_result_cache.end()){
     for(unsigned j=0;j<outputs;++j)result[j]=hit->second[j];
     return true;
    }
   }
  }
  Value function;
  StreamRange sampled_stream;
  bool has_sampled_stream=false;
  if(reference.kind==Value::Kind::Reference){
   const auto key=std::make_pair(reference.integer,reference.generation);
   const auto cached=function_dictionary_cache.find(key);
   if(cached!=function_dictionary_cache.end()){
    function=cached->second.dictionary;
    has_sampled_stream=cached->second.is_stream;
    if(has_sampled_stream)sampled_stream=cached->second.stream;
   }else{
    const XrefEntry* stream_entry=nullptr;
    if(entry(reference,stream_entry)&&
       locate_indirect_stream(data,size,*stream_entry,xref,sampled_stream)){
     function=sampled_stream.dictionary;
     has_sampled_stream=true;
    }else if(!object(reference,function))return false;
    if(function_dictionary_cache.size()<128){
     CachedFunction parsed;
     parsed.dictionary=function;
     parsed.is_stream=has_sampled_stream;
     if(has_sampled_stream)parsed.stream=sampled_stream;
     function_dictionary_cache.emplace(key,std::move(parsed));
    }
   }
  }else function=reference;
  const Value* type=get(function,"FunctionType");
  const Value* domain=get(function,"Domain");
  if(!type||type->kind!=Value::Kind::Integer||
     !domain||domain->kind!=Value::Kind::Array||
     domain->array.size()!=2)return false;
  auto number=[](const Value& v,double& out){
   if(v.kind==Value::Kind::Integer)out=double(v.integer);
   else if(v.kind==Value::Kind::Real)out=v.real;
   else return false;
   return std::isfinite(out)&&std::abs(out)<=100000;
  };
  double low=0,high=0;
  if(!number(domain->array[0],low)||!number(domain->array[1],high)||
     high<=low)return false;
  x=std::clamp(x,low,high);
  if(type->integer==0){
   // Sampled subfunctions inside stitching functions have one input.
   // Decode packed samples and interpolate the two neighboring entries.
   if(!has_sampled_stream)return false;
   const Value* sizes=get(function,"Size");
   const Value* bits=get(function,"BitsPerSample");
   const Value* encode=get(function,"Encode");
   const Value* decode=get(function,"Decode");
   const Value* range=get(function,"Range");
   if(!sizes||sizes->kind!=Value::Kind::Array||sizes->array.size()!=1||
      sizes->array[0].kind!=Value::Kind::Integer||
      sizes->array[0].integer<2||sizes->array[0].integer>4096||
      !bits||bits->kind!=Value::Kind::Integer||
      (bits->integer!=1&&bits->integer!=2&&bits->integer!=4&&
       bits->integer!=8&&bits->integer!=12&&bits->integer!=16&&
       bits->integer!=24&&bits->integer!=32)||
      !range||range->kind!=Value::Kind::Array||
      range->array.size()!=2*outputs)return false;
   const size_t count=size_t(sizes->array[0].integer);
   const unsigned bps=unsigned(bits->integer);
   const size_t bytes=(count*outputs*bps+7)/8;
   if(bytes>4*1024*1024)return false;
   // Keep decoded sample grids across evaluations, including non-byte-
   // aligned inputs reached through stitching functions.
   if(reference.kind!=Value::Kind::Reference)return false;
   const auto sample_key=std::make_pair(reference.integer,reference.generation);
   const std::vector<uint8_t>* samples=nullptr;
   std::vector<uint8_t> uncached_samples;
   auto hit=sampled_function_cache.find(sample_key);
   if(hit!=sampled_function_cache.end()){
    if(hit->second.size()!=bytes)return false;
    samples=&hit->second;
   }else{
    if(!decode_stream(data,size,sampled_stream,uncached_samples,bytes)||
       uncached_samples.size()!=bytes)return false;
    if(bytes<=8*1024*1024-sampled_function_cache_bytes&&
       sampled_function_cache.size()<64){
     sampled_function_cache_bytes+=bytes;
     auto inserted=sampled_function_cache.emplace(
       sample_key,std::move(uncached_samples));
     samples=&inserted.first->second;
    }else samples=&uncached_samples;
   }
   CachedSample local_params;
   const CachedSample* params=nullptr;
   const auto param_key=std::make_tuple(reference.integer,
                                        reference.generation,outputs);
   const auto found=sample_parameter_cache.find(param_key);
   if(found!=sample_parameter_cache.end()){
    params=&found->second;
   }else{
    local_params.count=count;
    local_params.bits=bps;
    local_params.encode[0]=0;
    local_params.encode[1]=double(count-1);
    if(encode){
     if(encode->kind!=Value::Kind::Array||encode->array.size()!=2||
        !number(encode->array[0],local_params.encode[0])||
        !number(encode->array[1],local_params.encode[1]))return false;
    }
    if(decode&&(decode->kind!=Value::Kind::Array||
                decode->array.size()!=outputs*2))return false;
    for(unsigned c=0;c<outputs;++c){
     double r0=0,r1=0;
     if(!number(range->array[2*c],r0)||
        !number(range->array[2*c+1],r1)||r0>r1)return false;
     local_params.range[2*c]=r0;
     local_params.range[2*c+1]=r1;
     local_params.decode[2*c]=r0;
     local_params.decode[2*c+1]=r1;
     if(decode&&(!number(decode->array[2*c],local_params.decode[2*c])||
                 !number(decode->array[2*c+1],local_params.decode[2*c+1])))
      return false;
    }
    if(sample_parameter_cache.size()<128)
     params=&sample_parameter_cache.emplace(param_key,local_params).first->second;
    else params=&local_params;
   }
   const double e0=params->encode[0],e1=params->encode[1];
   const double position=std::clamp(e0+(x-low)/(high-low)*(e1-e0),
                                    0.0,double(count-1));
   const size_t index=size_t(position);
   const size_t next=std::min(index+1,count-1);
   const double blend=position-double(index);
   auto read_sample=[&](size_t sample_index)->double{
    const size_t start=sample_index*bps;
    uint64_t value=0;
    for(unsigned bit=0;bit<bps;++bit){
     const size_t p=start+bit;
     value=(value<<1)|(((*samples)[p/8]>>(7-(p%8)))&1u);
    }
    return double(value)/double((uint64_t(1)<<bps)-1);
   };
   for(unsigned c=0;c<outputs;++c){
    const double r0=params->range[2*c],r1=params->range[2*c+1];
    const double d0=params->decode[2*c],d1=params->decode[2*c+1];
    const double sample=(1-blend)*read_sample(index*outputs+c)+
                        blend*read_sample(next*outputs+c);
    result[c]=std::clamp(d0+sample*(d1-d0),r0,r1);
   }
  }else if(type->integer==4){
   // Bounded numeric calculator with executable procedures and branches.
   // Unsupported operators and malformed procedure nesting fail closed.
   if(!has_sampled_stream)return false;
   const Value* output_range=get(function,"Range");
   if(!output_range||output_range->kind!=Value::Kind::Array||
      output_range->array.size()!=outputs*2)return false;
   // Cache bounded decoded calculator programs for this document so image
   // pixels do not repeatedly decompress and tokenize the same function.
   if(reference.kind!=Value::Kind::Reference)return false;
   const auto cache_key=std::make_pair(reference.integer,reference.generation);
   std::vector<std::string> tokens;
   auto cached=calculator_token_cache.find(cache_key);
   if(cached!=calculator_token_cache.end()){
    tokens=cached->second;
   }else{
    std::vector<uint8_t> program;
    if(!decode_stream(data,size,sampled_stream,program,16384))return false;
   // Lex calculator source as PDF/PostScript tokens. Percent starts a
   // comment through end-of-line; braces delimit procedures even when adjacent.
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
    if(calculator_token_cache.size()<64)
     calculator_token_cache.emplace(cache_key,tokens);
   }
   struct CalcValue { double number; uint8_t kind; size_t procedure; };
   // kind: 0 real, 1 boolean, 2 integer, 3 procedure.
   std::vector<CalcValue> stack{{x,0,0}};
   std::vector<std::vector<std::string>> procedures;
   size_t operations=0;
   // Each expanded token carries the procedure-call depth at which it runs.
   // This bounds nested execution even when branches are expanded iteratively.
   std::vector<uint8_t> execution_depth(tokens.size(),0);
   for(size_t pc=0;pc<tokens.size();++pc){
    token=tokens[pc];
    if(++operations>4096||token.size()>64)return false;
    const uint8_t active_depth=execution_depth[pc];
    if(token=="{"){
     size_t nested=1;
     std::vector<std::string> procedure;
     for(++pc;pc<tokens.size();++pc){
      if(tokens[pc]=="{"){
       if(++nested>8)return false;
      }else if(tokens[pc]=="}"){
       if(--nested==0)break;
      }
      procedure.push_back(tokens[pc]);
     }
     if(nested||procedures.size()>=64||procedure.size()>4096)return false;
     if(stack.size()>=64)return false;
     procedures.push_back(std::move(procedure));
     stack.push_back({0,3,procedures.size()});continue;
    }
    if(token=="}")return false;
    if(token=="if"||token=="ifelse"){
     const size_t required=token=="if"?1:2;
     if(stack.size()<required+1||
        stack[stack.size()-required-1].kind!=1)return false;
     for(size_t j=0;j<required;++j)
      if(stack[stack.size()-required+j].kind!=3)return false;
     const bool condition=stack[stack.size()-required-1].number!=0;
     const size_t chosen_id=required==1?stack.back().procedure:
       stack[stack.size()-(condition?2:1)].procedure;
     if(chosen_id==0||chosen_id>procedures.size())return false;
     const std::vector<std::string> chosen=(required==1&&!condition)?
       std::vector<std::string>{}:procedures[chosen_id-1];
     for(size_t j=0;j<required+1;++j){
      stack.pop_back();
     }
     if(!chosen.empty()){
      if(active_depth>=8||chosen.size()>4096-tokens.size())return false;
      tokens.insert(tokens.begin()+pc+1,chosen.begin(),chosen.end());
      execution_depth.insert(execution_depth.begin()+pc+1,
                             chosen.size(),uint8_t(active_depth+1));
     }
     continue;
    }
    // Calculator numbers support signed decimals, exponent notation and
    // PostScript radix integers (base#digits). Invalid literals fail closed.
    const bool numeric_start=token[0]=='+'||token[0]=='-'||
      token[0]=='.'||(token[0]>='0'&&token[0]<='9');
    if(numeric_start){
     const char* first=token.data();
     const char* last=first+token.size();
     bool negative=false;
     if(*first=='+'||*first=='-'){
      negative=*first=='-';++first;
     }
     if(first==last)return false;
     double value=0;
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
     if(stack.size()>=64)return false;
     stack.push_back({value,uint8_t(integer_literal?2:0),0});
     continue;
    }
    if(token=="true"||token=="false"){
     if(stack.size()>=64)return false;
     stack.push_back({token=="true"?1.0:0.0,1,0});continue;
    }
    if(token=="copy"){
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
    if(token=="index"){
     if(stack.empty()||stack.back().kind!=2)return false;
     const double n=stack.back().number;stack.pop_back();
     if(n<0||n!=std::floor(n)||n>=double(stack.size())||
        stack.size()>=64)return false;
     stack.push_back(stack[stack.size()-1-size_t(n)]);
     continue;
    }
    if(token=="roll"){
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
    if(token=="dup"){
     if(stack.empty()||stack.size()>=64)return false;
     stack.push_back(stack.back());continue;
    }
    if(token=="exch"){
     if(stack.size()<2)return false;
     std::swap(stack[stack.size()-1],stack[stack.size()-2]);
     continue;
    }
    if(token=="pop"){
     if(stack.empty())return false;
     stack.pop_back();continue;
    }
    if(token=="cvi"||token=="cvr"||token=="floor"||
       token=="ceiling"||token=="round"||token=="truncate"||
       token=="ln"||token=="log"||token=="sin"||token=="cos"){
     if(stack.empty())return false;
     if(stack.back().kind==1||stack.back().kind==3)return false;
     double& value=stack.back().number;
     if(token=="cvi"){
      if(value< -2147483648.0||value>2147483647.0)return false;
      value=std::trunc(value);
     }else if(token=="truncate")value=std::trunc(value);
     else if(token=="floor")value=std::floor(value);
     else if(token=="ceiling")value=std::ceil(value);
     else if(token=="round")
      value=value<0? -std::floor(-value+0.5):std::floor(value+0.5);
     else if(token=="ln"||token=="log"){
      if(value<=0)return false;
      value=token=="ln"?std::log(value):std::log10(value);
     }else if(token=="sin"||token=="cos"){
      const double radians=value*(3.14159265358979323846/180.0);
      value=token=="sin"?std::sin(radians):std::cos(radians);
     }
     if(!std::isfinite(value)||std::abs(value)>1e12)return false;
     if(token=="cvi"){
      stack.back().kind=2;
     }else if(token=="cvr"||token=="ln"||token=="log"||
              token=="sin"||token=="cos")stack.back().kind=0;
     continue;
    }
    if(token=="idiv"||token=="mod"||token=="exp"||
       token=="atan"||token=="bitshift"){
     if(stack.size()<2)return false;
     if(stack.back().kind==1||stack.back().kind==3||stack[stack.size()-2].kind==1||stack[stack.size()-2].kind==3)return false;
     const bool rhs_integer=stack.back().kind==2;
     const double rhs=stack.back().number;stack.pop_back();
     double& lhs=stack.back().number;
     if(token=="idiv"||token=="mod"){
      if(stack.back().kind!=2||!rhs_integer||rhs==0)return false;
      if(lhs!=std::trunc(lhs)||rhs!=std::trunc(rhs)||
         lhs< -2147483648.0||lhs>2147483647.0||
         rhs< -2147483648.0||rhs>2147483647.0||
         (lhs==-2147483648.0&&rhs==-1))return false;
      if(token=="idiv")lhs=std::trunc(lhs/rhs);
      else lhs=std::fmod(lhs,rhs);
     }else if(token=="exp"){
      if(lhs<0&&rhs!=std::trunc(rhs))return false;
      lhs=std::pow(lhs,rhs);
     }else if(token=="atan"){
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
     stack.back().kind=(token=="idiv"||token=="mod"||token=="bitshift")?2:0;
     continue;
    }
    if(token=="neg"||token=="abs"||token=="sqrt"){
     if(stack.empty())return false;
     if(stack.back().kind==1||stack.back().kind==3)return false;
     double& top=stack.back().number;
     if(stack.back().kind==2&&top==-2147483648.0&&
        (token=="neg"||token=="abs"))return false;
     if(token=="sqrt")stack.back().kind=0;
     if(token=="neg")top=-top;
     else if(token=="abs")top=std::abs(top);
     else{
      if(top<0)return false;
      top=std::sqrt(top);
     }
    }else if(token=="add"||token=="sub"||token=="mul"||
             token=="div"||token=="min"||token=="max"){
     if(stack.size()<2)return false;
     if(stack.back().kind==1||stack.back().kind==3||stack[stack.size()-2].kind==1||stack[stack.size()-2].kind==3)return false;
     const bool integer_pair=stack.back().kind==2&&stack[stack.size()-2].kind==2;
     const double rhs=stack.back().number;stack.pop_back();
     double& lhs=stack.back().number;
     if(token=="add")lhs+=rhs;
     else if(token=="sub")lhs-=rhs;
     else if(token=="mul")lhs*=rhs;
     else if(token=="div"){
      if(rhs==0)return false;
      lhs/=rhs;
     }else if(token=="min")lhs=std::min(lhs,rhs);
     else lhs=std::max(lhs,rhs);
     const bool in_integer_range=std::isfinite(lhs)&&
       lhs>=-2147483648.0&&lhs<=2147483647.0;
     if(integer_pair&&token!="div"&&!in_integer_range)return false;
     const bool keep_integer=integer_pair&&token!="div"&&
       lhs==std::trunc(lhs)&&in_integer_range;
     stack.back().kind=keep_integer?2:0;
    }else if(token=="eq"||token=="ne"||token=="gt"||
             token=="ge"||token=="lt"||token=="le"||
             token=="and"||token=="or"||token=="xor"){
     if(stack.size()<2)return false;
     if(stack.back().kind==3||stack[stack.size()-2].kind==3)return false;
     const bool lb=stack[stack.size()-2].kind==1;
     const bool rb=stack.back().kind==1;
     const bool logic=token=="and"||token=="or"||token=="xor";
     const bool equality=token=="eq"||token=="ne";
     const bool integers=stack.back().kind==2&&
                         stack[stack.size()-2].kind==2;
     if((logic&&!((lb&&rb)||integers))||
        (equality&&lb!=rb)||
        (!logic&&!equality&&(lb||rb)))return false;
     const double rhs=stack.back().number;stack.pop_back();
     double& lhs=stack.back().number;
     if(token=="eq")lhs=lhs==rhs;
     else if(token=="ne")lhs=lhs!=rhs;
     else if(token=="gt")lhs=lhs>rhs;
     else if(token=="ge")lhs=lhs>=rhs;
     else if(token=="lt")lhs=lhs<rhs;
     else if(token=="le")lhs=lhs<=rhs;
     else if(integers){
      if(lhs< -2147483648.0||lhs>2147483647.0||
         rhs< -2147483648.0||rhs>2147483647.0)return false;
      const uint32_t a=uint32_t(int32_t(lhs));
      const uint32_t b=uint32_t(int32_t(rhs));
      const uint32_t bits=token=="and"?(a&b):
                          token=="or"?(a|b):(a^b);
      lhs=bits>=0x80000000u?double(int64_t(bits)-4294967296LL):
                             double(bits);
     }else if(token=="and")lhs=lhs!=0&&rhs!=0;
     else if(token=="or")lhs=lhs!=0||rhs!=0;
     else lhs=(lhs!=0)!=(rhs!=0);
     stack.back().kind=logic&&integers?2:1;
    }else if(token=="not"){
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
  }else if(type->integer==2){
   CachedExponent local;
   const CachedExponent* compiled=nullptr;
   const bool indirect=reference.kind==Value::Kind::Reference;
   const auto key=std::make_tuple(reference.integer,reference.generation,
                                  outputs);
   if(indirect){
    const auto hit=exponent_function_cache.find(key);
    if(hit!=exponent_function_cache.end())compiled=&hit->second;
   }
   if(!compiled){
    const Value* n=get(function,"N");
    if(!n||!number(*n,local.exponent)||
       local.exponent<=0||local.exponent>100)return false;
    for(const auto& [name,dest]:
       std::initializer_list<std::pair<const char*,std::array<double,4>*>>{
        {"C0",&local.c0},{"C1",&local.c1}}){
     if(const Value* values=get(function,name)){
      if(values->kind!=Value::Kind::Array||
         values->array.size()!=outputs)return false;
      for(unsigned j=0;j<outputs;++j)
       if(!number(values->array[j],(*dest)[j]))return false;
     }
    }
    if(indirect&&exponent_function_cache.size()<128)
     compiled=&exponent_function_cache.emplace(key,std::move(local)).first->second;
    else compiled=&local;
   }
   const double powered=std::pow(x,compiled->exponent);
   if(!std::isfinite(powered))return false;
   for(unsigned j=0;j<outputs;++j){
    result[j]=compiled->c0[j]+powered*
              (compiled->c1[j]-compiled->c0[j]);
    if(!std::isfinite(result[j]))return false;
   }
  }else if(type->integer==3){
   CachedStitch local;
   const CachedStitch* compiled=nullptr;
   const bool indirect=reference.kind==Value::Kind::Reference;
   const auto key=std::make_pair(reference.integer,reference.generation);
   if(indirect){
    const auto hit=stitch_function_cache.find(key);
    if(hit!=stitch_function_cache.end())compiled=&hit->second;
   }
   if(!compiled){
    const Value* functions=get(function,"Functions");
    const Value* bounds=get(function,"Bounds");
    const Value* enc=get(function,"Encode");
    if(!functions||functions->kind!=Value::Kind::Array||
       functions->array.empty()||functions->array.size()>32||
       !bounds||bounds->kind!=Value::Kind::Array||
       bounds->array.size()!=functions->array.size()-1||
       !enc||enc->kind!=Value::Kind::Array||
       enc->array.size()!=functions->array.size()*2)return false;
    local.functions=functions->array;
    local.bounds.push_back(low);
    double previous=low;
    for(const Value& v:bounds->array){
     double boundary=0;
     if(!number(v,boundary)||boundary<=previous||boundary>=high)
      return false;
     local.bounds.push_back(boundary);
     previous=boundary;
    }
    local.bounds.push_back(high);
    for(const Value& v:enc->array){
     double coordinate=0;
     if(!number(v,coordinate))return false;
     local.encode.push_back(coordinate);
    }
    if(indirect&&stitch_function_cache.size()<128)
     compiled=&stitch_function_cache.emplace(key,std::move(local)).first->second;
    else compiled=&local;
   }
   const auto it=std::upper_bound(compiled->bounds.begin()+1,
                                  compiled->bounds.end()-1,x);
   const size_t segment=size_t(it-(compiled->bounds.begin()+1));
   if(segment>=compiled->functions.size())return false;
   const double left=compiled->bounds[segment];
   const double right=compiled->bounds[segment+1];
   const double e0=compiled->encode[2*segment];
   const double e1=compiled->encode[2*segment+1];
   const double mapped=e0+(x-left)/(right-left)*(e1-e0);
   if(!eval_tint_function(compiled->functions[segment],mapped,outputs,
                          result,depth+1))return false;
  }else return false;
  if(const Value* range=get(function,"Range")){
   if(range->kind!=Value::Kind::Array||range->array.size()!=outputs*2)
    return false;
   for(unsigned j=0;j<outputs;++j){
    double a=0,b=0;
    if(!number(range->array[2*j],a)||
       !number(range->array[2*j+1],b)||a>b)return false;
    result[j]=std::clamp(result[j],a,b);
   }
  }
  if(cacheable&&calculator_result_cache.size()<16384){
   std::array<double,4> saved{};
   for(unsigned j=0;j<outputs;++j)saved[j]=result[j];
   calculator_result_cache.emplace(
      std::make_tuple(reference.integer,reference.generation,
                      outputs,byte_input),saved);
  }
  return true;
 }
