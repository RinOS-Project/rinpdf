#include "rinpdf/rinpdf.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

struct rinpdf_page_editor {
 std::vector<rinpdf_page_source> pages;
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
 return RINPDF_OK;
}
extern "C" rinpdf_status rinpdf_page_editor_move(rinpdf_page_editor* editor,size_t from,size_t to){
 if(!editor||from>=editor->pages.size()||to>=editor->pages.size())return RINPDF_INVALID_ARGUMENT;
 auto page=editor->pages[from];
 editor->pages.erase(editor->pages.begin()+from);
 editor->pages.insert(editor->pages.begin()+to,page);
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
  return RINPDF_OK;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
extern "C" rinpdf_status rinpdf_page_editor_duplicate(rinpdf_page_editor* editor,
 size_t index,size_t at){
 if(!editor||index>=editor->pages.size()||at>editor->pages.size()||
    editor->pages.size()>=10000)return RINPDF_INVALID_ARGUMENT;
 try{
  const auto source=editor->pages[index];
  editor->pages.insert(editor->pages.begin()+at,source);
  return RINPDF_OK;
 }catch(const std::bad_alloc&){return RINPDF_OUT_OF_MEMORY;}
}
extern "C" rinpdf_status rinpdf_page_editor_export_memory(const rinpdf_page_editor* editor,
 uint8_t** data,size_t* size){
 if(!data||!size)return RINPDF_INVALID_ARGUMENT;
 *data=nullptr;*size=0;
 if(!editor||editor->pages.empty())return RINPDF_INVALID_ARGUMENT;
 return rinpdf_merge_selected_pages_lossless_memory(
   editor->pages.data(),editor->pages.size(),data,size);
}
extern "C" rinpdf_status rinpdf_page_editor_export_file(const rinpdf_page_editor* editor,
 const char* path){
 if(!editor||!path||editor->pages.empty())return RINPDF_INVALID_ARGUMENT;
 return rinpdf_merge_selected_pages_lossless_file(path,editor->pages.data(),editor->pages.size());
}
