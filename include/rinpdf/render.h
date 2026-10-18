#pragma once
#include "page.h"
#ifdef __cplusplus
extern "C" {
#endif
/* RGBA output, stride measured in bytes. Supports bounded vector paths, supported embedded TrueType glyphs, opaque 8-bit DeviceGray/DeviceRGB image XObjects, and bounded Form XObjects inheriting page resources (nested Forms up to depth 8, with nested Form XObjects and bounded Form-local Gray/RGB images, with restricted Form-local embedded TrueType fonts, but without transparency groups or full font resource support). Unsupported operators or formats return UNSUPPORTED. */
rinpdf_status rinpdf_render_page(const rinpdf_document *doc,size_t index,
 unsigned width,unsigned height,unsigned char *rgba,size_t stride);
#ifdef __cplusplus
}
#endif
