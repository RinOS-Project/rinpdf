#include "rinpdf/render.h"
extern "C" rinpdf_status rinpdf_render_page(const rinpdf_document *d,size_t,unsigned,unsigned,unsigned char *,size_t){
 return d?RINPDF_UNSUPPORTED:RINPDF_INVALID_ARGUMENT;
}
