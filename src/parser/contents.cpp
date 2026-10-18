#include "contents.h"
#include "xref.h"
#include "xref_stream.h"
#include "object.h"
#include "object_resolver.h"
#include "stream_object.h"
#include "stream.h"
#include "function_cache.h"
#include "dct_image.h"
#include "ccitt_image.h"
#include "value.h"
#include "font_encoding.h"
#include "tounicode.h"
#include "../renderer/sfnt.h"
#include <algorithm>
#include <map>
#include <set>
#include <cmath>
#include <tuple>
#include <array>
#include <sstream>
#include <charconv>
namespace rinpdf::parser {
static const Value* get(const Value& v,const char* name){
 if(v.kind!=Value::Kind::Dictionary)return nullptr;
 auto it=v.dictionary.find(name);
 return it==v.dictionary.end()?nullptr:&it->second;
}

static bool box_value(const Value& value,MediaBox& box){
 if(value.kind!=Value::Kind::Array||value.array.size()!=4)return false;
 double v[4];
 for(size_t i=0;i<4;++i){
  if(value.array[i].kind!=Value::Kind::Integer)return false;
  v[i]=(double)value.array[i].integer;
  if(!std::isfinite(v[i])||std::abs(v[i])>10000000)return false;
 }
 if(v[2]<=v[0]||v[3]<=v[1])return false;
 box={v[0],v[1],v[2],v[3]};return true;
}
struct Reader {
 const uint8_t *data;size_t size;const std::vector<XrefEntry>& xref;const std::vector<XrefStreamEntry>& compressed;
 mutable std::map<size_t,Value> compressed_cache;
 mutable std::map<std::pair<int64_t,int64_t>,CalculatorProgram> calculator_token_cache;
 mutable std::map<std::tuple<int64_t,int64_t,unsigned,unsigned>,std::array<double,4>> calculator_result_cache;
 mutable std::map<std::pair<int64_t,int64_t>,std::vector<uint8_t>> sampled_function_cache;
 mutable size_t sampled_function_cache_bytes=0;
 mutable std::map<std::pair<int64_t,int64_t>,CachedFunction> function_dictionary_cache;
 mutable std::map<std::pair<int64_t,int64_t>,CachedStitch> stitch_function_cache;
 mutable std::map<std::tuple<int64_t,int64_t,unsigned>,CachedExponent> exponent_function_cache;
 mutable std::map<std::tuple<int64_t,int64_t,unsigned>,CachedSample> sample_parameter_cache;
 bool entry(const Value& ref,const XrefEntry*& out) const {
  if(ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  auto it=std::find_if(xref.begin(),xref.end(),[&](const XrefEntry& e){
   return e.in_use&&e.object==(size_t)ref.integer&&e.generation==ref.generation;
  });
  if(it==xref.end())return false;
  out=&*it;
  return true;
 }
 bool object(const Value& ref,Value& out) const {
  return resolve_reference_value(data,size,xref,compressed,ref,out,&compressed_cache);
 }
 bool stream(const Value& ref,std::vector<uint8_t>& bytes,size_t limit) const {
  const XrefEntry *e=nullptr;if(!entry(ref,e))return false;
  StreamRange range;
  if(!locate_indirect_stream(data,size,*e,xref,range))return false;
  return decode_stream(data,size,range,bytes,limit);
 }
 bool resolve_unicode_cmap(const Value& ref,unsigned depth,
  std::set<size_t>& visiting,std::map<CMapKey,std::u32string>& output,
  std::vector<std::pair<CMapKey,CMapKey>>& spaces,
  std::string& cmap_name,const Value* page_resources=nullptr) const {
  if(depth>=8||ref.kind!=Value::Kind::Reference||ref.integer<=0||
     !visiting.insert(size_t(ref.integer)).second)return false;
  // Ordinary object resolution intentionally rejects stream objects.
  // Resolve the indirect stream header to obtain its dictionary and data.
  const XrefEntry* entry_ptr=nullptr;
  if(!entry(ref,entry_ptr))return false;
  StreamRange range;
  if(!locate_indirect_stream(data,size,*entry_ptr,xref,range))return false;
  Value dictionary=range.dictionary;
  std::vector<uint8_t> decoded;
  if(!decode_stream(data,size,range,decoded,1024*1024))return false;
  if(const Value* name=get(dictionary,"CMapName")){
   if(name->kind!=Value::Kind::Name||name->text.size()>128)return false;
   cmap_name=name->text;
  }
  const Value* use=get(dictionary,"UseCMap");
  std::map<CMapKey,std::u32string> parent;
  std::vector<std::pair<CMapKey,CMapKey>> parent_spaces;
  std::string parent_name;
  if(use){
   Value parent_ref=*use;
   if(use->kind==Value::Kind::Name){
    // Only an explicitly declared CMap resource can satisfy a name;
    // never guess a built-in CMap from the requested string.
    const Value* resources=get(dictionary,"Resources");
    Value resolved_resources;
    if(!resources)resources=page_resources;
    if(!resources)return false;
    if(resources->kind==Value::Kind::Reference){
     if(!object(*resources,resolved_resources))return false;
    }else resolved_resources=*resources;
    const Value* cmap_resources=get(resolved_resources,"CMap");
    Value cmap_dictionary;
    if(!cmap_resources)return false;
    if(cmap_resources->kind==Value::Kind::Reference){
     if(!object(*cmap_resources,cmap_dictionary))return false;
    }else cmap_dictionary=*cmap_resources;
    if(cmap_dictionary.kind!=Value::Kind::Dictionary)return false;
    const auto it=cmap_dictionary.dictionary.find(use->text);
    if(it==cmap_dictionary.dictionary.end()||
       it->second.kind!=Value::Kind::Reference)return false;
    parent_ref=it->second;
   }
   if(!resolve_unicode_cmap(parent_ref,depth+1,visiting,parent,
        parent_spaces,parent_name,page_resources))return false;
   if(parent_name.empty()||
      (use->kind==Value::Kind::Name&&parent_name!=use->text))return false;
  }
  std::map<CMapKey,std::u32string> result;
  std::vector<std::pair<CMapKey,CMapKey>> ranges;
  if(!parse_tounicode_sequences(decoded.data(),decoded.size(),result,&ranges,
       parent_name,use?&parent:nullptr,use?&parent_spaces:nullptr))return false;
  // A stream dictionary UseCMap may identify a parent even if the stream
  // does not contain an inline usecmap operator.
  if(use){
   for(const auto& [key,value]:parent){
    if(result.size()>=4096&&!result.count(key))return false;
    result.emplace(key,value);
   }
   if(ranges.empty())ranges=parent_spaces;
   else if(!parent_spaces.empty()){
    // A child's codespace must not change its inherited byte boundaries.
    if(ranges.size()!=parent_spaces.size())return false;
    for(size_t i=0;i<ranges.size();++i){
     const auto& a=ranges[i];const auto& b=parent_spaces[i];
     if(a.first.width!=b.first.width||a.first.code!=b.first.code||
        a.second.width!=b.second.width||a.second.code!=b.second.code)
      return false;
    }
   }
   if(!ranges.empty())for(const auto& [key,value]:result){
    bool covered=false;
    for(const auto& range:ranges)
     if(key.width==range.first.width&&key.code>=range.first.code&&
        key.code<=range.second.code){covered=true;break;}
    if(!covered)return false;
   }
  }
  output=std::move(result);spaces=std::move(ranges);
  visiting.erase(size_t(ref.integer));
  return true;
 }
 bool decode_page_image(const StreamRange& range,PageImage& image) const {
      const Value* w=get(range.dictionary,"Width");
      const Value* h=get(range.dictionary,"Height");
      const Value* bits=get(range.dictionary,"BitsPerComponent");
      const Value* cs=get(range.dictionary,"ColorSpace");
      const Value* mask_flag=get(range.dictionary,"ImageMask");
      if(mask_flag){
       if(mask_flag->kind!=Value::Kind::Boolean)return false;
       if(mask_flag->boolean){
        if(!w||!h||w->kind!=Value::Kind::Integer||
           h->kind!=Value::Kind::Integer||
           w->integer<=0||h->integer<=0||
           w->integer>4096||h->integer>4096||cs||
           get(range.dictionary,"SMask")||get(range.dictionary,"Mask")||
           get(range.dictionary,"DecodeParms")||
           (bits&&(bits->kind!=Value::Kind::Integer||bits->integer!=1)))
         return false;
        bool invert=false;
        if(const Value* decode=get(range.dictionary,"Decode")){
         if(decode->kind!=Value::Kind::Array||decode->array.size()!=2||
            decode->array[0].kind!=Value::Kind::Integer||
            decode->array[1].kind!=Value::Kind::Integer)return false;
         if(decode->array[0].integer==1&&decode->array[1].integer==0)
          invert=true;
         else if(decode->array[0].integer!=0||decode->array[1].integer!=1)
          return false;
        }
        const size_t width=size_t(w->integer),height=size_t(h->integer);
        const size_t pitch=(width+7)/8;
        std::vector<uint8_t> packed;
        if(!decode_stream(data,size,range,packed,pitch*height)||
           packed.size()!=pitch*height)return false;
        image=PageImage{};
        image.width=unsigned(width);image.height=unsigned(height);
        image.channels=1;image.stencil=true;
        image.pixels.assign(width*height,0);
        for(size_t y=0;y<height;++y)
         for(size_t x=0;x<width;++x){
          const bool bit=(packed[y*pitch+x/8]>>(7-(x%8)))&1;
          image.pixels[y*width+x]=uint8_t(bit!=invert?255:0);
         }
        return true;
       }
      }
      if(!w||!h||!bits||!cs||
         w->kind!=Value::Kind::Integer||h->kind!=Value::Kind::Integer||
         bits->kind!=Value::Kind::Integer||
         (bits->integer!=8&&bits->integer!=4&&
          bits->integer!=2&&bits->integer!=1)||
         w->integer<=0||h->integer<=0||
         w->integer>4096||h->integer>4096||
         (cs->kind!=Value::Kind::Name&&cs->kind!=Value::Kind::Array)){return false;}
      bool indexed=false;
      unsigned icc_channels=0;
      bool separation=false;
      unsigned device_inputs=1;
      bool sampled_tint=false;
      bool recursive_stitch=false;
      Value stitch_function;
      struct StitchPart {
       double c0[4]{0,0,0,0},c1[4]{1,1,1,1},exponent=1;
      };
      std::vector<StitchPart> stitch_parts;
      std::vector<double> stitch_bounds,stitch_encode;
      std::vector<uint8_t> tint_samples;
      std::vector<unsigned> tint_sizes;
      std::vector<double> tint_encode,tint_decode,tint_domain,tint_range;
      unsigned tint_sample_bits=8;
      unsigned tint_channels=0;
      double tint_c0[4]{0,0,0,0},tint_c1[4]{1,1,1,1},tint_n=1;
      unsigned calibrated=0;
      bool lab=false;
      double lab_range[4]{-100,100,-100,100};
      double cal_gamma[3]{1,1,1};
      double cal_matrix[9]{1,0,0,0,1,0,0,0,1};
      // Initially support D65-calibrated spaces with the default zero
      // BlackPoint. Other illuminants require chromatic adaptation.
      if(cs->kind==Value::Kind::Array&&cs->array.size()==2&&
         cs->array[0].kind==Value::Kind::Name&&
         (cs->array[0].text=="CalGray"||cs->array[0].text=="CalRGB")){
       calibrated=cs->array[0].text=="CalGray"?1:3;
       if(cs->array[1].kind!=Value::Kind::Dictionary)return false;
       const Value& dict=cs->array[1];
       auto read_number=[](const Value& v,double& x){
        if(v.kind==Value::Kind::Integer)x=double(v.integer);
        else if(v.kind==Value::Kind::Real)x=v.real;
        else return false;
        return std::isfinite(x);
       };
       const Value* white=get(dict,"WhitePoint");
       if(!white||white->kind!=Value::Kind::Array||
          white->array.size()!=3)return false;
       double wp[3]{};
       for(unsigned i=0;i<3;++i)
        if(!read_number(white->array[i],wp[i]))return false;
       if(std::abs(wp[0]-0.95047)>0.002||
          std::abs(wp[1]-1)>0.002||
          std::abs(wp[2]-1.08883)>0.002)return false;
       if(const Value* black=get(dict,"BlackPoint")){
        if(black->kind!=Value::Kind::Array||black->array.size()!=3)
         return false;
        for(const auto& val:black->array){
         double v=0;
         if(!read_number(val,v)||std::abs(v)>1e-8)return false;
        }
       }
       if(const Value* gamma=get(dict,"Gamma")){
        if(calibrated==1){
         if(!read_number(*gamma,cal_gamma[0]))return false;
        }else{
         if(gamma->kind!=Value::Kind::Array||gamma->array.size()!=3)
          return false;
         for(unsigned i=0;i<3;++i)
          if(!read_number(gamma->array[i],cal_gamma[i]))return false;
        }
       }
       for(unsigned i=0;i<calibrated;++i)
        if(cal_gamma[i]<=0||cal_gamma[i]>16)return false;
       if(const Value* matrix=get(dict,"Matrix")){
        if(calibrated!=3||matrix->kind!=Value::Kind::Array||
           matrix->array.size()!=9)return false;
        for(unsigned i=0;i<9;++i)
         if(!read_number(matrix->array[i],cal_matrix[i])||
            std::abs(cal_matrix[i])>16)return false;
       }else if(calibrated==3){
        // CalRGB identity is XYZ, not device RGB. It is converted below.
       }
      }
      if(cs->kind==Value::Kind::Array&&cs->array.size()==2&&
         cs->array[0].kind==Value::Kind::Name&&cs->array[0].text=="Lab"){
       if(cs->array[1].kind!=Value::Kind::Dictionary)return false;
       const Value& info=cs->array[1];
       auto number=[](const Value& v,double& out){
        if(v.kind==Value::Kind::Integer)out=double(v.integer);
        else if(v.kind==Value::Kind::Real)out=v.real;
        else return false;
        return std::isfinite(out);
       };
       const Value* wp=get(info,"WhitePoint");
       if(!wp||wp->kind!=Value::Kind::Array||wp->array.size()!=3)
        return false;
       const double d65[3]{0.95047,1,1.08883};
       for(unsigned i=0;i<3;++i){
        double v=0;
        if(!number(wp->array[i],v)||std::abs(v-d65[i])>0.002)
         return false;
       }
       if(const Value* bp=get(info,"BlackPoint")){
        if(bp->kind!=Value::Kind::Array||bp->array.size()!=3)
         return false;
        for(const auto& v:bp->array){
         double value=0;
         if(!number(v,value)||std::abs(value)>1e-8)return false;
        }
       }
       if(const Value* range=get(info,"Range")){
        if(range->kind!=Value::Kind::Array||range->array.size()!=4)
         return false;
        for(unsigned i=0;i<4;++i)
         if(!number(range->array[i],lab_range[i])||
            std::abs(lab_range[i])>1000)return false;
        if(lab_range[0]>=lab_range[1]||lab_range[2]>=lab_range[3])
         return false;
       }
       lab=true;
      }
      if(cs->kind==Value::Kind::Array&&
         (cs->array.size()==4||cs->array.size()==5)&&
         cs->array[0].kind==Value::Kind::Name&&
         (cs->array[0].text=="Separation"||
          cs->array[0].text=="DeviceN")){
       const bool device_n=cs->array[0].text=="DeviceN";
       if(device_n){
        if(cs->array[1].kind!=Value::Kind::Array||
           cs->array[1].array.empty()||
           cs->array[1].array.size()>4)return false;
        device_inputs=unsigned(cs->array[1].array.size());
        for(const Value& name:cs->array[1].array)
         if(name.kind!=Value::Kind::Name)return false;
        if(cs->array.size()==5&&
           cs->array[4].kind!=Value::Kind::Dictionary)return false;
       }else if(cs->array.size()!=4||
                cs->array[1].kind!=Value::Kind::Name)return false;
       if(cs->array[2].kind!=Value::Kind::Name)return false;
       const std::string& alternate=cs->array[2].text;
       tint_channels=alternate=="DeviceGray"?1:
                     alternate=="DeviceRGB"?3:
                     alternate=="DeviceCMYK"?4:0;
       if(!tint_channels)return false;
       Value fn;
       if(cs->array[3].kind==Value::Kind::Reference){
        const XrefEntry* function_entry=nullptr;
        if(!entry(cs->array[3],function_entry))return false;
        StreamRange function_stream;
        if(locate_indirect_stream(data,size,*function_entry,xref,
                                  function_stream))
         fn=function_stream.dictionary;
        else if(!object(cs->array[3],fn))return false;
       }else fn=cs->array[3];
       const Value* type=get(fn,"FunctionType");
       const Value* domain=get(fn,"Domain");
       const Value* exponent=get(fn,"N");
       const Value* c0=get(fn,"C0");
       const Value* c1=get(fn,"C1");
       auto number=[](const Value& v,double& out){
        if(v.kind==Value::Kind::Integer)out=double(v.integer);
        else if(v.kind==Value::Kind::Real)out=v.real;
        else return false;
        return std::isfinite(out)&&std::abs(out)<=100000;
       };
       if(!type||type->kind!=Value::Kind::Integer)return false;
       if(type->integer==0){
        // Bounded sampled functions with multilinear interpolation.
        if(cs->array[3].kind!=Value::Kind::Reference)return false;
        const XrefEntry* entry_ptr=nullptr;
        if(!entry(cs->array[3],entry_ptr))return false;
        StreamRange function_range;
        if(!locate_indirect_stream(data,size,*entry_ptr,xref,function_range))
         return false;
        const Value* sizes=get(function_range.dictionary,"Size");
        const Value* bps=get(function_range.dictionary,"BitsPerSample");
        const Value* range=get(function_range.dictionary,"Range");
        const Value* encode=get(function_range.dictionary,"Encode");
        const Value* decode=get(function_range.dictionary,"Decode");
        if(!sizes||sizes->kind!=Value::Kind::Array||
           sizes->array.size()!=device_inputs||
           !bps||bps->kind!=Value::Kind::Integer||
           (bps->integer!=1&&bps->integer!=2&&bps->integer!=4&&
            bps->integer!=8&&bps->integer!=12&&bps->integer!=16&&
            bps->integer!=24&&bps->integer!=32)||
           !range||range->kind!=Value::Kind::Array||
           range->array.size()!=tint_channels*2||
           !domain||domain->kind!=Value::Kind::Array||
           domain->array.size()!=device_inputs*2)return false;
        tint_sample_bits=unsigned(bps->integer);
        size_t sample_count=tint_channels;
        for(unsigned i=0;i<device_inputs;++i){
         const Value& v=sizes->array[i];
         if(v.kind!=Value::Kind::Integer||v.integer<2||v.integer>64)
          return false;
         tint_sizes.push_back(unsigned(v.integer));
         if(sample_count>4*1024*1024/unsigned(v.integer))return false;
         sample_count*=unsigned(v.integer);
         double d0=0,d1=0;
         if(!number(domain->array[2*i],d0)||
            !number(domain->array[2*i+1],d1)||d0>=d1||
            std::abs(d0)>100000||std::abs(d1)>100000)return false;
         tint_domain.push_back(d0);
         tint_domain.push_back(d1);
        }
        for(unsigned c=0;c<tint_channels;++c){
         double a=0,b=0;
         if(!number(range->array[2*c],a)||
            !number(range->array[2*c+1],b)||a>=b||
            std::abs(a)>100000||std::abs(b)>100000)return false;
         tint_range.push_back(a);
         tint_range.push_back(b);
        }
        tint_encode.resize(device_inputs*2);
        tint_decode.resize(tint_channels*2);
        if(encode&&(encode->kind!=Value::Kind::Array||
                    encode->array.size()!=device_inputs*2))return false;
        if(decode&&(decode->kind!=Value::Kind::Array||
                    decode->array.size()!=tint_channels*2))return false;
        for(unsigned i=0;i<device_inputs;++i){
         tint_encode[i*2]=0;
         tint_encode[i*2+1]=double(tint_sizes[i]-1);
         if(encode&&(!number(encode->array[2*i],tint_encode[2*i])||
                     !number(encode->array[2*i+1],tint_encode[2*i+1])))
          return false;
        }
        for(unsigned c=0;c<tint_channels;++c){
         tint_decode[2*c]=tint_range[2*c];
         tint_decode[2*c+1]=tint_range[2*c+1];
         if(decode&&(!number(decode->array[2*c],tint_decode[2*c])||
                     !number(decode->array[2*c+1],tint_decode[2*c+1])))
          return false;
        }
        const uint64_t bit_count=uint64_t(sample_count)*tint_sample_bits;
        const uint64_t byte_count=(bit_count+7)/8;
        if(byte_count>4*1024*1024)return false;
        if(!decode_stream(data,size,function_range,tint_samples,
                          size_t(byte_count))||
           tint_samples.size()!=size_t(byte_count))return false;
        sampled_tint=true;
       }else if(type->integer==4){
        if(device_inputs!=1)return false;
        double probe[4]{};
        if(!eval_tint_function(cs->array[3],0,tint_channels,probe)||
           !eval_tint_function(cs->array[3],1,tint_channels,probe))
         return false;
        stitch_function=cs->array[3];
        recursive_stitch=true;
       }else if(type->integer==3){
        if(device_inputs!=1)return false;
        double boundary[4]{};
        // Probe all four tint endpoints to validate the function hierarchy.
        if(!eval_tint_function(cs->array[3],0,tint_channels,boundary)||
           !eval_tint_function(cs->array[3],1,tint_channels,boundary))
         return false;
        stitch_function=cs->array[3];
        recursive_stitch=true;
       }else if(type->integer==2){
        if(device_inputs!=1||!domain||domain->kind!=Value::Kind::Array||
           domain->array.size()!=2||!exponent||
           !number(*exponent,tint_n)||tint_n<=0||tint_n>100)
         return false;
        double d0=0,d1=0;
        if(!number(domain->array[0],d0)||!number(domain->array[1],d1)||
           d0!=0||d1!=1)return false;
        for(const auto& setting:std::initializer_list<std::pair<const Value*,double*>>{
              {c0,tint_c0},{c1,tint_c1}}){
         if(!setting.first)continue;
         if(setting.first->kind!=Value::Kind::Array||
            setting.first->array.size()!=tint_channels)return false;
         for(unsigned c=0;c<tint_channels;++c)
          if(!number(setting.first->array[c],setting.second[c])||
             setting.second[c]<0||setting.second[c]>1)return false;
        }
       }else return false;
       // Both Separation and one-colorant DeviceN share the same
       // one-input Type 2 tint evaluation into the alternate space.
       separation=true;
      }
      std::string palette;
      unsigned maximum_index=0;
      unsigned palette_channels=0;
      if(cs->kind==Value::Kind::Array&&cs->array.size()==2&&
         cs->array[0].kind==Value::Kind::Name&&
         cs->array[0].text=="ICCBased"){
       // Without an ICC color engine, use only an explicitly specified
       // PDF Alternate. Never silently treat unknown profiles as sRGB.
       if(cs->array[1].kind!=Value::Kind::Reference)return false;
       const XrefEntry* icc_entry=nullptr;
       if(!entry(cs->array[1],icc_entry))return false;
       StreamRange icc_range;
       if(!locate_indirect_stream(data,size,*icc_entry,xref,icc_range))
        return false;
       const Value* n=get(icc_range.dictionary,"N");
       const Value* alternate=get(icc_range.dictionary,"Alternate");
       if(!n||n->kind!=Value::Kind::Integer||
          !alternate||alternate->kind!=Value::Kind::Name)return false;
       icc_channels=alternate->text=="DeviceGray"?1:
                    alternate->text=="DeviceRGB"?3:
                    alternate->text=="DeviceCMYK"?4:0;
       if(!icc_channels||n->integer!=icc_channels)return false;
       // Validate the profile stream reference, but do not claim ICC-based
       // color accuracy: the fallback uses the explicit Alternate only.
       std::vector<uint8_t> profile;
       if(!decode_stream(data,size,icc_range,profile,4*1024*1024)||
          profile.size()<128)return false;
      }else if(cs->kind==Value::Kind::Array&&!calibrated&&!lab&&!separation){
       if(cs->array.size()!=4||cs->array[0].kind!=Value::Kind::Name||
          cs->array[0].text!="Indexed"||
          cs->array[1].kind!=Value::Kind::Name||
          cs->array[2].kind!=Value::Kind::Integer||
          cs->array[2].integer<0||cs->array[2].integer>255||
          (cs->array[3].kind!=Value::Kind::String&&
           cs->array[3].kind!=Value::Kind::Reference))return false;
       palette_channels=cs->array[1].text=="DeviceRGB"?3:
                        cs->array[1].text=="DeviceGray"?1:0;
       if(!palette_channels)return false;
       maximum_index=unsigned(cs->array[2].integer);
       const size_t palette_size=size_t(maximum_index+1)*palette_channels;
       if(cs->array[3].kind==Value::Kind::String)
        palette=cs->array[3].text;
       else{
        std::vector<uint8_t> decoded_palette;
        if(!stream(cs->array[3],decoded_palette,palette_size)||
           decoded_palette.size()!=palette_size)return false;
        palette.assign(decoded_palette.begin(),decoded_palette.end());
       }
       if(palette.size()!=palette_size)return false;
       indexed=true;
      }
      if(!indexed&&bits->integer!=8&&bits->integer!=1)return false;
      if(indexed&&unsigned(maximum_index)>=(1u<<unsigned(bits->integer)))
       return false;
      const unsigned channels=indexed?1:separation?device_inputs:lab?3:calibrated?calibrated:icc_channels?icc_channels:
        cs->text=="DeviceGray"?1:cs->text=="DeviceRGB"?3:
        cs->text=="DeviceCMYK"?4:0;
      if(!channels||get(range.dictionary,"Mask")||
         get(range.dictionary,"Decode")){return false;}
      const size_t expected=size_t(w->integer)*size_t(h->integer)*channels;
      if(expected>32*1024*1024){return false;}
      image=PageImage{};
      image.width=unsigned(w->integer);image.height=unsigned(h->integer);
      image.channels=channels;
      const Value* filter=get(range.dictionary,"Filter");
      const bool fax=filter&&filter->kind==Value::Kind::Name&&
        (filter->text=="CCITTFaxDecode"||filter->text=="CCF");
      if(fax){
       if(indexed||bits->integer!=1||channels!=1||
          get(range.dictionary,"SMask")||get(range.dictionary,"Mask")||
          get(range.dictionary,"Decode"))return false;
       const Value* parms=get(range.dictionary,"DecodeParms");
       if(parms&&parms->kind!=Value::Kind::Dictionary)return false;
       auto number=[&](const char* key,long long fallback,long long& out){
        const Value* v=parms?get(*parms,key):nullptr;
        if(!v){out=fallback;return true;}
        if(v->kind!=Value::Kind::Integer)return false;
        out=v->integer;return true;
       };
       auto boolean=[&](const char* key,bool fallback,bool& out){
        const Value* v=parms?get(*parms,key):nullptr;
        if(!v){out=fallback;return true;}
        if(v->kind!=Value::Kind::Boolean)return false;
        out=v->boolean;return true;
       };
       long long k=0,columns=image.width,rows=image.height,damaged=0;
       bool byte_align=false,black_is_one=false;
       if(!number("K",0,k)||!number("Columns",image.width,columns)||
          !number("Rows",image.height,rows)||
          !number("DamagedRowsBeforeError",0,damaged)||
          !boolean("EncodedByteAlign",false,byte_align)||
          !boolean("BlackIs1",false,black_is_one)||
          columns!=image.width||rows!=image.height||damaged!=0||
          (k!=-1&&k!=0)||
          range.offset>size||range.length>size-range.offset||
          !decode_ccitt_image(data+range.offset,range.length,
             image.width,image.height,int(k),byte_align,black_is_one,
             image.pixels))return false;
       return true;
      }
      if(!indexed&&bits->integer!=8)return false;
      // Support a same-size, 8-bit grayscale SMask image. Restrict the
      // initial implementation to raw/Flate data without predictors.
      if(const Value* smask=get(range.dictionary,"SMask")){
       if(smask->kind!=Value::Kind::Reference){return false;}
       const XrefEntry* mask_entry=nullptr;
       if(!entry(*smask,mask_entry)){return false;}
       StreamRange mask_range;
       if(!locate_indirect_stream(data,size,*mask_entry,xref,mask_range)){
        return false;
       }
       const Value* ms=get(mask_range.dictionary,"Subtype");
       const Value* mw=get(mask_range.dictionary,"Width");
       const Value* mh=get(mask_range.dictionary,"Height");
       const Value* mb=get(mask_range.dictionary,"BitsPerComponent");
       const Value* mc=get(mask_range.dictionary,"ColorSpace");
       if(!ms||ms->kind!=Value::Kind::Name||ms->text!="Image"||
          !mw||mw->kind!=Value::Kind::Integer||mw->integer!=w->integer||
          !mh||mh->kind!=Value::Kind::Integer||mh->integer!=h->integer||
          !mb||mb->kind!=Value::Kind::Integer||mb->integer!=8||
          !mc||mc->kind!=Value::Kind::Name||mc->text!="DeviceGray"||
          get(mask_range.dictionary,"SMask")||
          get(mask_range.dictionary,"Mask")||
          get(mask_range.dictionary,"Decode")||
          get(mask_range.dictionary,"DecodeParms")||
          !decode_stream(data,size,mask_range,image.alpha,
                         size_t(w->integer)*size_t(h->integer))||
          image.alpha.size()!=size_t(w->integer)*size_t(h->integer)){
        return false;
       }
      }
      const bool dct=filter&&filter->kind==Value::Kind::Name&&
        (filter->text=="DCTDecode"||filter->text=="DCT");
      if(dct){
       // The JPEG backend only decodes native Gray/RGB samples.
       if(indexed||separation||calibrated||lab||channels==4)return false;
       // JPEG image data is compressed independently of PDF Flate filters.
       // DecodeParms predictors are not valid for this path.
       if(get(range.dictionary,"DecodeParms")||
          range.offset>size||range.length>size-range.offset||
          !decode_dct_image(data+range.offset,range.length,
             unsigned(w->integer),unsigned(h->integer),channels,
             image.pixels)){return false;}
       return true;
      }
      // Predictor handling is centralized in decode_stream for both
      // Flate and LZW images. Validate the resulting raster dimensions.
      if(const Value* parms=get(range.dictionary,"DecodeParms")){
       if(parms->kind!=Value::Kind::Dictionary)return false;
       auto check=[&](const char* key,unsigned expected_value){
        const Value* value=get(*parms,key);
        return !value||(value->kind==Value::Kind::Integer&&
                        value->integer==expected_value);
       };
       if(!check("Columns",image.width)||!check("Colors",channels)||
          !check("BitsPerComponent",unsigned(bits->integer)))return false;
      }
      const unsigned sample_bits=unsigned(bits->integer);
      const size_t packed_stride=(size_t(image.width)*sample_bits+7)/8;
      const size_t decoded_size=indexed&&sample_bits<8?
         packed_stride*image.height:expected;
      std::vector<uint8_t> decoded;
      if(!decode_stream(data,size,range,decoded,decoded_size)||
         decoded.size()!=decoded_size)return false;
      if(indexed&&sample_bits<8){
       image.pixels.resize(size_t(image.width)*image.height);
       const unsigned sample_mask=(1u<<sample_bits)-1;
       for(size_t y=0;y<image.height;++y)
        for(size_t x=0;x<image.width;++x){
         const size_t bit=x*sample_bits;
         image.pixels[y*image.width+x]=uint8_t(
          (decoded[y*packed_stride+bit/8]>>(8-sample_bits-(bit%8)))&
          sample_mask);
        }
      }else image.pixels=std::move(decoded);
      if(indexed){
       std::vector<uint8_t> rgb(size_t(image.width)*image.height*3);
       for(size_t i=0;i<size_t(image.width)*image.height;++i){
        const unsigned index=image.pixels[i];
        if(index>maximum_index)return false;
        for(unsigned c=0;c<3;++c)
         rgb[3*i+c]=uint8_t(palette[index*palette_channels+
                                     (palette_channels==1?0:c)]);
       }
       image.pixels=std::move(rgb);image.channels=3;
      }else if(separation){
       std::vector<uint8_t> rgb(size_t(image.width)*image.height*3);
       for(size_t i=0;i<size_t(image.width)*image.height;++i){
        double values[4]{};
        if(sampled_tint){
         // Multilinear interpolation over the sampled function grid.
         // The first input varies fastest in the PDF sample table.
         size_t lower[4]{},strides[4]{};
         double fraction[4]{};
         size_t stride=1;
         for(unsigned dimension=0;dimension<device_inputs;++dimension){
          const double sample=double(image.pixels[i*device_inputs+dimension])/255;
          const double input=std::clamp(sample,tint_domain[2*dimension],
                                           tint_domain[2*dimension+1]);
          const double domain_span=tint_domain[2*dimension+1]-
                                   tint_domain[2*dimension];
          const double normalized=(input-tint_domain[2*dimension])/domain_span;
          const double position=std::clamp(
           tint_encode[2*dimension]+normalized*
             (tint_encode[2*dimension+1]-tint_encode[2*dimension]),
           0.0,double(tint_sizes[dimension]-1));
          lower[dimension]=size_t(position);
          fraction[dimension]=position-double(lower[dimension]);
          if(lower[dimension]>=tint_sizes[dimension]-1){
           lower[dimension]=tint_sizes[dimension]-1;
           fraction[dimension]=0;
          }
          strides[dimension]=stride;
          stride*=tint_sizes[dimension];
         }
         const unsigned corners=1u<<device_inputs;
         for(unsigned corner=0;corner<corners;++corner){
          size_t index=0;
          double weight=1;
          for(unsigned dimension=0;dimension<device_inputs;++dimension){
           const bool upper=(corner&(1u<<dimension))!=0;
           const bool can_advance=lower[dimension]+1<tint_sizes[dimension];
           index+=(lower[dimension]+(upper&&can_advance?1:0))*
                  strides[dimension];
           weight*=upper?fraction[dimension]:1-fraction[dimension];
          }
          for(unsigned c=0;c<tint_channels;++c){
           const size_t sample_index=index*tint_channels+c;
           // FunctionType 0 samples are packed MSB-first, without
           // per-sample byte padding (including 12/24/32-bit values).
           const size_t first_bit=sample_index*size_t(tint_sample_bits);
           uint64_t sample=0;
           for(unsigned bit=0;bit<tint_sample_bits;++bit){
            const size_t position=first_bit+bit;
            sample=(sample<<1)|
               ((tint_samples[position/8]>>(7-(position%8)))&1u);
           }
           const uint64_t maximum=(uint64_t(1)<<tint_sample_bits)-1;
           const double raw=double(sample)/double(maximum);
           const double decoded=tint_decode[2*c]+raw*
              (tint_decode[2*c+1]-tint_decode[2*c]);
           values[c]+=weight*std::clamp(decoded,tint_range[2*c],
                                                  tint_range[2*c+1]);
          }
         }
        }else if(recursive_stitch){
         if(!eval_tint_function(stitch_function,
              double(image.pixels[i])/255,tint_channels,values))
          return false;
        }else{
         const double tint=std::pow(double(image.pixels[i])/255,tint_n);
         for(unsigned c=0;c<tint_channels;++c)
          values[c]=tint_c0[c]+tint*(tint_c1[c]-tint_c0[c]);
        }
        for(unsigned c=0;c<3;++c){
         double v=tint_channels==1?values[0]:
                  tint_channels==3?values[c]:
                  (1-values[c])*(1-values[3]);
         rgb[3*i+c]=uint8_t(std::lround(255*std::clamp(v,0.0,1.0)));
        }
       }
       image.pixels=std::move(rgb);image.channels=3;
      }else if(lab){
       std::vector<uint8_t> rgb(size_t(image.width)*image.height*3);
       auto convert=[](double linear){
        linear=std::clamp(linear,0.0,1.0);
        const double value=linear<=0.0031308?12.92*linear:
          1.055*std::pow(linear,1.0/2.4)-0.055;
        return uint8_t(std::lround(255*std::clamp(value,0.0,1.0)));
       };
       auto inverse_lab=[](double t){
        const double delta=6.0/29.0;
        return t>delta?t*t*t:3*delta*delta*(t-4.0/29.0);
       };
       for(size_t i=0;i<size_t(image.width)*image.height;++i){
        const double L=100*double(image.pixels[3*i])/255;
        const double aa=lab_range[0]+
          (lab_range[1]-lab_range[0])*double(image.pixels[3*i+1])/255;
        const double bb=lab_range[2]+
          (lab_range[3]-lab_range[2])*double(image.pixels[3*i+2])/255;
        const double fy=(L+16)/116;
        const double X=0.95047*inverse_lab(fy+aa/500);
        const double Y=inverse_lab(fy);
        const double Z=1.08883*inverse_lab(fy-bb/200);
        rgb[3*i]=convert(3.2406*X-1.5372*Y-0.4986*Z);
        rgb[3*i+1]=convert(-0.9689*X+1.8758*Y+0.0415*Z);
        rgb[3*i+2]=convert(0.0557*X-0.2040*Y+1.0570*Z);
       }
       image.pixels=std::move(rgb);image.channels=3;
      }else if(calibrated){
       std::vector<uint8_t> rgb(size_t(image.width)*image.height*3);
       auto srgb=[](double linear){
        linear=std::clamp(linear,0.0,1.0);
        const double encoded=linear<=0.0031308?
          12.92*linear:1.055*std::pow(linear,1.0/2.4)-0.055;
        return uint8_t(std::lround(std::clamp(encoded,0.0,1.0)*255));
       };
       for(size_t i=0;i<size_t(image.width)*image.height;++i){
        double X=0,Y=0,Z=0;
        if(calibrated==1){
         Y=std::pow(double(image.pixels[i])/255,cal_gamma[0]);
         X=0.95047*Y;Z=1.08883*Y;
        }else{
         double component[3];
         for(unsigned c=0;c<3;++c)
          component[c]=std::pow(double(image.pixels[i*3+c])/255,
                                cal_gamma[c]);
         X=cal_matrix[0]*component[0]+cal_matrix[3]*component[1]+
           cal_matrix[6]*component[2];
         Y=cal_matrix[1]*component[0]+cal_matrix[4]*component[1]+
           cal_matrix[7]*component[2];
         Z=cal_matrix[2]*component[0]+cal_matrix[5]*component[1]+
           cal_matrix[8]*component[2];
        }
        rgb[3*i]=srgb(3.2406*X-1.5372*Y-0.4986*Z);
        rgb[3*i+1]=srgb(-0.9689*X+1.8758*Y+0.0415*Z);
        rgb[3*i+2]=srgb(0.0557*X-0.2040*Y+1.0570*Z);
       }
       image.pixels=std::move(rgb);image.channels=3;
      }else if(channels==4){
       std::vector<uint8_t> rgb(size_t(image.width)*image.height*3);
       for(size_t i=0;i<size_t(image.width)*image.height;++i){
        const unsigned k=image.pixels[4*i+3];
        for(unsigned c=0;c<3;++c)
         rgb[3*i+c]=uint8_t(((255-unsigned(image.pixels[4*i+c]))*
                                  (255-k)+127)/255);
       }
       image.pixels=std::move(rgb);image.channels=3;
      }
      return true;
 }
 bool walk_images(const Value& ref,size_t depth,size_t& current,size_t target,
  std::set<size_t>& active,const Value* inherited,
  std::map<std::string,PageImage>& output) const {
  if(depth>64||ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  const size_t id=static_cast<size_t>(ref.integer);
  if(!active.insert(id).second)return false;
  Value node,resources_owned;
  if(!object(ref,node)){active.erase(id);return false;}
  if(const Value* resources=get(node,"Resources")){
   if(resources->kind==Value::Kind::Reference){
    if(!object(*resources,resources_owned)){active.erase(id);return false;}
   }else resources_owned=*resources;
   if(resources_owned.kind!=Value::Kind::Dictionary){active.erase(id);return false;}
   inherited=&resources_owned;
  }
  const Value* type=get(node,"Type");
  if(!type||type->kind!=Value::Kind::Name){active.erase(id);return false;}
  if(type->text=="Page"){
   if(current==target&&inherited){
    if(const Value* xobjs=get(*inherited,"XObject")){
     Value entries;
     if(xobjs->kind==Value::Kind::Reference){
      if(!object(*xobjs,entries)){active.erase(id);return false;}
     }else entries=*xobjs;
     if(entries.kind!=Value::Kind::Dictionary||
        entries.dictionary.size()>256){active.erase(id);return false;}
     for(const auto& [name,ref_image]:entries.dictionary){
      if(ref_image.kind!=Value::Kind::Reference){active.erase(id);return false;}
      const XrefEntry* ent=nullptr;
      if(!entry(ref_image,ent)){active.erase(id);return false;}
      StreamRange range;
      if(!locate_indirect_stream(data,size,*ent,xref,range)){
       active.erase(id);return false;
      }
      const Value* subtype=get(range.dictionary,"Subtype");
      if(!subtype||subtype->kind!=Value::Kind::Name){active.erase(id);return false;}
      if(subtype->text=="Form")continue;
      if(subtype->text!="Image"){active.erase(id);return false;}
      PageImage image;
      if(!decode_page_image(range,image)){active.erase(id);return false;}
      output.emplace(name,std::move(image));
     }
    }
   }
   ++current;active.erase(id);return true;
  }
  if(type->text!="Pages"){active.erase(id);return false;}
  const Value* kids=get(node,"Kids");
  if(!kids||kids->kind!=Value::Kind::Array){active.erase(id);return false;}
  for(const Value& child:kids->array){
   if(!walk_images(child,depth+1,current,target,active,inherited,output)){
    active.erase(id);return false;
   }
  }
  active.erase(id);return true;
 }
 // Evaluate one-input Type 2 and nested Type 3 functions. References
 // are resolved explicitly and recursion is bounded for untrusted PDFs.
 #include "function_eval.inc"
 bool parse_cid_descendant(const Value& descendant,PageFont& resource) const {
  const Value* subtype=get(descendant,"Subtype");
  if(!subtype||subtype->kind!=Value::Kind::Name||
     subtype->text!="CIDFontType2"||
     !parse_cid_widths(get(descendant,"DW"),get(descendant,"W"),
                       resource.cid_widths)||
     !parse_cid_vertical_metrics(get(descendant,"DW2"),get(descendant,"W2"),
                                 resource.vertical_metrics))return false;
  if(const Value* map=get(descendant,"CIDToGIDMap")){
   if(map->kind==Value::Kind::Name){
    if(map->text!="Identity")return false;
   }else if(map->kind==Value::Kind::Reference){
    resource.cid_map.identity=false;
    if(!stream(*map,resource.cid_map.bytes,131072)||
       !cid_map_valid(resource.cid_map))return false;
   }else return false;
  }
  return true;
 }
 bool parse_font_descriptor(const Value& reference,PageFont& font) const {
  Value descriptor;
  if(reference.kind==Value::Kind::Reference){
   if(!object(reference,descriptor))return false;
  }else descriptor=reference;
  if(descriptor.kind!=Value::Kind::Dictionary)return false;
  if(font.subtype!="Type0"){
   if(const Value* missing=get(descriptor,"MissingWidth")){
    if(missing->kind==Value::Kind::Integer)
     font.missing_width=double(missing->integer);
    else if(missing->kind==Value::Kind::Real)
     font.missing_width=missing->real;
    else return false;
    if(!std::isfinite(font.missing_width)||
       font.missing_width<0||font.missing_width>100000)return false;
   }
  }
  if(const Value* file=get(descriptor,"FontFile2")){
   if(file->kind!=Value::Kind::Reference||
      !stream(*file,font.embedded_truetype,16*1024*1024))return false;
   renderer::SfntFont validated;
   if(!renderer::parse_sfnt(font.embedded_truetype.data(),
                            font.embedded_truetype.size(),validated))return false;
  }
  return true;
 }
 bool parse_font_encoding(const Value& font,PageFont& out) const {
  const Value* encoding=get(font,"Encoding");
  if(!encoding)return true;
  Value resolved;
  if(encoding->kind==Value::Kind::Reference){
   if(!object(*encoding,resolved))return false;
  }else resolved=*encoding;
  if(resolved.kind==Value::Kind::Name){
   out.encoding=resolved.text;
   return true;
  }
  if(resolved.kind!=Value::Kind::Dictionary)return false;
  const Value* base=get(resolved,"BaseEncoding");
  const Value* differences=get(resolved,"Differences");
  if(!base||base->kind!=Value::Kind::Name||!differences||
     !decode_font_differences(*differences,out.differences))return false;
  out.encoding=base->text;
  return true;
 }
 bool parse_font_dictionary(const Value& font,const Value* scope,
                           PageFont& out) const {
      const Value* subtype=get(font,"Subtype"),*base=get(font,"BaseFont");
      if(font.kind!=Value::Kind::Dictionary||!subtype||!base||
         subtype->kind!=Value::Kind::Name||base->kind!=Value::Kind::Name){
       return false;
      }
      out=PageFont{base->text,subtype->text,"",{}};
      if(!parse_font_encoding(font,out)){return false;}
      if(out.subtype!="Type0"){
       const Value* first=get(font,"FirstChar");
       const Value* last=get(font,"LastChar");
       const Value* widths=get(font,"Widths");
       if(first||last||widths){
        if(!first||!last||!widths||
           first->kind!=Value::Kind::Integer||
           last->kind!=Value::Kind::Integer||
           first->integer<0||last->integer>255||
           first->integer>last->integer){return false;}
        Value width_array;
        if(widths->kind==Value::Kind::Reference){
         if(!object(*widths,width_array)){return false;}
        }else width_array=*widths;
        if(width_array.kind!=Value::Kind::Array||
           width_array.array.size()!=size_t(last->integer-first->integer+1)){
         return false;
        }
        for(size_t i=0;i<width_array.array.size();++i){
         double width=0;
         const Value& value=width_array.array[i];
         if(value.kind==Value::Kind::Integer)width=double(value.integer);
         else if(value.kind==Value::Kind::Real)width=value.real;
         else{return false;}
         if(!std::isfinite(width)||width<0||width>100000){
          return false;
         }
         out.simple_widths.emplace(static_cast<uint8_t>(first->integer+static_cast<long long>(i)),width);
        }
       }
      }
      // Type0 fonts select a descendant CIDFont; CIDToGIDMap is distinct
      // from the ToUnicode extraction map and must not be confused with it.
      if(out.subtype=="Type0"){
       const Value* descendants=get(font,"DescendantFonts");
       if(!descendants||descendants->kind!=Value::Kind::Array||
          descendants->array.size()!=1){return false;}
       const Value& reference=descendants->array.front();
       Value descendant;
       if(reference.kind==Value::Kind::Reference){
        if(!object(reference,descendant)){return false;}
       }else descendant=reference;
       if(!parse_cid_descendant(descendant,out)){
        return false;
       }
       if(const Value* child_descriptor=get(descendant,"FontDescriptor")){
        if(!parse_font_descriptor(*child_descriptor,out)){
         return false;
        }
       }
      }
      if(const Value* descriptor=get(font,"FontDescriptor")){
       if(!parse_font_descriptor(*descriptor,out)){
        return false;
       }
      }
      if(const Value* cmap=get(font,"ToUnicode")){
       std::set<size_t> visiting;
       std::string cmap_name;
       if(!resolve_unicode_cmap(*cmap,0,visiting,out.unicode_sequences,
            out.unicode_codespaces,cmap_name,scope)){
        return false;
       }
      }
  return true;
 }
 bool load_local_form(const Value& ref,unsigned depth,std::set<size_t>& visiting,
                     const std::map<std::pair<int64_t,int64_t>,std::string>& page_names,
                     PageForm& form) const {
  if(depth>=8||ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  const size_t id=size_t(ref.integer);
  if(!visiting.insert(id).second)return false;
  const XrefEntry* ent=nullptr;
  if(!entry(ref,ent))return false;
  StreamRange range;
  if(!locate_indirect_stream(data,size,*ent,xref,range))return false;
  const Value* subtype=get(range.dictionary,"Subtype");
  const Value* form_type=get(range.dictionary,"FormType");
  if(!subtype||subtype->kind!=Value::Kind::Name||subtype->text!="Form"||
     get(range.dictionary,"Group")||
     (form_type&&(form_type->kind!=Value::Kind::Integer||form_type->integer!=1)))
   return false;
  const Value* bbox=get(range.dictionary,"BBox");
  if(!bbox||bbox->kind!=Value::Kind::Array||bbox->array.size()!=4)return false;
  auto number=[](const Value& value,double& out){
   if(value.kind==Value::Kind::Integer)out=double(value.integer);
   else if(value.kind==Value::Kind::Real)out=value.real;
   else return false;
   return std::isfinite(out)&&std::abs(out)<=10000000;
  };
  for(size_t i=0;i<4;++i)if(!number(bbox->array[i],form.bbox[i]))return false;
  if(form.bbox[0]>=form.bbox[2]||form.bbox[1]>=form.bbox[3])return false;
  if(const Value* matrix=get(range.dictionary,"Matrix")){
   if(matrix->kind!=Value::Kind::Array||matrix->array.size()!=6)return false;
   for(size_t i=0;i<6;++i)
    if(!number(matrix->array[i],form.matrix[i]))return false;
  }
  if(!decode_stream(data,size,range,form.content,4*1024*1024))return false;
  if(const Value* resource=get(range.dictionary,"Resources")){
   form.own_resources=true;
   Value scope;
   if(resource->kind==Value::Kind::Reference){
    if(!object(*resource,scope))return false;
   }else scope=*resource;
   if(scope.kind!=Value::Kind::Dictionary||
      get(scope,"Properties")||
      get(scope,"ColorSpace")||get(scope,"ExtGState")||
      get(scope,"Pattern")||get(scope,"Shading"))return false;
   if(const Value* fonts=get(scope,"Font")){
    Value dictionary;
    if(fonts->kind==Value::Kind::Reference){
     if(!object(*fonts,dictionary))return false;
    }else dictionary=*fonts;
    if(dictionary.kind!=Value::Kind::Dictionary||
       dictionary.dictionary.size()>64)return false;
    for(const auto& [alias,reference]:dictionary.dictionary){
     Value font;
     if(reference.kind==Value::Kind::Reference){
      if(!object(reference,font))return false;
     }else font=reference;
     PageFont local;
     if(!parse_font_dictionary(font,&scope,local)||
        (local.subtype!="Type0"&&local.subtype!="TrueType")||
        local.embedded_truetype.empty())return false;
     form.local_fonts.emplace(alias,std::move(local));
    }
   }
   if(const Value* x=get(scope,"XObject")){
    Value children;
    if(x->kind==Value::Kind::Reference){
     if(!object(*x,children))return false;
    }else children=*x;
    if(children.kind!=Value::Kind::Dictionary||
       children.dictionary.size()>128)return false;
    for(const auto& [alias,child_ref]:children.dictionary){
     if(child_ref.kind!=Value::Kind::Reference)return false;
     const XrefEntry* child_entry=nullptr;
     if(!entry(child_ref,child_entry))return false;
     StreamRange child_range;
     if(!locate_indirect_stream(data,size,*child_entry,xref,child_range))
      return false;
     const Value* child_type=get(child_range.dictionary,"Subtype");
     if(!child_type||child_type->kind!=Value::Kind::Name)return false;
     if(child_type->text=="Image"){
      const auto page=page_names.find({child_ref.integer,
                                        int64_t(child_ref.generation)});
      if(page!=page_names.end()){
       form.xobject_aliases.emplace(alias,page->second);
      }else{
       PageImage image;
       if(!decode_page_image(child_range,image))return false;
       form.local_images.emplace(alias,std::move(image));
      }
     }else if(child_type->text=="Form"){
      PageForm child;
      if(!load_local_form(child_ref,depth+1,visiting,page_names,child))
       return false;
      form.local_forms.emplace(alias,std::move(child));
     }else return false;
    }
   }
  }
  visiting.erase(id);
  return true;
 }
 bool walk_forms(const Value& ref,size_t depth,size_t& current,size_t target,
  std::set<size_t>& active,const Value* inherited,
  std::map<std::string,PageForm>& output) const {
  if(depth>64||ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  const size_t id=size_t(ref.integer);
  if(!active.insert(id).second)return false;
  Value node,owned;
  if(!object(ref,node)){active.erase(id);return false;}
  if(const Value* resources=get(node,"Resources")){
   if(resources->kind==Value::Kind::Reference){
    if(!object(*resources,owned)){active.erase(id);return false;}
   }else owned=*resources;
   if(owned.kind!=Value::Kind::Dictionary){active.erase(id);return false;}
   inherited=&owned;
  }
  const Value* type=get(node,"Type");
  if(!type||type->kind!=Value::Kind::Name){active.erase(id);return false;}
  if(type->text=="Page"){
   if(current==target&&inherited){
    if(const Value* x=get(*inherited,"XObject")){
     Value entries;
     if(x->kind==Value::Kind::Reference){
      if(!object(*x,entries)){active.erase(id);return false;}
     }else entries=*x;
     if(entries.kind!=Value::Kind::Dictionary||entries.dictionary.size()>256){
      active.erase(id);return false;
     }
     // Build a reverse lookup for objects already resolved at page scope.
     // A Form may rename these entries in its own XObject dictionary.
     std::map<std::pair<int64_t,int64_t>,std::string> page_names;
     for(const auto& [page_name,page_ref]:entries.dictionary)
      if(page_ref.kind==Value::Kind::Reference)
       page_names.emplace(std::make_pair(page_ref.integer,
                           int64_t(page_ref.generation)),page_name);
     for(const auto& [name,objref]:entries.dictionary){
      if(objref.kind!=Value::Kind::Reference){active.erase(id);return false;}
      const XrefEntry* ent=nullptr;
      if(!entry(objref,ent)){active.erase(id);return false;}
      StreamRange range;
      if(!locate_indirect_stream(data,size,*ent,xref,range)){
       active.erase(id);return false;
      }
      const Value* subtype=get(range.dictionary,"Subtype");
      if(!subtype||subtype->kind!=Value::Kind::Name){active.erase(id);return false;}
      if(subtype->text!="Form")continue;
      PageForm form;
      std::set<size_t> visiting;
      if(!load_local_form(objref,0,visiting,page_names,form)){
       active.erase(id);return false;
      }
      output.emplace(name,std::move(form));
     }
    }
   }
   ++current;active.erase(id);return true;
  }
  if(type->text!="Pages"){active.erase(id);return false;}
  const Value* kids=get(node,"Kids");
  if(!kids||kids->kind!=Value::Kind::Array){active.erase(id);return false;}
  for(const Value& child:kids->array){
   if(!walk_forms(child,depth+1,current,target,active,inherited,output)){
    active.erase(id);return false;
   }
  }
  active.erase(id);return true;
 }
 bool walk_properties(const Value& ref,size_t depth,size_t& current,size_t target,
  std::set<size_t>& active,const Value* inherited,PageProperties& output) const {
  if(depth>64||ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  const size_t id=static_cast<size_t>(ref.integer);
  if(!active.insert(id).second)return false;
  Value node,owned_resources;
  if(!object(ref,node)){active.erase(id);return false;}
  if(const Value* resources=get(node,"Resources")){
   if(resources->kind==Value::Kind::Reference){
    if(!object(*resources,owned_resources)){active.erase(id);return false;}
   }else owned_resources=*resources;
   if(owned_resources.kind!=Value::Kind::Dictionary){active.erase(id);return false;}
   inherited=&owned_resources;
  }
  const Value* type=get(node,"Type");
  if(!type||type->kind!=Value::Kind::Name){active.erase(id);return false;}
  if(type->text=="Page"){
   if(current==target&&inherited){
    if(const Value* source=get(*inherited,"Properties")){
     Value properties;
     if(source->kind==Value::Kind::Reference){
      if(!object(*source,properties)){active.erase(id);return false;}
     }else properties=*source;
     if(properties.kind!=Value::Kind::Dictionary||
        properties.dictionary.size()>4096){active.erase(id);return false;}
     for(const auto& [key,value]:properties.dictionary){
      Value dictionary;
      if(value.kind==Value::Kind::Reference){
       if(!object(value,dictionary)){active.erase(id);return false;}
      }else dictionary=value;
      if(dictionary.kind!=Value::Kind::Dictionary){active.erase(id);return false;}
      PageProperty property;
      if(const Value* lang=get(dictionary,"Lang")){
       if(lang->kind!=Value::Kind::String||lang->text.size()>64){
        active.erase(id);return false;
       }
       property.language=lang->text;
      }
      if(const Value* actual=get(dictionary,"ActualText")){
       if(actual->kind!=Value::Kind::String||actual->text.size()>4*1024*1024){
        active.erase(id);return false;
       }
       property.actual_text=actual->text;
       property.has_actual_text=true;
      }
      output.emplace(key,std::move(property));
     }
    }
   }
   ++current;active.erase(id);return true;
  }
  if(type->text!="Pages"){active.erase(id);return false;}
  const Value* kids=get(node,"Kids");
  if(!kids||kids->kind!=Value::Kind::Array){active.erase(id);return false;}
  for(const auto& child:kids->array)
   if(!walk_properties(child,depth+1,current,target,active,inherited,output)){
    active.erase(id);return false;
   }
  active.erase(id);return true;
 }
 bool walk_fonts(const Value& ref,size_t depth,size_t& current,size_t target,
  std::set<size_t>& active,const Value* inherited,
  std::map<std::string,PageFont>& output,
  const std::string& catalog_language) const {
  if(depth>64||ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  const size_t id=static_cast<size_t>(ref.integer);
  if(!active.insert(id).second)return false;
  Value node;
  if(!object(ref,node)){active.erase(id);return false;}
  Value owned_resources;
  if(const Value* resources=get(node,"Resources")){
   if(resources->kind==Value::Kind::Reference){
    if(!object(*resources,owned_resources)){active.erase(id);return false;}
   }else owned_resources=*resources;
   if(owned_resources.kind!=Value::Kind::Dictionary){active.erase(id);return false;}
   inherited=&owned_resources;
  }
  const Value* type=get(node,"Type");
  if(!type||type->kind!=Value::Kind::Name){active.erase(id);return false;}
  if(type->text=="Page"){
   if(current==target&&inherited){
    Value font_dictionary;
    if(const Value* fonts=get(*inherited,"Font")){
     if(fonts->kind==Value::Kind::Reference){
      if(!object(*fonts,font_dictionary)){active.erase(id);return false;}
     }else font_dictionary=*fonts;
     if(font_dictionary.kind!=Value::Kind::Dictionary||
        font_dictionary.dictionary.size()>4096){active.erase(id);return false;}
     for(const auto& [key,entry]:font_dictionary.dictionary){
      Value font;
      if(entry.kind==Value::Kind::Reference){
       if(!object(entry,font)){active.erase(id);return false;}
      }else font=entry;
      PageFont resource;
      if(!parse_font_dictionary(font,inherited,resource)){
       active.erase(id);return false;
      }
      resource.language=catalog_language;
      if(const Value* lang=get(node,"Lang")){
       if(lang->kind!=Value::Kind::String||lang->text.size()>64){
        active.erase(id);return false;
       }
       resource.language=lang->text;
      }
      output.emplace(key,std::move(resource));
     }
    }
   }
   ++current;active.erase(id);return true;
  }
  if(type->text!="Pages"){active.erase(id);return false;}
  const Value* kids=get(node,"Kids");
  if(!kids||kids->kind!=Value::Kind::Array){active.erase(id);return false;}
  for(const Value& child:kids->array)
   if(!walk_fonts(child,depth+1,current,target,active,inherited,output,catalog_language)){
    active.erase(id);return false;
   }
  active.erase(id);return true;
 }
 bool walk_box(const Value& ref,size_t depth,size_t& current,size_t target,
               std::set<size_t>& active,const MediaBox* inherited,const MediaBox* inherited_crop,unsigned inherited_rotation,MediaBox& output) const {
  if(depth>64||ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  size_t id=(size_t)ref.integer;
  if(!active.insert(id).second)return false;
  Value node;
  if(!object(ref,node)){active.erase(id);return false;}
  MediaBox own{};
  const Value *media=get(node,"MediaBox");
  if(media){if(!box_value(*media,own)){active.erase(id);return false;}inherited=&own;}
  const Value *rotate=get(node,"Rotate");
  if(rotate){
   if(rotate->kind!=Value::Kind::Integer||rotate->integer%90!=0){active.erase(id);return false;}
   long long angle=rotate->integer%360;
   if(angle<0)angle+=360;
   inherited_rotation=(unsigned)angle;
  }
  MediaBox own_crop{};
  const Value *crop=get(node,"CropBox");
  if(crop){if(!box_value(*crop,own_crop)){active.erase(id);return false;}inherited_crop=&own_crop;}
  const Value *type=get(node,"Type");
  if(!type||type->kind!=Value::Kind::Name){active.erase(id);return false;}
  if(type->text=="Page"){
   if(current==target){if(!inherited){active.erase(id);return false;}
    output=*inherited;
    output.rotation=inherited_rotation;
    if(inherited_crop){
     output.x0=std::max(output.x0,inherited_crop->x0);
     output.y0=std::max(output.y0,inherited_crop->y0);
     output.x1=std::min(output.x1,inherited_crop->x1);
     output.y1=std::min(output.y1,inherited_crop->y1);
     if(output.x0>=output.x1||output.y0>=output.y1){active.erase(id);return false;}
    }}
   ++current;active.erase(id);return true;
  }
  if(type->text!="Pages"){active.erase(id);return false;}
  const Value *kids=get(node,"Kids");
  if(!kids||kids->kind!=Value::Kind::Array){active.erase(id);return false;}
  for(const Value& child:kids->array){
   if(!walk_box(child,depth+1,current,target,active,inherited,inherited_crop,inherited_rotation,output)){active.erase(id);return false;}
  }
  active.erase(id);return true;
 }
 bool walk(const Value& ref,size_t depth,size_t& current,size_t target,
           std::set<size_t>& active,std::vector<uint8_t>& result,size_t limit) const {
  if(depth>64||ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  size_t id=(size_t)ref.integer;
  if(!active.insert(id).second)return false;
  Value node;
  if(!object(ref,node)){active.erase(id);return false;}
  const Value *type=get(node,"Type");
  if(!type||type->kind!=Value::Kind::Name){active.erase(id);return false;}
  if(type->text=="Page"){
   bool ok=true;
   if(current==target){
    const Value *contents=get(node,"Contents");
    if(contents){
     if(contents->kind==Value::Kind::Reference)ok=stream(*contents,result,limit);
     else if(contents->kind==Value::Kind::Array){
      for(const Value& child:contents->array){
       std::vector<uint8_t> part;
       if(!stream(child,part,limit-result.size())){ok=false;break;}
       result.insert(result.end(),part.begin(),part.end());
       if(result.size()<limit)result.push_back('\n');
      }
     }else ok=false;
    }
   }
   ++current;active.erase(id);return ok;
  }
  if(type->text!="Pages"){active.erase(id);return false;}
  const Value *kids=get(node,"Kids");
  if(!kids||kids->kind!=Value::Kind::Array){active.erase(id);return false;}
  for(const Value& child:kids->array){
   if(!walk(child,depth+1,current,target,active,result,limit)){active.erase(id);return false;}
  }
  active.erase(id);return true;
 }
};
bool read_page_contents(const uint8_t *data,size_t size,size_t index,std::vector<uint8_t>& contents,size_t limit){
 contents.clear();if(!data||limit==0)return false;
 std::vector<XrefEntry> xref;std::vector<XrefStreamEntry> compressed;Value trailer;
 if(!read_object_xref(data,size,xref,compressed,trailer))return false;
 if(get(trailer,"Encrypt"))return false;
 const Value *root=get(trailer,"Root");if(!root)return false;
 Reader reader{data,size,xref,compressed,{}};Value catalog;
 if(!reader.object(*root,catalog))return false;
 const Value *type=get(catalog,"Type"),*pages=get(catalog,"Pages");
 if(!type||type->kind!=Value::Kind::Name||type->text!="Catalog"||!pages)return false;
 std::set<size_t> active;size_t current=0;
 return reader.walk(*pages,0,current,index,active,contents,limit)&&index<current;
}
bool read_page_box(const uint8_t *data,size_t size,size_t index,MediaBox& box){
 box={0,0,0,0};if(!data)return false;
 std::vector<XrefEntry> xref;std::vector<XrefStreamEntry> compressed;Value trailer;
 if(!read_object_xref(data,size,xref,compressed,trailer))return false;
 if(get(trailer,"Encrypt"))return false;
 const Value *root=get(trailer,"Root");if(!root)return false;
 Reader reader{data,size,xref,compressed,{}};Value catalog;
 if(!reader.object(*root,catalog))return false;
 const Value *type=get(catalog,"Type"),*pages=get(catalog,"Pages");
 if(!type||type->kind!=Value::Kind::Name||type->text!="Catalog"||!pages)return false;
 std::set<size_t> active;size_t current=0;
 return reader.walk_box(*pages,0,current,index,active,nullptr,nullptr,0,box)&&index<current;
}

bool read_page_fonts(const uint8_t* data,size_t size,size_t index,
 std::map<std::string,PageFont>& fonts){
 if(!data)return false;
 std::vector<XrefEntry> xref;std::vector<XrefStreamEntry> compressed;Value trailer;
 if(!read_object_xref(data,size,xref,compressed,trailer))return false;
 const Value* root=get(trailer,"Root");
 if(!root)return false;
 Reader reader{data,size,xref,compressed,{}};
 Value catalog;
 if(!reader.object(*root,catalog))return false;
 const Value* type=get(catalog,"Type"),*pages=get(catalog,"Pages");
 if(!type||type->kind!=Value::Kind::Name||type->text!="Catalog"||!pages)return false;
 std::map<std::string,PageFont> result;
 size_t current=0;std::set<size_t> active;
 std::string document_language;
 if(const Value* lang=get(catalog,"Lang")){
  if(lang->kind!=Value::Kind::String||lang->text.size()>64)return false;
  document_language=lang->text;
 }
 if(!reader.walk_fonts(*pages,0,current,index,active,nullptr,result,document_language)||index>=current)
  return false;
 fonts=std::move(result);
 return true;
}

bool read_page_properties(const uint8_t* data,size_t size,size_t index,
 PageProperties& properties){
 if(!data)return false;
 std::vector<XrefEntry> xref;
 std::vector<XrefStreamEntry> compressed;
 Value trailer;
 if(!read_object_xref(data,size,xref,compressed,trailer)||
    get(trailer,"Encrypt"))return false;
 const Value* root=get(trailer,"Root");
 if(!root)return false;
 Reader reader{data,size,xref,compressed,{}};
 Value catalog;
 if(!reader.object(*root,catalog))return false;
 const Value* type=get(catalog,"Type"),*pages=get(catalog,"Pages");
 if(!type||type->kind!=Value::Kind::Name||
    type->text!="Catalog"||!pages)return false;
 size_t current=0;
 std::set<size_t> active;
 PageProperties result;
 if(!reader.walk_properties(*pages,0,current,index,active,nullptr,result)||
    index>=current)return false;
 properties=std::move(result);return true;
}

bool read_page_images(const uint8_t* data,size_t size,size_t index,
 std::map<std::string,PageImage>& images){
 if(!data)return false;
 std::vector<XrefEntry> xref;std::vector<XrefStreamEntry> compressed;Value trailer;
 if(!read_object_xref(data,size,xref,compressed,trailer)||
    get(trailer,"Encrypt"))return false;
 const Value* root=get(trailer,"Root");if(!root)return false;
 Reader reader{data,size,xref,compressed,{}};
 Value catalog;
 if(!reader.object(*root,catalog))return false;
 const Value* type=get(catalog,"Type"),*pages=get(catalog,"Pages");
 if(!type||type->kind!=Value::Kind::Name||type->text!="Catalog"||!pages)return false;
 size_t current=0;std::set<size_t> active;
 std::map<std::string,PageImage> result;
 if(!reader.walk_images(*pages,0,current,index,active,nullptr,result)||
    index>=current)return false;
 images=std::move(result);return true;
}

bool read_page_forms(const uint8_t* data,size_t size,size_t index,
 std::map<std::string,PageForm>& forms){
 if(!data)return false;
 std::vector<XrefEntry> xref;std::vector<XrefStreamEntry> compressed;Value trailer;
 if(!read_object_xref(data,size,xref,compressed,trailer)||get(trailer,"Encrypt"))
  return false;
 const Value* root=get(trailer,"Root");if(!root)return false;
 Reader reader{data,size,xref,compressed,{}};
 Value catalog;
 if(!reader.object(*root,catalog))return false;
 const Value* type=get(catalog,"Type"),*pages=get(catalog,"Pages");
 if(!type||type->kind!=Value::Kind::Name||type->text!="Catalog"||!pages)
  return false;
 size_t current=0;std::set<size_t> active;
 std::map<std::string,PageForm> result;
 if(!reader.walk_forms(*pages,0,current,index,active,nullptr,result)||
    index>=current)return false;
 forms=std::move(result);return true;
}

}
