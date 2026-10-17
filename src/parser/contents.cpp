#include "contents.h"
#include "xref.h"
#include "xref_stream.h"
#include "object.h"
#include "object_resolver.h"
#include "stream_object.h"
#include "stream.h"
#include "value.h"
#include "font_encoding.h"
#include "../renderer/sfnt.h"
#include <algorithm>
#include <map>
#include <set>
#include <cmath>
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
 bool walk_fonts(const Value& ref,size_t depth,size_t& current,size_t target,
  std::set<size_t>& active,const Value* inherited,
  std::map<std::string,PageFont>& output) const {
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
      const Value* subtype=get(font,"Subtype"),*base=get(font,"BaseFont");
      if(font.kind!=Value::Kind::Dictionary||!subtype||!base||
         subtype->kind!=Value::Kind::Name||base->kind!=Value::Kind::Name){
       active.erase(id);return false;
      }
      PageFont resource{base->text,subtype->text,"",{}};
      if(const Value* encoding=get(font,"Encoding")){
       Value resolved;
       if(encoding->kind==Value::Kind::Reference){
        if(!object(*encoding,resolved)){active.erase(id);return false;}
       }else resolved=*encoding;
       if(resolved.kind==Value::Kind::Name)resource.encoding=resolved.text;
       else if(resolved.kind==Value::Kind::Dictionary){
        const Value* base_encoding=get(resolved,"BaseEncoding");
        const Value* differences=get(resolved,"Differences");
        if(!base_encoding||base_encoding->kind!=Value::Kind::Name||
           !differences||!decode_font_differences(*differences,resource.differences)){
         active.erase(id);return false;
        }
        resource.encoding=base_encoding->text;
       }else{active.erase(id);return false;}
      }
      if(const Value* descriptor=get(font,"FontDescriptor")){
       Value resolved_descriptor;
       if(descriptor->kind==Value::Kind::Reference){
        if(!object(*descriptor,resolved_descriptor)){active.erase(id);return false;}
       }else resolved_descriptor=*descriptor;
       if(resolved_descriptor.kind!=Value::Kind::Dictionary){
        active.erase(id);return false;
       }
       if(const Value* file=get(resolved_descriptor,"FontFile2")){
        if(file->kind!=Value::Kind::Reference||
           !stream(*file,resource.embedded_truetype,16*1024*1024)){
         active.erase(id);return false;
        }
        renderer::SfntFont sfnt;
        if(!renderer::parse_sfnt(resource.embedded_truetype.data(),
                                 resource.embedded_truetype.size(),sfnt)){
         active.erase(id);return false;
        }
       }
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
   if(!walk_fonts(child,depth+1,current,target,active,inherited,output)){
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
 if(!reader.walk_fonts(*pages,0,current,index,active,nullptr,result)||index>=current)
  return false;
 fonts=std::move(result);
 return true;
}

}
