#pragma once
#include "page.h"
#ifdef __cplusplus
extern "C" {
#endif
/* RGBA output, stride measured in bytes. Supports bounded vector paths, supported embedded TrueType glyphs, opaque 8-bit DeviceGray/DeviceRGB image XObjects, and bounded Form XObjects inheriting page resources (nested Forms up to depth 8, with independently referenced nested Form XObjects and aliases to page-registered images, but without Form-only images or transparency groups). Unsupported operators or formats return UNSUPPORTED. */
rinpdf_status rinpdf_render_page(const rinpdf_document *doc,size_t index,
 unsigned width,unsigned height,unsigned char *rgba,size_t stride);
#ifdef __cplusplus
}
#endif
