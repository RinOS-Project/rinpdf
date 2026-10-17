#include "pages.h"
#include "xref.h"
#include "xref_stream.h"
#include "object.h"
#include "value.h"
#include <algorithm>
#include <set>
#include <vector>
namespace rinpdf::parser {
static const Value* field(const Value& v,const char* key){
 if(v.kind!=Value::Kind::Dictionary)return nullptr;
 auto it=v.dictionary.find(key);return it==v.dictionary.end()?nullptr:&it->second;
}
static bool named(const Value& v,const char* name){return v.kind==Value::Kind::Name&&v.text==name;}
struct Context {
 const uint8_t *bytes;size_t length;const std::vector<XrefEntry>& xref;
 std::set<size_t> active;size_t visited=0;
 bool load(const Value& ref,Value& out){
  if(ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  size_t id=(size_t)ref.integer;
  auto it=std::find_if(xref.begin(),xref.end(),[&](const XrefEntry& e){return e.object==id&&e.generation==ref.generation&&e.in_use;});
  if(it==xref.end())return false;
  size_t begin=0,end=0;
  if(!resolve_object(bytes,length,*it,begin,end)||end<=begin)return false;
  Lexer lexer(bytes+begin,end-begin);
  if(!parse_value(lexer,out))return false;
  return lexer.next().kind==TokenKind::End;
 }
 bool walk(const Value& ref,size_t depth,size_t& count){
  if(depth>64||visited++>=100000||ref.kind!=Value::Kind::Reference||ref.integer<=0)return false;
  size_t id=(size_t)ref.integer;
  if(!active.insert(id).second)return false;
  Value node;
  if(!load(ref,node)){active.erase(id);return false;}
  const Value* type=field(node,"Type");
  if(!type){active.erase(id);return false;}
  if(named(*type,"Page")){
   if(count==100000){active.erase(id);return false;}
   ++count;active.erase(id);return true;
  }
  if(!named(*type,"Pages")){active.erase(id);return false;}
  const Value* kids=field(node,"Kids"),*declared=field(node,"Count");
  if(!kids||kids->kind!=Value::Kind::Array||!declared||declared->kind!=Value::Kind::Integer||declared->integer<0){active.erase(id);return false;}
  size_t local=0;
  for(const Value& child:kids->array)if(!walk(child,depth+1,local)){active.erase(id);return false;}
  active.erase(id);
  if((unsigned long long)declared->integer!=local)return false;
  if(count>100000-local)return false;
  count+=local;return true;
 }
};
bool count_page_tree(const uint8_t *data,size_t size,size_t& count){
 count=0;std::vector<XrefEntry> entries;
 Value trailer;
 if(!read_uncompressed_xref(data,size,entries,trailer))return false;
 if(field(trailer,"Prev")||field(trailer,"XRefStm")||field(trailer,"Encrypt"))return false;
 const Value* root_ref=field(trailer,"Root");
 if(!root_ref||root_ref->kind!=Value::Kind::Reference)return false;
 Context ctx{data,size,entries};
 Value root;
 if(!ctx.load(*root_ref,root))return false;
 const Value* type=field(root,"Type");
 if(!type||!named(*type,"Catalog"))return false;
 const Value* pages=field(root,"Pages");
 return pages&&ctx.walk(*pages,0,count);
}
}
