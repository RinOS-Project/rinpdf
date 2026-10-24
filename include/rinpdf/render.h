#pragma once
#include "page.h"
#ifdef __cplusplus
extern "C" {
#endif
/* RGBA output, stride measured in bytes. Supports bounded vector paths, supported embedded TrueType glyphs, opaque 8-bit DeviceGray/DeviceRGB image XObjects, and bounded Form XObjects inheriting page resources (nested Forms up to depth 8, with nested Form XObjects and bounded Form-local Gray/RGB images, with restricted Form-local embedded TrueType fonts, but without transparency groups or full font resource support). Unsupported operators or formats return UNSUPPORTED. */
rinpdf_status rinpdf_render_page(const rinpdf_document *doc,size_t index,
 unsigned width,unsigned height,unsigned char *rgba,size_t stride);
/* Calculate pixel dimensions for a target DPI. Uses effective CropBox,
 * /Rotate and /UserUnit. Dimensions are rounded up and limited to
 * the renderer's 16384-side / 16777216-pixel bounds.
 */
rinpdf_status rinpdf_render_page_size_for_dpi(const rinpdf_document *doc,
 size_t page_index,double dpi,unsigned *width,unsigned *height);
/* Render to a caller-owned RGBA buffer using DPI-derived dimensions.
 * The buffer must have space for height rows of stride bytes.
 * The resolved dimensions are returned on success.
 */
rinpdf_status rinpdf_render_page_at_dpi(const rinpdf_document *doc,
 size_t page_index,double dpi,unsigned char *rgba,size_t stride,
 unsigned *width,unsigned *height);
/* Required bytes for an RGBA image with caller-selected stride.
 * For a tightly packed image pass stride=0. No image allocation occurs.
 */
rinpdf_status rinpdf_render_page_buffer_size(unsigned width,unsigned height,
 size_t stride,size_t *required_bytes);
/* DPI render with explicit buffer capacity. Reject undersized buffers before
 * any pixel writes. Output dimensions are set after a successful render.
 */
rinpdf_status rinpdf_render_page_at_dpi_sized(const rinpdf_document *doc,
 size_t page_index,double dpi,unsigned char *rgba,size_t buffer_bytes,
 size_t stride,unsigned *width,unsigned *height);
/* Capacity-checked rendering at explicit pixel dimensions.
 * stride=0 means tightly packed RGBA rows. buffer_bytes must cover the
 * final row's pixels, including any inter-row padding.
 */
rinpdf_status rinpdf_render_page_sized(const rinpdf_document *doc,
 size_t page_index,unsigned width,unsigned height,unsigned char *rgba,
 size_t buffer_bytes,size_t stride);
/* Render a viewport in the coordinate system of a full-resolution page.
 * x/y are top-left pixel offsets; tile dimensions determine output storage.
 * Supports page rotation 0, 90, 180 and 270 degrees.
 */
rinpdf_status rinpdf_render_page_tile_sized(const rinpdf_document *doc,
 size_t page_index,unsigned full_width,unsigned full_height,
 unsigned tile_x,unsigned tile_y,unsigned tile_width,unsigned tile_height,
 unsigned char *rgba,size_t buffer_bytes,size_t stride);
#ifdef __cplusplus
}
#endif
