#pragma once
#include "object.h"
#include "object_stream.h"
#include "xref_stream.h"
#include <algorithm>
#include <map>
#include <utility>
namespace rinpdf::parser {
// Resolves ordinary and type-2 compressed dictionary/array values.
// Stream contents must still be ordinary (type 1) indirect objects.
inline bool resolve_reference_value(const uint8_t* data,size_t size,
 const std::vector<XrefEntry>& ordinary,
 const std::vector<XrefStreamEntry>& compressed,
 const Value& ref,Value& out,
 std::map<size_t,Value>* compressed_cache=nullptr){
 if(!data||ref.kind!=Value::Kind::Reference||ref.integer<=0||
    ref.integer>1000000)return false;
 const size_t id=static_cast<size_t>(ref.integer);
 auto it=std::find_if(ordinary.begin(),ordinary.end(),
  [&](const XrefEntry& entry){return entry.in_use&&entry.object==id&&entry.generation==ref.generation;});
 if(it!=ordinary.end()){
  size_t begin=0,end=0;
  if(!resolve_object(data,size,*it,begin,end)||end<=begin)return false;
  Lexer lex(data+begin,end-begin);
  Value result;
  if(!parse_value(lex,result)||lex.next().kind!=TokenKind::End)return false;
  out=std::move(result);
  return true;
 }
 if(ref.generation!=0||compressed.empty())return false;
 if(compressed_cache){
  auto cached=compressed_cache->find(id);
  if(cached!=compressed_cache->end()){out=cached->second;return true;}
 }
 Value resolved;
 if(!resolve_compressed_object(data,size,compressed,id,resolved))return false;
 // Bounded per-reader cache, with no partial entries on decoding failure.
 if(compressed_cache&&compressed_cache->size()<1024)
  compressed_cache->emplace(id,resolved);
 out=std::move(resolved);
 return true;
}
}
