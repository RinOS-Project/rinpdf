#include "stream.h"
#include "flate.h"
#include <cstring>
#include <utility>
#include <algorithm>
#include <string>
#include <cmath>
#include <cstdint>
namespace rinpdf::parser {
static bool space(unsigned char c){return c==0||c==9||c==10||c==12||c==13||c==32;}
static bool keyword(const uint8_t *data,size_t size,size_t pos,const char *word,size_t len){
 return pos<=size&&len<=size-pos&&std::memcmp(data+pos,word,len)==0&&(pos+len==size||space(data[pos+len]));
}
bool locate_stream(const uint8_t *data,size_t size,StreamRange& out){
 out=StreamRange{};if(!data||size==0)return false;
 Lexer lexer(data,size);Value dict;
 if(!parse_value(lexer,dict)||dict.kind!=Value::Kind::Dictionary)return false;
 auto it=dict.dictionary.find("Length");
 if(it==dict.dictionary.end()||it->second.kind!=Value::Kind::Integer||it->second.integer<0)return false;
 auto amount=(unsigned long long)it->second.integer;
 size_t pos=lexer.position();
 while(pos<size&&space(data[pos]))++pos;
 if(!keyword(data,size,pos,"stream",6))return false;
 pos+=6;
 if(pos==size)return false;
 if(data[pos]=='\r'){++pos;if(pos<size&&data[pos]=='\n')++pos;}
 else if(data[pos]=='\n')++pos;
 else return false;
 if(amount>size-pos)return false;
 size_t end=pos+(size_t)amount;
 size_t marker=end;
 // The declared stream length must end before the separating EOL.
 // Otherwise a length that includes the delimiter is silently accepted.
 if(marker<size&&data[marker]=='\r'){++marker;if(marker<size&&data[marker]=='\n')++marker;}
 else if(marker<size&&data[marker]=='\n')++marker;
 else return false;
 if(!keyword(data,size,marker,"endstream",9))return false;
 marker+=9;
 while(marker<size&&space(data[marker]))++marker;
 if(marker!=size)return false;
 out.dictionary=std::move(dict);out.offset=pos;out.length=(size_t)amount;
 return true;
}
static int hex_digit(unsigned char c){
 if(c>='0'&&c<='9')return c-'0';
 if(c>='a'&&c<='f')return c-'a'+10;
 if(c>='A'&&c<='F')return c-'A'+10;
 return -1;
}
static bool decode_hex(const uint8_t* input,size_t length,
 std::vector<uint8_t>& output,size_t limit){
 output.clear();
 int nibble=-1;
 bool terminated=false;
 for(size_t i=0;i<length;++i){
  const unsigned char c=input[i];
  if(terminated){if(!space(c))return false;continue;}
  if(space(c))continue;
  if(c=='>'){
   terminated=true;
   if(nibble>=0){
    if(output.size()>=limit)return false;
    output.push_back(uint8_t(nibble<<4));
    nibble=-1;
   }
   continue;
  }
  const int digit=hex_digit(c);
  if(digit<0)return false;
  if(nibble<0)nibble=digit;
  else{
   if(output.size()>=limit)return false;
   output.push_back(uint8_t((nibble<<4)|digit));
   nibble=-1;
  }
 }
 return terminated;
}
static bool decode_ascii85(const uint8_t* input,size_t length,
 std::vector<uint8_t>& output,size_t limit){
 output.clear();
 uint64_t accumulator=0;
 unsigned digits=0;
 bool terminated=false;
 for(size_t i=0;i<length;++i){
  const unsigned char c=input[i];
  if(terminated){if(!space(c))return false;continue;}
  if(space(c))continue;
  if(c=='~'){
   if(i+1>=length||input[i+1]!='>')return false;
   ++i;terminated=true;
   if(digits==1)return false;
   if(digits){
    const unsigned count=digits;
    while(digits++<5)accumulator=accumulator*85+84;
    if(accumulator>0xffffffffULL)return false;
    if(output.size()>limit||count-1>limit-output.size())return false;
    for(unsigned j=0;j<count-1;++j)
     output.push_back(uint8_t(accumulator>>(24-8*j)));
   }
   continue;
  }
  if(c=='z'){
   if(digits||output.size()>limit||limit-output.size()<4)return false;
   output.insert(output.end(),4,0);continue;
  }
  if(c<'!'||c>'u')return false;
  accumulator=accumulator*85+(c-'!');
  if(++digits==5){
   if(accumulator>0xffffffffULL||output.size()>limit||
      limit-output.size()<4)return false;
   for(int shift=24;shift>=0;shift-=8)
    output.push_back(uint8_t(accumulator>>shift));
   accumulator=0;digits=0;
  }
 }
 return terminated;
}
static bool decode_run_length(const uint8_t* input,size_t length,
 std::vector<uint8_t>& output,size_t limit){
 output.clear();
 size_t i=0;
 while(i<length){
  const unsigned control=input[i++];
  if(control==128){
   while(i<length)if(!space(input[i++]))return false;
   return true;
  }
  if(control<=127){
   const size_t count=control+1;
   if(count>length-i||output.size()>limit||
      count>limit-output.size())return false;
   output.insert(output.end(),input+i,input+i+count);
   i+=count;
  }else{
   const size_t count=257-control;
   if(i>=length||output.size()>limit||
      count>limit-output.size())return false;
   output.insert(output.end(),count,input[i++]);
  }
 }
 return false;
}
static bool decode_lzw(const uint8_t* input,size_t length,
 std::vector<uint8_t>& output,size_t limit,unsigned early_change){
 if(early_change>1)return false;
 output.clear();
 struct Entry {int prefix=-1;uint8_t suffix=0;};
 Entry table[4096]{};
 for(int i=0;i<256;++i)table[i]={-1,uint8_t(i)};
 unsigned next=258,bits=9;
 int previous=-1;
 size_t bitpos=0;
 uint8_t word[4096],reverse[4096];
 size_t previous_length=0;
 auto read_code=[&](unsigned& code)->bool{
  if(bitpos>length*8||size_t(bits)>length*8-bitpos)return false;
  code=0;
  for(unsigned k=0;k<bits;++k){
   const size_t bit=bitpos++;
   code=(code<<1)|((input[bit/8]>>(7-(bit%8)))&1);
  }
  return true;
 };
 auto expand=[&](unsigned code,size_t& count)->bool{
  count=0;
  unsigned hops=0;
  while(code>=256){
   if(code>=next||code<258||++hops>4096||count>=4096)return false;
   reverse[count++]=table[code].suffix;
   code=unsigned(table[code].prefix);
  }
  if(count>=4096)return false;
  reverse[count++]=uint8_t(code);
  for(size_t i=0;i<count;++i)word[i]=reverse[count-i-1];
  return true;
 };
 while(true){
  unsigned code=0;
  if(!read_code(code))return false;
  if(code==257)return true;
  if(code==256){
   next=258;bits=9;previous=-1;previous_length=0;
   continue;
  }
  size_t count=0;
  if(code<next){
   if(!expand(code,count))return false;
  }else if(code==next&&previous>=0){
   if(previous_length>=4096)return false;
   if(!expand(unsigned(previous),count)||count>=4096)return false;
   word[count++]=word[0];
  }else return false;
  if(count>limit-output.size())return false;
  output.insert(output.end(),word,word+count);
  if(previous>=0&&next<4096){
   table[next++]={previous,word[0]};
   if(bits<12&&next+early_change==(1u<<bits))++bits;
  }
  previous=int(cstatic bool apply_predictor(std::vector<uint8_t>& bytes,
 const Value& params,size_t limit){
 auto param=[&](const char* name,unsigned fallback,unsigned& result){
  const auto it=params.dictionary.find(name);
  if(it==params.dictionary.end()){result=fallback;return true;}
  if(it->second.kind!=Value::Kind::Integer||it->second.integer<0||
     it->second.integer>1000000)return false;
  result=unsigned(it->second.integer);return true;
 };
 unsigned predictor=1,colors=1,bpc=8,columns=1;
 if(!param("Predictor",1,predictor)||!param("Colors",1,colors)||
    !param("BitsPerComponent",8,bpc)||!param("Columns",1,columns)||
    !colors||!columns||!(bpc==1||bpc==2||bpc==4||bpc==8||bpc==16))
  return false;
 if(predictor==1)return bytes.size()<=limit;
 if(predictor!=2&&(predictor<10||predictor>15))return false;
 const uint64_t row_bits=uint64_t(colors)*columns*bpc;
 const uint64_t pixel_bits=uint64_t(colors)*bpc;
 const uint64_t row64=(row_bits+7)/8;
 const uint64_t bpp64=(pixel_bits+7)/8;
 if(!row64||row64>limit||bpp64>row64)return false;
 const size_t row=size_t(row64),bpp=size_t(bpp64);
 if(predictor==2){
  // TIFF differencing is component-wise. This implementation supports
  // byte-aligned samples; packed samples are rejected rather than corrupted.
  if(bpc!=8)return false;
  if(bytes.size()%row)return false;
  for(size_t offset=0;offset<bytes.size();offset+=row)
   for(size_t i=bpp;i<row;++i)
    bytes[offset+i]=uint8_t(bytes[offset+i]+bytes[offset+i-bpp]);
  return true;
 }
 const size_t encoded_row=row+1;
 if(bytes.size()%encoded_row)return false;
 const size_t rows=bytes.size()/encoded_row;
 if(rows>limit/row)return false;
 std::vector<uint8_t> result(rows*row);
 for(size_t r=0;r<rows;++r){
  const uint8_t filter=bytes[r*encoded_row];
  if(filter>4||(predictor!=15&&filter!=predictor-10))return false;
  for(size_t x=0;x<row;++x){
   const unsigned left=x>=bpp?result[r*row+x-bpp]:0;
   const unsigned up=r?result[(r-1)*row+x]:0;
   const unsigned corner=r&&x>=bpp?result[(r-1)*row+x-bpp]:0;
   unsigned prediction=0;
   if(filter==1)prediction=left;
   else if(filter==2)prediction=up;
   else if(filter==3)prediction=(left+up)/2;
   else if(filter==4){
    const int p=int(left)+int(up)-int(corner);
    const int dl=std::abs(p-int(left)),du=std::abs(p-int(up));
    const int dc=std::abs(p-int(corner));
    prediction=dl<=du&&dl<=dc?left:du<=dc?up:corner;
   }
   result[r*row+x]=uint8_t(bytes[r*encoded_row+1+x]+prediction);
  }
 }
 bytes=std::move(result);
 return true;
}
ode);
  previous_length=count;
 }
}
bool decode_stream(const uint8_t *data,size_t size,const StreamRange& range,std::vector<uint8_t>& output,size_t max_output){
 output.clear();
 if(!data||range.offset>size||range.length>size-range.offset||
    max_output==0)return false;
 const auto filter=range.dictionary.dictionary.find("Filter");
 if(filter==range.dictionary.dictionary.end()){
  if(range.length>max_output)return false;
  output.assign(data+range.offset,data+range.offset+range.length);
  return true;
 }
 std::vector<std::string> filters;
 const Value& value=filter->second;
 if(value.kind==Value::Kind::Name)filters.push_back(value.text);
 else if(value.kind==Value::Kind::Array){
  if(value.array.empty()||value.array.size()>8)return false;
  for(const Value& item:value.array){
   if(item.kind!=Value::Kind::Name)return false;
   filters.push_back(item.text);
  }
 }else return false;
 // DecodeParms may be a dictionary for a single filter or an array
 // aligned with the Filter array. Null entries mean default parameters.
 const auto parms=range.dictionary.dictionary.find("DecodeParms");
 std::vector<const Value*> parameters(filters.size(),nullptr);
 if(parms!=range.dictionary.dictionary.end()){
  const Value& p=parms->second;
  if(p.kind==Value::Kind::Dictionary){
   if(filters.size()!=1)return false;
   parameters[0]=&p;
  }else if(p.kind==Value::Kind::Array){
   if(p.array.size()!=filters.size())return false;
   for(size_t i=0;i<p.array.size();++i){
    if(p.array[i].kind==Value::Kind::Dictionary)
     parameters[i]=&p.array[i];
    else if(p.array[i].kind!=Value::Kind::Null)return false;
   }
  }else if(p.kind!=Value::Kind::Null)return false;
 }
 std::vector<uint8_t> stage(data+range.offset,
                           data+range.offset+range.length);
 if(stage.size()>max_output&&filters.size()==0)return false;
 for(size_t filter_index=0;filter_index<filters.size();++filter_index){
  const std::string& name=filters[filter_index];
  const Value* parameter=parameters[filter_index];
  unsigned early_change=1;
  if(name=="LZWDecode"||name=="LZW"){
   if(parameter){
    if(parameter->kind!=Value::Kind::Dictionary)return false;
    const auto it=parameter->dictionary.find("EarlyChange");
    if(it!=parameter->dictionary.end()){
     if(it->second.kind!=Value::Kind::Integer||
        (it->second.integer!=0&&it->second.integer!=1))return false;
     early_change=unsigned(it->second.integer);
    }
   }
  }
  std::vector<uint8_t> decoded;
  // PNG predictors prepend one selector byte per decoded row.
  // Allow only that bounded overhead before the predictor is removed.
  size_t stage_limit=max_output;
  if(parameter&&
     (name=="FlateDecode"||name=="Fl"||
      name=="LZWDecode"||name=="LZW")&&
     parameter->kind==Value::Kind::Dictionary){
   const auto& d=parameter->dictionary;
   const auto it=d.find("Predictor");
   if(it!=d.end()&&it->second.kind==Value::Kind::Integer&&
      it->second.integer>=10&&it->second.integer<=15){
    if(max_output>SIZE_MAX/2)return false;
    stage_limit=max_output*2;
   }
  }
  const bool ok=name=="FlateDecode"||name=="Fl"?
    flate_decode(stage.data(),stage.size(),decoded,stage_limit):
    (name=="ASCIIHexDecode"||name=="AHx")?
    decode_hex(stage.data(),stage.size(),decoded,stage_limit):
    (name=="ASCII85Decode"||name=="A85")?
    decode_ascii85(stage.data(),stage.size(),decoded,stage_limit):
    (name=="RunLengthDecode"||name=="RL")?
    decode_run_length(stage.data(),stage.size(),decoded,stage_limit):
    (name=="LZWDecode"||name=="LZW")?
    decode_lzw(stage.data(),stage.size(),decoded,stage_limit,early_change):false;
  if(!ok){output.clear();return false;}
  stage=std::move(decoded);
  if(parameter&&
     (name=="LZWDecode"||name=="LZW"||
      name=="FlateDecode"||name=="Fl")){
   if(parameter->kind!=Value::Kind::Dictionary||
      !apply_predictor(stage,*parameter,max_output))return false;
  }
 }
 output=std::move(stage);return true;
}
}
