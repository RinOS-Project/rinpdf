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
#ifdef __cplusplus
}
#endif
