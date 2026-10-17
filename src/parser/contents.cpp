#include "contents.h"
#include "xref.h"
#include "xref_stream.h"
#include "object.h"
#include "object_resolver.h"
#include "stream_object.h"
#include "stream.h"
#include "value.h"
#include "font_encoding.h"
#include "tounicode.h"
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
 bool resolve_unicode_cmap(const Value& ref,unsigned depth,
  std::set<size_t>& visiting,std::map<CMapKey,std::u32string>& output,
  std::vector<std::pair<CMapKey,CMapKey>>& spaces,
  std::string& cmap_name,const Value* page_resources=nullptr) const {
  if(depth>=8||ref.kind!=Value::Kind::Reference||ref.integer<=0||
     !visiting.insert(size_t(ref.integer)).second)return false;
  Value dictionary;
  std::vector<uint8_t> decoded;
  if(!object(ref,dictionary)||dictionary.kind!=Value::Kind::Dictionary||
     !stream(ref,decoded,1024*1024))return false;
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
      // Type0 fonts select a descendant CIDFont; CIDToGIDMap is distinct
      // from the ToUnicode extraction map and must not be confused with it.
      if(resource.subtype=="Type0"){
       const Value* descendants=get(font,"DescendantFonts");
       if(!descendants||descendants->kind!=Value::Kind::Array||
          descendants->array.size()!=1){active.erase(id);return false;}
       const Value& reference=descendants->array.front();
       Value descendant;
       if(reference.kind==Value::Kind::Reference){
        if(!object(reference,descendant)){active.erase(id);return false;}
       }else descendant=reference;
       const Value* child_type=get(descendant,"Subtype");
       if(!child_type||child_type->kind!=Value::Kind::Name||
          child_type->text!="CIDFontType2"){active.erase(id);return false;}
       if(!parse_cid_widths(get(descendant,"DW"),get(descendant,"W"),resource.cid_widths)){
        active.erase(id);return false;
       }
       if(!parse_cid_vertical_metrics(get(descendant,"DW2"),
            get(descendant,"W2"),resource.vertical_metrics)){
        active.erase(id);return false;
       }
       const Value* map=get(descendant,"CIDToGIDMap");
       if(map){
        if(map->kind==Value::Kind::Name){
         if(map->text!="Identity"){active.erase(id);return false;}
        }else if(map->kind==Value::Kind::Reference){
         resource.cid_map.identity=false;
         if(!stream(*map,resource.cid_map.bytes,131072)||
            !cid_map_valid(resource.cid_map)){active.erase(id);return false;}
        }else{active.erase(id);return false;}
       }
       // CIDFont's descriptor owns the embedded TrueType, not Type0's.
       // Parse the descendant descriptor in the common path below.
       if(const Value* child_descriptor=get(descendant,"FontDescriptor")){
        Value desc;
        if(child_descriptor->kind==Value::Kind::Reference){
         if(!object(*child_descriptor,desc)){active.erase(id);return false;}
        }else desc=*child_descriptor;
        if(desc.kind!=Value::Kind::Dictionary){active.erase(id);return false;}
        if(const Value* file=get(desc,"FontFile2")){
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
      if(const Value* cmap=get(font,"ToUnicode")){
       std::set<size_t> visiting;
       std::string cmap_name;
       if(!resolve_unicode_cmap(*cmap,0,visiting,resource.unicode_sequences,
            resource.unicode_codespaces,cmap_name,inherited)){
        active.erase(id);return false;
       }
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

}
