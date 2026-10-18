// PDF Function evaluator (Reader member definition).
// Included in Reader until its resolver dependencies are extracted.
 // Compile one-input PDF functions recursively for shared evaluation.
 bool compile_tint_tree(const Value& ref,unsigned outputs,PdfFunction& out,
                        unsigned depth=0) const {
  if(depth>=8||outputs<1||outputs>4)return false;
  Value dictionary;
  StreamRange stream_range;
  bool is_stream=false;
  if(ref.kind==Value::Kind::Reference){
   const XrefEntry* e=nullptr;
   if(entry(ref,e)&&locate_indirect_stream(data,size,*e,xref,stream_range)){
    dictionary=stream_range.dictionary;is_stream=true;
   }else if(!object(ref,dictionary))return false;
  }else dictionary=ref;
  const Value* type=get(dictionary,"FunctionType");
  const Value* domain=get(dictionary,"Domain");
  if(!type||type->kind!=Value::Kind::Integer||
     !domain||domain->kind!=Value::Kind::Array||domain->array.size()!=2)
   return false;
  auto numeric=[](const Value& v,double& d)->bool{
   if(v.kind==Value::Kind::Integer)d=double(v.integer);
   else if(v.kind==Value::Kind::Real)d=v.real;
   else return false;
   return std::isfinite(d)&&std::abs(d)<=100000;
  };
  double d0=0,d1=0;
  if(!numeric(domain->array[0],d0)||!numeric(domain->array[1],d1)||
     d0>=d1)return false;
  std::array<double,8> limits{};
  if(const Value* range=get(dictionary,"Range")){
   if(range->kind!=Value::Kind::Array||range->array.size()!=outputs*2)
    return false;
   for(unsigned i=0;i<outputs;++i)
    if(!numeric(range->array[i*2],limits[i*2])||
       !numeric(range->array[i*2+1],limits[i*2+1])||
       limits[i*2]>limits[i*2+1])return false;
  }else{
   if(type->integer==0||type->integer==4)return false;
   for(unsigned i=0;i<outputs;++i){
    limits[i*2]=-100000;limits[i*2+1]=100000;
   }
  }
  if(type->integer==2){
   ExponentialFunction p;
   const Value* n=get(dictionary,"N");
   if(!n||!numeric(*n,p.exponent)||p.exponent<=0||p.exponent>100)
    return false;
   for(const auto& setting:
       std::initializer_list<std::pair<const char*,std::array<double,4>*>>{
        {"C0",&p.c0},{"C1",&p.c1}}){
    if(const Value* a=get(dictionary,setting.first)){
     if(a->kind!=Value::Kind::Array||a->array.size()!=outputs)return false;
     for(unsigned i=0;i<outputs;++i)
      if(!numeric(a->array[i],(*setting.second)[i]))return false;
    }
   }
   return compile_exponential_pdf_function(p,outputs,d0,d1,limits,out);
  }
  if(type->integer==4){
   if(!is_stream)return false;
   std::vector<uint8_t> bytes;
   return decode_stream(data,size,stream_range,bytes,16384)&&
     compile_calculator_pdf_function(bytes,outputs,d0,d1,limits,out);
  }
  if(type->integer==0){
   if(!is_stream)return false;
   const Value* sz=get(dictionary,"Size");
   const Value* bp=get(dictionary,"BitsPerSample");
   if(!sz||sz->kind!=Value::Kind::Array||sz->array.size()!=1||
      sz->array[0].kind!=Value::Kind::Integer||
      sz->array[0].integer<2||sz->array[0].integer>4096||
      !bp||bp->kind!=Value::Kind::Integer)return false;
   SampledFunction p;
   p.count=size_t(sz->array[0].integer);p.bits=unsigned(bp->integer);
   if(p.bits!=1&&p.bits!=2&&p.bits!=4&&p.bits!=8&&p.bits!=12&&
      p.bits!=16&&p.bits!=24&&p.bits!=32)return false;
   p.outputs=outputs;p.domain[0]=d0;p.domain[1]=d1;
   p.range=limits;p.decode=limits;p.encode[1]=double(p.count-1);
   if(const Value* enc=get(dictionary,"Encode")){
    if(enc->kind!=Value::Kind::Array||enc->array.size()!=2||
       !numeric(enc->array[0],p.encode[0])||
       !numeric(enc->array[1],p.encode[1]))return false;
   }
   if(const Value* dec=get(dictionary,"Decode")){
    if(dec->kind!=Value::Kind::Array||dec->array.size()!=outputs*2)
     return false;
    for(unsigned i=0;i<outputs*2;++i)
     if(!numeric(dec->array[i],p.decode[i]))return false;
   }
   const size_t bytes=(p.count*outputs*p.bits+7)/8;
   if(bytes>4*1024*1024)return false;
   std::vector<uint8_t> samples;
   return decode_stream(data,size,stream_range,samples,bytes)&&
     samples.size()==bytes&&compile_sampled_pdf_function(p,samples,out);
  }
  if(type->integer==3){
   const Value* funcs=get(dictionary,"Functions");
   const Value* bounds=get(dictionary,"Bounds");
   const Value* encode=get(dictionary,"Encode");
   if(!funcs||funcs->kind!=Value::Kind::Array||
      funcs->array.empty()||funcs->array.size()>32||
      !bounds||bounds->kind!=Value::Kind::Array||
      bounds->array.size()!=funcs->array.size()-1||
      !encode||encode->kind!=Value::Kind::Array||
      encode->array.size()!=funcs->array.size()*2)return false;
   std::vector<double> locations{d0},mapped;
   for(const Value& v:bounds->array){
    double x=0;
    if(!numeric(v,x)||x<=locations.back()||x>=d1)return false;
    locations.push_back(x);
   }
   locations.push_back(d1);
   for(const Value& v:encode->array){
    double x=0;if(!numeric(v,x))return false;mapped.push_back(x);
   }
   std::vector<PdfFunction> children;
   for(const Value& child:funcs->array){
    PdfFunction compiled;
    if(!compile_tint_tree(child,outputs,compiled,depth+1))return false;
    children.push_back(std::move(compiled));
   }
   return compile_stitching_pdf_function(children,locations,mapped,
                                         outputs,d0,d1,limits,out);
  }
  return false;
 }
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
   if(!has_sampled_stream||reference.kind!=Value::Kind::Reference)return false;
   const auto key=std::make_tuple(reference.integer,reference.generation,outputs);
   PdfFunction local;
   const PdfFunction* compiled=nullptr;
   const auto hit=compiled_sample_cache.find(key);
   if(hit!=compiled_sample_cache.end())compiled=&hit->second;
   else{
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
       range->array.size()!=outputs*2)return false;
    SampledFunction parameters;
    parameters.count=size_t(sizes->array[0].integer);
    parameters.bits=unsigned(bits->integer);
    parameters.outputs=outputs;
    parameters.domain[0]=low;parameters.domain[1]=high;
    parameters.encode[0]=0;
    parameters.encode[1]=double(parameters.count-1);
    if(encode){
     if(encode->kind!=Value::Kind::Array||encode->array.size()!=2||
        !number(encode->array[0],parameters.encode[0])||
        !number(encode->array[1],parameters.encode[1]))return false;
    }
    if(decode&&(decode->kind!=Value::Kind::Array||
                decode->array.size()!=outputs*2))return false;
    for(unsigned j=0;j<outputs;++j){
     if(!number(range->array[2*j],parameters.range[2*j])||
        !number(range->array[2*j+1],parameters.range[2*j+1])||
        parameters.range[2*j]>parameters.range[2*j+1])return false;
     parameters.decode[2*j]=parameters.range[2*j];
     parameters.decode[2*j+1]=parameters.range[2*j+1];
     if(decode&&(!number(decode->array[2*j],parameters.decode[2*j])||
                 !number(decode->array[2*j+1],parameters.decode[2*j+1])))
      return false;
    }
    const size_t byte_count=(parameters.count*outputs*parameters.bits+7)/8;
    if(byte_count>4*1024*1024)return false;
    std::vector<uint8_t> samples;
    if(!decode_stream(data,size,sampled_stream,samples,byte_count)||
       samples.size()!=byte_count||
       !compile_sampled_pdf_function(parameters,samples,local))return false;
    if(compiled_sample_cache.size()<64)
     compiled=&compiled_sample_cache.emplace(key,std::move(local)).first->second;
    else compiled=&local;
   }
   if(!evaluate_pdf_function(*compiled,x,result))return false;
  }else if(type->integer==4){
   // Compile Type 4 once per object and output arity, then reuse the
   // common PdfFunction evaluation path across all image pixels.
   if(!has_sampled_stream||reference.kind!=Value::Kind::Reference)
    return false;
   const Value* output_range=get(function,"Range");
   if(!output_range||output_range->kind!=Value::Kind::Array||
      output_range->array.size()!=outputs*2)return false;
   const auto key=std::make_tuple(reference.integer,reference.generation,outputs);
   const PdfFunction* compiled=nullptr;
   PdfFunction local;
   const auto hit=compiled_calculator_cache.find(key);
   if(hit!=compiled_calculator_cache.end()){
    compiled=&hit->second;
   }else{
    std::array<double,8> limits{};
    for(unsigned j=0;j<outputs*2;++j)
     if(!number(output_range->array[j],limits[j]))return false;
    std::vector<uint8_t> source;
    if(!decode_stream(data,size,sampled_stream,source,16384)||
       !compile_calculator_pdf_function(source,outputs,low,high,
                                        limits,local))return false;
    if(compiled_calculator_cache.size()<64)
     compiled=&compiled_calculator_cache.emplace(key,std::move(local)).first->second;
    else compiled=&local;
   }
   if(!evaluate_pdf_function(*compiled,x,result))return false;
  }else if(type->integer==2){
   PdfFunction local;
   const PdfFunction* compiled=nullptr;
   const bool indirect=reference.kind==Value::Kind::Reference;
   const auto key=std::make_tuple(reference.integer,reference.generation,
                                  outputs);
   if(indirect){
    const auto hit=compiled_exponent_cache.find(key);
    if(hit!=compiled_exponent_cache.end())compiled=&hit->second;
   }
   if(!compiled){
    ExponentialFunction parameters;
    const Value* exponent=get(function,"N");
    if(!exponent||!number(*exponent,parameters.exponent)||
       parameters.exponent<=0||parameters.exponent>100)return false;
    for(const auto& [name,dest]:
        std::initializer_list<std::pair<const char*,std::array<double,4>*>>{
          {"C0",&parameters.c0},{"C1",&parameters.c1}}){
     if(const Value* entries=get(function,name)){
      if(entries->kind!=Value::Kind::Array||
         entries->array.size()!=outputs)return false;
      for(unsigned j=0;j<outputs;++j)
       if(!number(entries->array[j],(*dest)[j]))return false;
     }
    }
    std::array<double,8> limits{};
    if(const Value* range=get(function,"Range")){
     if(range->kind!=Value::Kind::Array||range->array.size()!=outputs*2)
      return false;
     for(unsigned j=0;j<outputs*2;++j)
      if(!number(range->array[j],limits[j]))return false;
    }else{
     for(unsigned j=0;j<outputs;++j){
      limits[2*j]=-100000;limits[2*j+1]=100000;
     }
    }
    if(!compile_exponential_pdf_function(parameters,outputs,low,high,
                                         limits,local))return false;
    if(indirect&&compiled_exponent_cache.size()<128)
     compiled=&compiled_exponent_cache.emplace(key,std::move(local)).first->second;
    else compiled=&local;
   }
   if(!evaluate_pdf_function(*compiled,x,result))return false;
  }else if(type->integer==3){
   PdfFunction local;
   const PdfFunction* compiled=nullptr;
   const bool indirect=reference.kind==Value::Kind::Reference;
   const auto key=std::make_tuple(reference.integer,reference.generation,outputs);
   if(indirect){
    const auto hit=compiled_stitch_cache.find(key);
    if(hit!=compiled_stitch_cache.end())compiled=&hit->second;
   }
   if(!compiled){
    if(!compile_tint_tree(reference,outputs,local,depth))return false;
    if(indirect&&compiled_stitch_cache.size()<64)
     compiled=&compiled_stitch_cache.emplace(key,std::move(local)).first->second;
    else compiled=&local;
   }
   if(!evaluate_pdf_function(*compiled,x,result))return false;
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
