#pragma once
#include "error.h"
#include "document.h"
#include "page.h"
#include "render.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Creates a minimal one-page PDF with ASCII text. */
rinpdf_status rinpdf_write_text_file(const char *path,const char *ascii_text);

/* Options for paginated ASCII text PDF creation. Zero values use defaults.
 * The pointers may be NULL. Text supports CR/LF, tabs and form-feed page breaks.
 */
typedef struct rinpdf_text_options {
 unsigned page_width;
 unsigned page_height;
 unsigned margin;
 unsigned font_size;
 const char *title;
 const char *author;
 const char *subject;
} rinpdf_text_options;
rinpdf_status rinpdf_write_text_document(const char *path,const char *ascii_text,
 const rinpdf_text_options *options);
/* Creates a malloc-backed PDF byte buffer; release it with rinpdf_free_buffer. */
rinpdf_status rinpdf_write_text_memory(const char *ascii_text,
 const rinpdf_text_options *options,uint8_t **data,size_t *size);
void rinpdf_free_buffer(void *data);
/* Creates a single-page PDF containing an 8-bit grayscale (components=1),
 * RGB (components=3) or straight-alpha RGBA (components=4) image.
 * RGBA transparency is retained as a PDF soft mask.
 * Input rows may have padding via stride.
 * The memory variant returns a buffer released using rinpdf_free_buffer.
 */
rinpdf_status rinpdf_write_image_memory(const uint8_t *pixels,
 unsigned width,unsigned height,size_t stride,unsigned components,
 uint8_t **data,size_t *size);
rinpdf_status rinpdf_write_image_file(const char *path,const uint8_t *pixels,
 unsigned width,unsigned height,size_t stride,unsigned components);
/* Each image becomes one page; buffers must remain valid during the call.
 * Supported components: 1 (gray), 3 (RGB), 4 (RGBA with soft mask).
 */
typedef struct rinpdf_image_page {
 const uint8_t *pixels;
 unsigned width,height;
 size_t stride;
 unsigned components;
} rinpdf_image_page;
rinpdf_status rinpdf_write_image_pages_memory(const rinpdf_image_page *pages,
 size_t count,uint8_t **data,size_t *size);
rinpdf_status rinpdf_write_image_pages_file(const char *path,
 const rinpdf_image_page *pages,size_t count);
/* Rasterize selected pages at the requested DPI and export to a new PDF.
 * Indices are zero-based and their order is preserved; duplicate indices
 * are allowed. Text and vector objects become pixels (not lossless editing).
 * Output memory must be released with rinpdf_free_buffer.
 */
rinpdf_status rinpdf_export_pages_memory(const rinpdf_document *doc,
 const size_t *indices,size_t count,unsigned dpi,uint8_t **data,size_t *size);
rinpdf_status rinpdf_export_pages_file(const char *path,
 const rinpdf_document *doc,const size_t *indices,size_t count,unsigned dpi);
/* Non-raster page selection/reordering for unencrypted PDFs with ordinary
 * cross-reference entries. Preserves page content streams and resources;
 * unsupported xref updates and compressed object tables return UNSUPPORTED.
 * Output from the memory API must be released with rinpdf_free_buffer.
 */
rinpdf_status rinpdf_export_pages_lossless_memory(const rinpdf_document *doc,
 const size_t *indices,size_t count,uint8_t **data,size_t *size);
rinpdf_status rinpdf_export_pages_lossless_file(const char *path,
 const rinpdf_document *doc,const size_t *indices,size_t count);
/* Merge pages from multiple opened PDF documents in the supplied order.
 * Pages are rasterized at DPI 36..600; original text/vectors are not retained.
 * Repeated pages and sources from the same document are supported.
 */
typedef struct rinpdf_page_source {
 const rinpdf_document *document;
 size_t page_index;
} rinpdf_page_source;
rinpdf_status rinpdf_merge_pages_memory(const rinpdf_page_source *pages,
 size_t count,unsigned dpi,uint8_t **data,size_t *size);
rinpdf_status rinpdf_merge_pages_file(const char *path,
 const rinpdf_page_source *pages,size_t count,unsigned dpi);
/* Merge complete non-encrypted classic-xref PDF documents without rasterizing.
 * Reassigns indirect object IDs and preserves encoded stream bytes.
 * Unsupported document structures return RINPDF_UNSUPPORTED.
 * The input documents must stay open for the duration of the call.
 */
rinpdf_status rinpdf_merge_documents_lossless_memory(
 const rinpdf_document *const *documents,size_t count,uint8_t **data,size_t *size);
rinpdf_status rinpdf_merge_documents_lossless_file(
 const char *path,const rinpdf_document *const *documents,size_t count);
#ifdef __cplusplus
}
#endif
