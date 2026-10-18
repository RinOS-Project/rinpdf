#pragma once
#include "page.h"
#ifdef __cplusplus
extern "C" {
#endif
/* RGBA output, stride measured in bytes. Supports bounded vector paths, supported embedded TrueType glyphs, and opaque 8-bit DeviceGray/DeviceRGB image XObjects. Unsupported operators or image formats return UNSUPPORTED. */
rinpdf_status rinpdf_render_page(const rinpdf_document *doc,size_t index,
 unsigned width,unsigned height,unsigned char *rgba,size_t stride);
#ifdef __cplusplus
}
#endif
