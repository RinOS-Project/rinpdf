#pragma once
#include "page.h"
#ifdef __cplusplus
extern "C" {
#endif
/* RGBA output, stride measured in bytes. Supports a limited subset of grayscale rectangle content. Unsupported operators return UNSUPPORTED. */
rinpdf_status rinpdf_render_page(const rinpdf_document *doc,size_t index,
 unsigned width,unsigned height,unsigned char *rgba,size_t stride);
#ifdef __cplusplus
}
#endif
