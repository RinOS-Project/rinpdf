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
   SampledFunction sampled;
   sampled.count=params->count;
   sampled.bits=params->bits;
   sampled.outputs=outputs;
   sampled.domain[0]=low;
   sampled.domain[1]=high;
   sampled.encode[0]=params->encode[0];
   sampled.encode[1]=params->encode[1];
   sampled.decode=params->decode;
   sampled.range=params->range;
   if(!evaluate_sampled_function(sampled,*samples,x,result))return false;
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
    if(!tokenize_calculator_function(program,tokens))return false;
    if(calculator_token_cache.size()<64)
     calculator_token_cache.emplace(cache_key,tokens);
   }
   if(!evaluate_calculator_tokens(tokens,x,outputs,result))return false;
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
   if(!evaluate_exponential_function(*compiled,x,outputs,result))
    return false;
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
   size_t segment=0;
   double mapped=0;
   if(!map_stitching_input(compiled->bounds,compiled->encode,
                           x,segment,mapped)||
      segment>=compiled->functions.size())return false;
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
