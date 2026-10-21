#include "rinpdf/rinpdf.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>
#include <memory>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace rinpdf {
rinpdf_status merge_original_pdf_bytes(const uint8_t* const*,const size_t*,
 size_t,uint8_t**,size_t*);
}
struct rinpdf_page_editor {
 std::vector<rinpdf_page_source> pages;
 std::vector<rinpdf_page_box_override> geometry;
};
extern "C" rinpdf_status rinpdf_page_editor_create(const rinpdf_document* document,
 rinpdf_page_editor** out){
 if(!out)return RINPDF_INVALID_ARGUMENT;
 *out=nullptr;
 if(!document)return RINPDF_INVALID_ARGUMENT;
 size_t count=0;
 const auto status=rinpdf_document_page_count(document,&count);
 if(status!=RINPDF_OK)return status;
 if(count>10000)return RINPDF_UNSUPPORTED;
 try{
  auto* editor=new rinpdf_page_editor;
  try{
   editor->pages.reserve(count);
   editor->geometry.resize(count);
   for(size_t i=0;i<count;++i)editor->pages.push_back({document,i});
  }catch(...){delete editor;throw;}
  *out=editor;return RINPDF_OK;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
extern "C" void rinpdf_page_editor_close(rinpdf_page_editor* editor){delete editor;}
extern "C" rinpdf_status rinpdf_page_editor_page_count(const rinpdf_page_editor* editor,size_t* count){
 if(!editor||!count)return RINPDF_INVALID_ARGUMENT;
 *count=editor->pages.size();return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_page_editor_delete(rinpdf_page_editor* editor,size_t index){
 if(!editor||index>=editor->pages.size())return RINPDF_INVALID_ARGUMENT;
 editor->pages.erase(editor->pages.begin()+index);
 editor->geometry.erase(editor->geometry.begin()+index);
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_page_editor_move(rinpdf_page_editor* editor,size_t from,size_t to){
 if(!editor||from>=editor->pages.size()||to>=editor->pages.size())return RINPDF_INVALID_ARGUMENT;
 if(from<to){
  std::rotate(editor->pages.begin()+from,editor->pages.begin()+from+1,editor->pages.begin()+to+1);
  std::rotate(editor->geometry.begin()+from,editor->geometry.begin()+from+1,editor->geometry.begin()+to+1);
 }else if(from>to){
  std::rotate(editor->pages.begin()+to,editor->pages.begin()+from,editor->pages.begin()+from+1);
  std::rotate(editor->geometry.begin()+to,editor->geometry.begin()+from,editor->geometry.begin()+from+1);
 }
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_page_editor_insert(rinpdf_page_editor* editor,
 size_t at,const rinpdf_document* source,size_t source_page){
 if(!editor||!source||at>editor->pages.size()||editor->pages.size()>=10000)
  return RINPDF_INVALID_ARGUMENT;
 size_t count=0;
 auto status=rinpdf_document_page_count(source,&count);
 if(status!=RINPDF_OK)return status;
 if(source_page>=count)return RINPDF_INVALID_ARGUMENT;
 try{
  editor->pages.insert(editor->pages.begin()+at,{source,source_page});
  try{editor->geometry.insert(editor->geometry.begin()+at,{});}
  catch(...){editor->pages.erase(editor->pages.begin()+at);throw;}
  return RINPDF_OK;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
extern "C" rinpdf_status rinpdf_page_editor_duplicate(rinpdf_page_editor* editor,
 size_t index,size_t at){
 if(!editor||index>=editor->pages.size()||at>editor->pages.size()||
    editor->pages.size()>=10000)return RINPDF_INVALID_ARGUMENT;
 try{
  const auto source=editor->pages[index];
  const auto geometry=editor->geometry[index];
  editor->pages.insert(editor->pages.begin()+at,source);
  try{editor->geometry.insert(editor->geometry.begin()+at,geometry);}
  catch(...){editor->pages.erase(editor->pages.begin()+at);throw;}
  return RINPDF_OK;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
extern "C" rinpdf_status rinpdf_page_editor_set_geometry(
 rinpdf_page_editor* editor,size_t index,const rinpdf_page_box_override* geometry){
 if(!editor||!geometry||index>=editor->pages.size())return RINPDF_INVALID_ARGUMENT;
 if(geometry->set_rotation&&geometry->rotation!=0&&geometry->rotation!=90&&
    geometry->rotation!=180&&geometry->rotation!=270)return RINPDF_INVALID_ARGUMENT;
 const auto valid=[](int enabled,const double* box){
  if(!enabled)return true;
  for(int k=0;k<4;++k)if(!std::isfinite(box[k]))return false;
  return box[2]>box[0]&&box[3]>box[1];
 };
 if(!valid(geometry->set_media_box,geometry->media_box)||
    !valid(geometry->set_crop_box,geometry->crop_box)||
    !valid(geometry->set_trim_box,geometry->trim_box)||
    !valid(geometry->set_bleed_box,geometry->bleed_box)||
    !valid(geometry->set_art_box,geometry->art_box))return RINPDF_INVALID_ARGUMENT;
 editor->geometry[index]=*geometry;return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_page_editor_get_geometry(
 const rinpdf_page_editor* editor,size_t index,rinpdf_page_box_override* geometry){
 if(!editor||!geometry||index>=editor->pages.size())return RINPDF_INVALID_ARGUMENT;
 *geometry=editor->geometry[index];
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_page_editor_clear_geometry(
 rinpdf_page_editor* editor,size_t index){
 if(!editor||index>=editor->pages.size())return RINPDF_INVALID_ARGUMENT;
 editor->geometry[index]={};
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_page_editor_rotate(
 rinpdf_page_editor* editor,size_t index,int quarter_turns){
 if(!editor||index>=editor->pages.size())return RINPDF_INVALID_ARGUMENT;
 const auto& source=editor->pages[index];
 unsigned current=0;
 if(editor->geometry[index].set_rotation)current=editor->geometry[index].rotation;
 else {
  rinpdf_page_geometry actual{};
  auto status=rinpdf_document_page_geometry(source.document,source.page_index,&actual);
  if(status!=RINPDF_OK)return status;
  current=actual.rotation;
 }
 // Normalize negative turns without signed overflow.
 int turns=quarter_turns%4;
 if(turns<0)turns+=4;
 auto& g=editor->geometry[index];
 g.rotation=((current/90+static_cast<unsigned>(turns))%4)*90;
 g.set_rotation=1;
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_page_editor_swap(
 rinpdf_page_editor* editor,size_t a,size_t b){
 if(!editor||a>=editor->pages.size()||b>=editor->pages.size())
  return RINPDF_INVALID_ARGUMENT;
 std::swap(editor->pages[a],editor->pages[b]);
 std::swap(editor->geometry[a],editor->geometry[b]);
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_page_editor_delete_range(
 rinpdf_page_editor* editor,size_t first,size_t count){
 if(!editor||first>editor->pages.size()||count>editor->pages.size()-first)
  return RINPDF_INVALID_ARGUMENT;
 editor->pages.erase(editor->pages.begin()+first,editor->pages.begin()+first+count);
 editor->geometry.erase(editor->geometry.begin()+first,editor->geometry.begin()+first+count);
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_page_editor_reverse_range(
 rinpdf_page_editor* editor,size_t first,size_t count){
 if(!editor||first>editor->pages.size()||count>editor->pages.size()-first)
  return RINPDF_INVALID_ARGUMENT;
 std::reverse(editor->pages.begin()+first,editor->pages.begin()+first+count);
 std::reverse(editor->geometry.begin()+first,editor->geometry.begin()+first+count);
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_page_editor_insert_range(
 rinpdf_page_editor* editor,size_t at,const rinpdf_document* source,
 size_t first,size_t count){
 if(!editor||!source||at>editor->pages.size()||count>10000-editor->pages.size())
  return RINPDF_INVALID_ARGUMENT;
 size_t total=0;
 auto status=rinpdf_document_page_count(source,&total);
 if(status!=RINPDF_OK)return status;
 if(first>total||count>total-first)return RINPDF_INVALID_ARGUMENT;
 if(count==0)return RINPDF_OK;
 try {
  // Build a complete replacement state before mutation, so out-of-memory
  // failures cannot desynchronize page sources and their geometry overrides.
  auto pages=editor->pages;
  auto geometry=editor->geometry;
  std::vector<rinpdf_page_source> inserted;
  inserted.reserve(count);
  for(size_t i=0;i<count;++i)inserted.push_back({source,first+i});
  pages.insert(pages.begin()+at,inserted.begin(),inserted.end());
  geometry.insert(geometry.begin()+at,count,rinpdf_page_box_override{});
  editor->pages.swap(pages);
  editor->geometry.swap(geometry);
  return RINPDF_OK;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
extern "C" rinpdf_status rinpdf_page_editor_export_memory(const rinpdf_page_editor* editor,
 uint8_t** data,size_t* size){
 if(!data||!size)return RINPDF_INVALID_ARGUMENT;
 *data=nullptr;*size=0;
 if(!editor||editor->pages.empty())return RINPDF_INVALID_ARGUMENT;
 try{
  struct FreePdf{void operator()(uint8_t* p)const{rinpdf_free_buffer(p);}};
  // A single source document can be exported directly: no intermediate
  // PDF, no second parse and no second serialization pass.
  const rinpdf_document* first_document=editor->pages.front().document;
  bool single_source=true;
  for(const auto& page:editor->pages)
   if(page.document!=first_document){single_source=false;break;}
  if(single_source){
   std::vector<size_t> indices;
   indices.reserve(editor->pages.size());
   for(const auto& page:editor->pages)indices.push_back(page.page_index);
   return rinpdf_export_pages_with_boxes_memory(first_document,
      indices.data(),editor->geometry.data(),indices.size(),data,size);
  }
  std::vector<std::unique_ptr<uint8_t,FreePdf>> owned;
  std::vector<const uint8_t*> buffers;
  std::vector<size_t> lengths;
  owned.reserve(128);buffers.reserve(128);lengths.reserve(128);
  size_t first=0;
  while(first<editor->pages.size()){
   const rinpdf_document* doc=editor->pages[first].document;
   size_t end=first+1;
   while(end<editor->pages.size()&&editor->pages[end].document==doc)++end;
   if(owned.size()>=128)return RINPDF_UNSUPPORTED;
   std::vector<size_t> indices;
   std::vector<rinpdf_page_box_override> overrides;
   indices.reserve(end-first);overrides.reserve(end-first);
   for(size_t i=first;i<end;++i){
    indices.push_back(editor->pages[i].page_index);
    overrides.push_back(editor->geometry[i]);
   }
   uint8_t* bytes=nullptr;size_t length=0;
   auto status=rinpdf_export_pages_with_boxes_memory(doc,indices.data(),
      overrides.data(),indices.size(),&bytes,&length);
   if(status!=RINPDF_OK)return status;
   owned.emplace_back(bytes);
   buffers.push_back(bytes);lengths.push_back(length);
   first=end;
  }
  return rinpdf::merge_original_pdf_bytes(buffers.data(),lengths.data(),
                                          buffers.size(),data,size);
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
extern "C" rinpdf_status rinpdf_page_editor_export_file(const rinpdf_page_editor* editor,
 const char* path){
 if(!editor||!path||editor->pages.empty())return RINPDF_INVALID_ARGUMENT;
 uint8_t* data=nullptr;size_t size=0;
 auto status=rinpdf_page_editor_export_memory(editor,&data,&size);
 if(status!=RINPDF_OK)return status;
 FILE* file=std::fopen(path,"wb");
 if(!file){rinpdf_free_buffer(data);return RINPDF_IO_ERROR;}
 bool ok=std::fwrite(data,1,size,file)==size;
 if(std::fclose(file)!=0)ok=false;
 rinpdf_free_buffer(data);
 return ok?RINPDF_OK:RINPDF_IO_ERROR;
}
