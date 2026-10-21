#include "rinpdf/rinpdf.h"
#include "../parser/xref_stream.h"
#include "../parser/object_resolver.h"
#include "../parser/object_stream.h"
#include "../parser/pages.h"
#include "../parser/value.h"
#include "../parser/lexer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <vector>

namespace {
using V=rinpdf::parser::Value;
using K=V::Kind;
const V* field(const V& v,const char* key) {
 if(v.kind!=K::Dictionary)return nullptr;
 auto it=v.dictionary.find(key);
 return it==v.dictionary.end()?nullptr:&it->second;
}
bool named(const V* v,const char* name){return v&&v->kind==K::Name&&v->text==name;}
std::string name(const std::string& s){
 const char* hex="0123456789ABCDEF";std::string out="/";
 for(unsigned char c:s){
  if(c<=32||c>=127||c=='#'||c=='/'||c=='%'||c=='<'||c=='>'||
     c=='('||c==')'||c=='['||c==']'||c=='{'||c=='}'){
   out+='#';out+=hex[c>>4];out+=hex[c&15];
  }else out+=(char)c;
 }
 return out;
}
bool rebase(V& v,size_t offset,const std::vector<int>& generations,unsigned depth=0){
 if(depth>64)return false;
 if(v.kind==K::Reference){
  if(v.integer<=0||size_t(v.integer)>=generations.size()||
     size_t(v.integer)>1000000-offset||generations[size_t(v.integer)]<0||
     v.generation!=(unsigned)generations[size_t(v.integer)])return false;
  v.integer+=(long long)offset;v.generation=0;
 }
 if(v.kind==K::Array)for(auto& item:v.array)if(!rebase(item,offset,generations,depth+1))return false;
 if(v.kind==K::Dictionary)for(auto& entry:v.dictionary)
  if(!rebase(entry.second,offset,generations,depth+1))return false;
 return true;
}
bool encode(const V& v,std::string& s,unsigned depth=0){
 if(depth>64)return false;
 switch(v.kind){
 case K::Null:s+="null";return true;
 case K::Boolean:s+=v.boolean?"true":"false";return true;
 case K::Integer:s+=std::to_string(v.integer);return true;
 case K::Real:{
  if(!std::isfinite(v.real))return false;
  char buf[64];std::snprintf(buf,sizeof(buf),"%.12g",v.real);s+=buf;return true;
 }
 case K::Name:s+=name(v.text);return true;
 case K::String:{
  s+='<';const char* hex="0123456789ABCDEF";
  for(unsigned char c:v.text){s+=hex[c>>4];s+=hex[c&15];}
  s+='>';return true;
 }
 case K::Reference:
  s+=std::to_string(v.integer)+" "+std::to_string(v.generation)+" R";return true;
 case K::Array:
  s+="[ ";for(const V& x:v.array){if(!encode(x,s,depth+1))return false;s+=' ';}s+=']';return true;
 case K::Dictionary:
  s+="<< ";for(const auto& [key,x]:v.dictionary){
   s+=name(key);s+=' ';if(!encode(x,s,depth+1))return false;s+=' ';
  }s+=">>";return true;
 default:return false;
 }
}
struct Input{
 const uint8_t* bytes=nullptr;size_t length=0,offset=0,max_id=0,root_pages=0,page_count=0;
 std::vector<rinpdf::parser::XrefEntry> xref;
 std::vector<rinpdf::parser::XrefStreamEntry> compressed;
 std::vector<std::string> objects;
 std::vector<V> values;
 std::vector<std::string> tails;
 std::vector<bool> present;
 std::vector<int> generations;
 std::vector<std::vector<size_t>> dependencies;
 std::vector<bool> reachable;
};
void gather_refs(const V& value,size_t offset,std::vector<size_t>& refs,unsigned depth=0){
 if(depth>64)return;
 if(value.kind==K::Reference){
  if(value.integer>0&&(size_t)value.integer>offset)
   refs.push_back((size_t)value.integer-offset);
 }else if(value.kind==K::Array){
  for(const auto& item:value.array)gather_refs(item,offset,refs,depth+1);
 }else if(value.kind==K::Dictionary){
  for(const auto& item:value.dictionary)gather_refs(item.second,offset,refs,depth+1);
 }
}

bool collect_page_leaves(const Input& doc,const V& ref,
 const std::map<std::string,V>& inherited,std::vector<V>& leaves,
 std::vector<size_t>& active,unsigned depth=0){
 if(depth>64||ref.kind!=K::Reference||ref.integer<=0)return false;
 size_t id=(size_t)ref.integer;
 if(std::find(active.begin(),active.end(),id)!=active.end())return false;
 active.push_back(id);
 V node;
 if(!rinpdf::parser::resolve_reference_value(doc.bytes,doc.length,doc.xref,
      doc.compressed,ref,node)||node.kind!=K::Dictionary)return false;
 auto properties=inherited;
 for(const char* key:{"Resources","MediaBox","CropBox","Rotate","TrimBox","BleedBox","ArtBox"}){
  if(const V* item=field(node,key))properties[key]=*item;
 }
 if(named(field(node,"Type"),"Page")){
  for(const auto& [key,item]:properties)if(!field(node,key))node.dictionary[key]=item;
  node.dictionary.erase("Parent");
  leaves.push_back(std::move(node));
 }else if(named(field(node,"Type"),"Pages")){
  const V* kids=field(node,"Kids");
  if(!kids||kids->kind!=K::Array||kids->array.size()>100000)return false;
  for(const V& child:kids->array)
   if(!collect_page_leaves(doc,child,properties,leaves,active,depth+1))return false;
 }else return false;
 active.pop_back();
 return leaves.size()<=100000;
}
bool apply_page_box(V& node,const char* key,const double* coordinates){
 if(!std::isfinite(coordinates[0])||!std::isfinite(coordinates[1])||
    !std::isfinite(coordinates[2])||!std::isfinite(coordinates[3])||
    coordinates[0]>=coordinates[2]||coordinates[1]>=coordinates[3])return false;
 V box;box.kind=K::Array;
 for(unsigned i=0;i<4;++i){V v;v.kind=K::Real;v.real=coordinates[i];box.array.push_back(v);}
 node.dictionary[key]=std::move(box);return true;
}
bool apply_page_geometry(V& node,const rinpdf_page_box_override& edit){
 if(edit.set_rotation){
  if(edit.rotation!=0&&edit.rotation!=90&&edit.rotation!=180&&edit.rotation!=270)return false;
  V rotate;rotate.kind=K::Integer;rotate.integer=edit.rotation;
  node.dictionary["Rotate"]=rotate;
 }
 return (!edit.set_media_box||apply_page_box(node,"MediaBox",edit.media_box))&&
 (!edit.set_crop_box||apply_page_box(node,"CropBox",edit.crop_box))&&
 (!edit.set_trim_box||apply_page_box(node,"TrimBox",edit.trim_box))&&
 (!edit.set_bleed_box||apply_page_box(node,"BleedBox",edit.bleed_box))&&
 (!edit.set_art_box||apply_page_box(node,"ArtBox",edit.art_box));
}

bool compact_references(V& node,const std::map<size_t,size_t>& mapping,unsigned depth=0){
 if(depth>64)return false;
 if(node.kind==K::Reference){
  if(node.integer<=0)return false;
  auto it=mapping.find((size_t)node.integer);
  if(it==mapping.end())return false;
  node.integer=(long long)it->second;node.generation=0;
 }else if(node.kind==K::Array){
  for(auto& child:node.array)if(!compact_references(child,mapping,depth+1))return false;
 }else if(node.kind==K::Dictionary){
  for(auto& entry:node.dictionary)
   if(!compact_references(entry.second,mapping,depth+1))return false;
 }
 return true;
}
bool resolve_length(const Input& doc,const V& length,size_t& out){
 V resolved=length;
 if(length.kind==K::Reference){
  if(!rinpdf::parser::resolve_reference_value(doc.bytes,doc.length,doc.xref,
                                               doc.compressed,length,resolved))return false;
 }
 if(resolved.kind!=K::Integer||resolved.integer<0||
    (unsigned long long)resolved.integer>doc.length)return false;
 out=(size_t)resolved.integer;
 return true;
}
bool prepare(Input& doc,bool serialize_objects=true){
 V trailer;
 // read_object_xref resolves /Prev revision chains and hybrid /XRefStm.
 // Only the latest live entries are exported; obsolete revisions are discarded.
 if(!rinpdf::parser::read_object_xref(doc.bytes,doc.length,doc.xref,doc.compressed,trailer)||
    field(trailer,"Encrypt"))return false;
 // Type-2 entries are decoded and materialized into ordinary indirect objects.
 const V* root_ref=field(trailer,"Root");
 if(!root_ref||root_ref->kind!=K::Reference||root_ref->integer<=0)return false;
 V catalog;
 if(!rinpdf::parser::resolve_reference_value(doc.bytes,doc.length,doc.xref,doc.compressed,
                                              *root_ref,catalog))return false;
 if(!named(field(catalog,"Type"),"Catalog"))return false;
 const V* pages=field(catalog,"Pages");
 if(!pages||pages->kind!=K::Reference||pages->integer<=0)return false;
 doc.root_pages=(size_t)pages->integer;
 if(!rinpdf::parser::count_page_tree(doc.bytes,doc.length,doc.page_count)||
    doc.page_count==0)return false;
 for(const auto& item:doc.xref){
  if(item.object>100000)return false;
  doc.max_id=std::max(doc.max_id,item.object);
 }
 for(const auto& item:doc.compressed){
  if(item.object>100000)return false;
  doc.max_id=std::max(doc.max_id,item.object);
 }
 doc.objects.resize(doc.max_id+1);
 doc.values.resize(doc.max_id+1);
 doc.tails.resize(doc.max_id+1);
 doc.present.assign(doc.max_id+1,false);
 doc.generations.assign(doc.max_id+1,-1);
 doc.dependencies.resize(doc.max_id+1);
 doc.reachable.assign(doc.max_id+1,false);
 std::vector<rinpdf::parser::XrefEntry> valid;
 for(const auto& entry:doc.xref){
  if(!entry.in_use)continue;
  if(entry.object==0||entry.offset>=doc.length||doc.present[entry.object])return false;
  doc.present[entry.object]=true;
  doc.generations[entry.object]=(int)entry.generation;valid.push_back(entry);
 }
 for(const auto& entry:doc.compressed){
  if(entry.type!=2)continue;
  if(entry.object==0||entry.object>doc.max_id||doc.present[entry.object])return false;
  doc.generations[entry.object]=0;
 }
 std::sort(valid.begin(),valid.end(),[](const auto& a,const auto& b){return a.offset<b.offset;});
 for(size_t i=0;i<valid.size();++i){
  const auto& entry=valid[i];
  const size_t limit=i+1<valid.size()?valid[i+1].offset:doc.length;
  if(limit<=entry.offset)return false;
  const uint8_t* ptr=doc.bytes+entry.offset;
  rinpdf::parser::Lexer header(ptr,limit-entry.offset);
  auto id=header.next(),gen=header.next(),obj=header.next();
  if(id.kind!=rinpdf::parser::TokenKind::Integer||
     gen.kind!=rinpdf::parser::TokenKind::Integer||
     obj.kind!=rinpdf::parser::TokenKind::Keyword||obj.value!="obj"||
     id.value!=std::to_string(entry.object)||gen.value!=std::to_string(entry.generation))return false;
  const size_t body=entry.offset+header.position();
  rinpdf::parser::Lexer parser(doc.bytes+body,limit-body);
  V value;
  if(!rinpdf::parser::parse_value(parser,value))return false;
  const size_t parsed_end=body+parser.position();
  // Stream contents are binary, so only the dictionary is decoded and rewritten.
  // The encoded bytes following the dictionary are preserved verbatim.
  rinpdf::parser::Lexer remainder(doc.bytes+parsed_end,limit-parsed_end);
  auto token=remainder.next();
  std::string tail;
  if(token.kind==rinpdf::parser::TokenKind::Keyword&&token.value=="stream"){
   if(value.kind!=K::Dictionary)return false;
   const V* length=field(value,"Length");
   // Resolve indirect /Length without modifying encoded stream bytes.
   size_t n=0;
   if(!length||!resolve_length(doc,*length,n))return false;
   size_t start=parsed_end+remainder.position();
   if(start>=limit)return false;
   if(doc.bytes[start]=='\r'&&start+1<limit&&doc.bytes[start+1]=='\n')start+=2;
   else if(doc.bytes[start]=='\n')++start;
   else return false;
   if(n>limit-start)return false;
   size_t end=start+n;
   size_t p=end;
   while(p<limit&&(doc.bytes[p]=='\r'||doc.bytes[p]=='\n'||doc.bytes[p]==' '))++p;
   if(limit-p<9||std::memcmp(doc.bytes+p,"endstream",9)!=0)return false;
   p+=9;
   rinpdf::parser::Lexer ending(doc.bytes+p,limit-p);
   auto endobj=ending.next();
   if(endobj.kind!=rinpdf::parser::TokenKind::Keyword||endobj.value!="endobj")return false;
   tail=" \nstream\n";
   tail.append((const char*)doc.bytes+start,n);
   tail+="\nendstream";
  }else if(token.kind!=rinpdf::parser::TokenKind::Keyword||token.value!="endobj")
   return false;
  if(entry.object==doc.root_pages){
   if(value.kind!=K::Dictionary||!named(field(value,"Type"),"Pages"))return false;
  }
  if(!rebase(value,doc.offset,doc.generations))return false;
  // Parent of the source root points to the new merged Pages node.
  if(entry.object==doc.root_pages){
   if(value.kind!=K::Dictionary||!named(field(value,"Type"),"Pages"))return false;
   // The imported Pages root is attached to our new Pages dictionary.
   // Its synthetic Parent is *not* an input-document reference.
   value.dictionary.erase("Parent");
   gather_refs(value,doc.offset,doc.dependencies[entry.object]);
   V parent;parent.kind=K::Reference;parent.integer=2;
   value.dictionary["Parent"]=parent;
  }else gather_refs(value,doc.offset,doc.dependencies[entry.object]);
  doc.values[entry.object]=std::move(value);
  doc.tails[entry.object]=std::move(tail);
  if(serialize_objects){
   std::string serialized;
   if(!encode(doc.values[entry.object],serialized))return false;
   doc.objects[entry.object]=std::move(serialized)+doc.tails[entry.object];
  }
 }
 for(const auto& entry:doc.compressed){
  if(entry.type!=2)continue;
  if(entry.object==0||entry.object>doc.max_id||doc.present[entry.object])return false;
  V value;
  if(!rinpdf::parser::resolve_compressed_object(doc.bytes,doc.length,
                                                 doc.compressed,entry.object,value))return false;
  if(!rebase(value,doc.offset,doc.generations))return false;
  if(entry.object==doc.root_pages){
   if(value.kind!=K::Dictionary||!named(field(value,"Type"),"Pages"))return false;
   value.dictionary.erase("Parent");
   gather_refs(value,doc.offset,doc.dependencies[entry.object]);
   V parent;parent.kind=K::Reference;parent.integer=2;
   value.dictionary["Parent"]=parent;
  }else gather_refs(value,doc.offset,doc.dependencies[entry.object]);
  doc.values[entry.object]=std::move(value);
  if(serialize_objects){
   std::string serialized;
   if(!encode(doc.values[entry.object],serialized))return false;
   doc.objects[entry.object]=std::move(serialized);
  }
  doc.present[entry.object]=true;
 }
 if(!doc.present[doc.root_pages])return false;
 // Reachability starts at the imported Pages root, not the source Catalog.
 // Each reference must point to a live object in the current xref revision.
 std::vector<size_t> pending{doc.root_pages};
 size_t visited=0;
 while(!pending.empty()){
  const size_t id=pending.back();pending.pop_back();
  if(id==0||id>doc.max_id||!doc.present[id])return false;
  if(doc.reachable[id])continue;
  if(++visited>100000)return false;
  doc.reachable[id]=true;
  for(size_t target:doc.dependencies[id]){
   if(target==0||target>doc.max_id||!doc.present[target])return false;
   if(target==0||target>doc.max_id||!doc.present[target])return false;
   if(!doc.reachable[target])pending.push_back(target);
  }
 }
 return true;
}
}
namespace rinpdf {

rinpdf_status compose_selected_pdf_bytes(
 const uint8_t* const* bytes,const size_t* lengths,size_t document_count,
 const size_t* document_indices,const size_t* page_indices,
 const rinpdf_page_box_override* geometry,size_t count,uint8_t** out,size_t* size){
 if(!out||!size)return RINPDF_INVALID_ARGUMENT;
 *out=nullptr;*size=0;
 if(!bytes||!lengths||!document_indices||!page_indices||!document_count||
    document_count>128||!count||count>10000)return RINPDF_INVALID_ARGUMENT;
 try{
  std::vector<Input> docs(document_count);
  std::vector<std::vector<V>> leaves(document_count);
  size_t next=3;
  for(size_t i=0;i<document_count;++i){
   if(!bytes[i]||lengths[i]<8||lengths[i]>256*1024*1024)return RINPDF_INVALID_ARGUMENT;
   auto& doc=docs[i];doc.bytes=bytes[i];doc.length=lengths[i];doc.offset=next-1;
   if(!prepare(doc,false))return RINPDF_UNSUPPORTED;
   if(doc.max_id>=1000000-next)return RINPDF_UNSUPPORTED;
   next+=doc.max_id;
   V trailer;
   std::vector<rinpdf::parser::XrefEntry> ordinary;
   std::vector<rinpdf::parser::XrefStreamEntry> compressed;
   if(!rinpdf::parser::read_object_xref(bytes[i],lengths[i],ordinary,compressed,trailer))
    return RINPDF_UNSUPPORTED;
   const V* catalog_ref=field(trailer,"Root");
   V catalog;
   if(!catalog_ref||!rinpdf::parser::resolve_reference_value(bytes[i],lengths[i],
      ordinary,compressed,*catalog_ref,catalog))return RINPDF_UNSUPPORTED;
   const V* page_root=field(catalog,"Pages");
   std::vector<size_t> active;
   if(!page_root||!collect_page_leaves(doc,*page_root,{},leaves[i],active))
    return RINPDF_UNSUPPORTED;
  }
  if(count>1000000-next)return RINPDF_UNSUPPORTED;
  const size_t first_page=next,last_id=first_page+count-1;
  std::vector<V> selected;selected.reserve(count);
  for(auto& doc:docs)std::fill(doc.reachable.begin(),doc.reachable.end(),false);
  for(size_t i=0;i<count;++i){
   const size_t d=document_indices[i];
   if(d>=document_count)return RINPDF_INVALID_ARGUMENT;
   if(page_indices[i]>=leaves[d].size())return RINPDF_INVALID_ARGUMENT;
   Input& doc=docs[d];
   V page=leaves[d][page_indices[i]];
   if(geometry&&!apply_page_geometry(page,geometry[i]))return RINPDF_INVALID_ARGUMENT;
   if(!rebase(page,doc.offset,doc.generations))return RINPDF_UNSUPPORTED;
   std::vector<size_t> pending;
   gather_refs(page,doc.offset,pending);
   while(!pending.empty()){
    size_t id=pending.back();pending.pop_back();
    if(id==0||id>=doc.present.size()||!doc.present[id])return RINPDF_UNSUPPORTED;
    if(doc.reachable[id])continue;
    doc.reachable[id]=true;
    for(size_t child:doc.dependencies[id]){
     if(child==0||child>=doc.present.size()||!doc.present[child])return RINPDF_UNSUPPORTED;
     if(!doc.reachable[child])pending.push_back(child);
    }
   }
   V parent;parent.kind=K::Reference;parent.integer=2;
   page.dictionary["Parent"]=parent;
   selected.push_back(std::move(page));
  }
  // Assign compact IDs only to the live dependency closure. Distinct
  // source PDFs share no IDs; all selected page dictionaries are new.
  std::map<size_t,size_t> mapping;
  size_t allocated=3;
  for(const auto& doc:docs)
   for(size_t id=1;id<doc.objects.size();++id)
    if(doc.reachable[id])mapping.emplace(id+doc.offset,allocated++);
  const size_t first_compact_page=allocated;
  if(count>1000000-allocated)return RINPDF_UNSUPPORTED;
  allocated+=count;
  std::string pdf="%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
  std::vector<size_t> positions(allocated,0);
  auto add=[&](size_t id,const std::string& body){
   positions[id]=pdf.size();
   pdf+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
  };
  add(1,"<< /Type /Catalog /Pages 2 0 R >>");
  std::string kids="[ ";
  for(size_t i=0;i<count;++i)
   kids+=std::to_string(first_compact_page+i)+" 0 R ";
  kids+="]";
  add(2,"<< /Type /Pages /Kids "+kids+" /Count "+std::to_string(count)+" >>");
  for(const auto& doc:docs)
   for(size_t id=1;id<doc.objects.size();++id){
    if(!doc.reachable[id])continue;
    V value=doc.values[id];
    // An imported /Pages root already received a synthetic parent during
    // preparation. Do not confuse that reference with a source object ID.
    const bool imported_root=id==doc.root_pages;
    if(imported_root&&value.kind==K::Dictionary)value.dictionary.erase("Parent");
    if(!compact_references(value,mapping))return RINPDF_UNSUPPORTED;
    if(imported_root){
     V parent;parent.kind=K::Reference;parent.integer=2;
     value.dictionary["Parent"]=parent;
    }
    std::string serialized;
    if(!encode(value,serialized))return RINPDF_UNSUPPORTED;
    add(mapping.at(id+doc.offset),serialized+doc.tails[id]);
   }
  for(size_t i=0;i<count;++i){
   V page=selected[i];
   // /Parent 2 0 R targets the synthetic document root, not a source object.
   page.dictionary.erase("Parent");
   if(!compact_references(page,mapping))return RINPDF_UNSUPPORTED;
   V parent;parent.kind=K::Reference;parent.integer=2;
   page.dictionary["Parent"]=parent;
   std::string serialized;
   if(!encode(page,serialized))return RINPDF_UNSUPPORTED;
   add(first_compact_page+i,serialized);
  }
  const size_t xref=pdf.size();
  pdf+="xref\n0 "+std::to_string(positions.size())+"\n0000000000 65535 f \n";
  char line[40];
  for(size_t i=1;i<positions.size();++i){
   if(positions[i]>9999999999ULL)return RINPDF_UNSUPPORTED;
   std::snprintf(line,sizeof(line),"%010zu 00000 %c \n",positions[i],
      positions[i]?'n':'f');pdf+=line;
  }
  pdf+="trailer\n<< /Size "+std::to_string(positions.size())+
    " /Root 1 0 R >>\nstartxref\n"+std::to_string(xref)+"\n%%EOF\n";
  uint8_t* result=(uint8_t*)std::malloc(pdf.size());
  if(!result)return RINPDF_OUT_OF_MEMORY;
  std::memcpy(result,pdf.data(),pdf.size());
  *out=result;*size=pdf.size();
  return RINPDF_OK;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}

rinpdf_status merge_original_pdf_bytes(const uint8_t* const* sources,const size_t* lengths,
 size_t source_count,uint8_t** out,size_t* out_size){
 if(!out||!out_size)return RINPDF_INVALID_ARGUMENT;
 *out=nullptr;*out_size=0;
 if(!sources||!lengths||!source_count||source_count>128)return RINPDF_INVALID_ARGUMENT;
 try {
  std::vector<Input> inputs(source_count);
  size_t next_id=3,total_pages=0;
  for(size_t i=0;i<source_count;++i){
   if(!sources[i]||lengths[i]<8||lengths[i]>256*1024*1024)return RINPDF_INVALID_ARGUMENT;
   auto& input=inputs[i];input.bytes=sources[i];input.length=lengths[i];
   input.offset=next_id-1;
   if(!prepare(input))return RINPDF_UNSUPPORTED;
   if(input.max_id>=1000000-next_id)return RINPDF_UNSUPPORTED;
   next_id+=input.max_id;
   total_pages+=input.page_count;
  }
  std::vector<size_t> positions(next_id,0);
  std::string pdf="%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
  auto add=[&](size_t id,const std::string& content){
   positions[id]=pdf.size();
   pdf+=std::to_string(id)+" 0 obj\n"+content+"\nendobj\n";
  };
  add(1,"<< /Type /Catalog /Pages 2 0 R >>");
  std::string kids="[ ";
  for(const auto& input:inputs)
   kids+=std::to_string(input.root_pages+input.offset)+" 0 R ";
  kids+="]";
  add(2,"<< /Type /Pages /Kids "+kids+" /Count "+std::to_string(total_pages)+" >>");
  for(const auto& input:inputs)
   for(size_t id=1;id<input.objects.size();++id)
    if(input.reachable[id])add(id+input.offset,input.objects[id]);
  const size_t xref=pdf.size();
  pdf+="xref\n0 "+std::to_string(positions.size())+"\n0000000000 65535 f \n";
  char buf[40];
  for(size_t i=1;i<positions.size();++i){
   if(positions[i]>9999999999ULL)return RINPDF_UNSUPPORTED;
   std::snprintf(buf,sizeof(buf),"%010zu 00000 %c \n",positions[i],
                 positions[i]?'n':'f');
   pdf+=buf;
  }
  pdf+="trailer\n<< /Size "+std::to_string(positions.size())+
       " /Root 1 0 R >>\nstartxref\n"+std::to_string(xref)+"\n%%EOF\n";
  uint8_t* memory=(uint8_t*)std::malloc(pdf.size());
  if(!memory)return RINPDF_OUT_OF_MEMORY;
  std::memcpy(memory,pdf.data(),pdf.size());
  *out=memory;*out_size=pdf.size();
  return RINPDF_OK;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
}
