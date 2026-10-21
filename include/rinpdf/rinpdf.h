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
/* Optional page dictionary overrides for non-raster extraction.
 * Coordinates are PDF user-space points. Flags set to zero leave inherited
 * values unchanged. Rotation must be 0, 90, 180 or 270 degrees.
 */
typedef struct rinpdf_page_box_override {
 int set_rotation;
 unsigned rotation;
 int set_media_box;
 double media_box[4];
 int set_crop_box;
 double crop_box[4];
 int set_trim_box;
 double trim_box[4];
 int set_bleed_box;
 double bleed_box[4];
 int set_art_box;
 double art_box[4];
} rinpdf_page_box_override;
rinpdf_status rinpdf_export_pages_with_boxes_memory(const rinpdf_document *doc,
 const size_t *indices,const rinpdf_page_box_override *overrides,
 size_t count,uint8_t **data,size_t *size);
rinpdf_status rinpdf_export_pages_with_boxes_file(const char *path,
 const rinpdf_document *doc,const size_t *indices,
 const rinpdf_page_box_override *overrides,size_t count);
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
/* Merge selected zero-based pages across documents without rasterizing.
 * Selection order is preserved and duplicate pages are supported.
 * Adjacent selections from the same document are grouped; up to 128
 * source runs and 10000 selected pages are accepted.
 * This path currently depends on the restricted lossless page extractor.
 */
rinpdf_status rinpdf_merge_selected_pages_lossless_memory(
 const rinpdf_page_source *pages,size_t count,uint8_t **data,size_t *size);
rinpdf_status rinpdf_merge_selected_pages_lossless_file(
 const char *path,const rinpdf_page_source *pages,size_t count);
/* Merge complete non-encrypted classic-xref PDF documents without rasterizing.
 * Reassigns indirect object IDs and preserves encoded stream bytes.
 * Unsupported document structures return RINPDF_UNSUPPORTED.
 * The input documents must stay open for the duration of the call.
 */
rinpdf_status rinpdf_merge_documents_lossless_memory(
 const rinpdf_document *const *documents,size_t count,uint8_t **data,size_t *size);
rinpdf_status rinpdf_merge_documents_lossless_file(
 const char *path,const rinpdf_document *const *documents,size_t count);
/* Open, merge and close source documents from the specified file paths. */
rinpdf_status rinpdf_merge_files_lossless(const char *path,
 const char *const *source_paths,size_t count);
/* Page editing operations use zero-based indices. The editor stores an
 * ordered page selection; deletion, insertion and movement mutate only this
 * selection until export. Source PDF documents must remain open.
 */
typedef struct rinpdf_page_editor rinpdf_page_editor;
rinpdf_status rinpdf_page_editor_create(const rinpdf_document *document,
 rinpdf_page_editor **out);
void rinpdf_page_editor_close(rinpdf_page_editor *editor);
rinpdf_status rinpdf_page_editor_set_geometry(rinpdf_page_editor *editor,
 size_t index,const rinpdf_page_box_override *geometry);
rinpdf_status rinpdf_page_editor_get_geometry(const rinpdf_page_editor *editor,
 size_t index,rinpdf_page_box_override *geometry);
rinpdf_status rinpdf_page_editor_clear_geometry(rinpdf_page_editor *editor,
 size_t index);
/* Rotate clockwise in increments of 90 degrees; negative values rotate CCW. */
rinpdf_status rinpdf_page_editor_rotate(rinpdf_page_editor *editor,
 size_t index,int quarter_turns);
rinpdf_status rinpdf_page_editor_swap(rinpdf_page_editor *editor,
 size_t first,size_t second);
rinpdf_status rinpdf_page_editor_page_count(const rinpdf_page_editor *editor,
 size_t *count);
rinpdf_status rinpdf_page_editor_delete(rinpdf_page_editor *editor,size_t index);
rinpdf_status rinpdf_page_editor_move(rinpdf_page_editor *editor,
 size_t from,size_t to);
rinpdf_status rinpdf_page_editor_insert(rinpdf_page_editor *editor,
 size_t at,const rinpdf_document *source,size_t source_page);
rinpdf_status rinpdf_page_editor_duplicate(rinpdf_page_editor *editor,
 size_t index,size_t at);
rinpdf_status rinpdf_page_editor_export_memory(const rinpdf_page_editor *editor,
 uint8_t **data,size_t *size);
rinpdf_status rinpdf_page_editor_export_file(const rinpdf_page_editor *editor,
 const char *path);
/* Write an incremental Info dictionary revision without changing page streams.
 * Inputs are UTF-8, stored as UTF-16BE PDF text strings.
 * Existing Info fields not supplied are not retained.
 */
typedef struct rinpdf_metadata {
 const char *title;
 const char *author;
 const char *subject;
 const char *keywords;
} rinpdf_metadata;
rinpdf_status rinpdf_metadata_update_memory(const uint8_t *pdf,size_t pdf_size,
 const rinpdf_metadata *metadata,uint8_t **output,size_t *output_size);
rinpdf_status rinpdf_metadata_update_file(const char *output_path,
 const uint8_t *pdf,size_t pdf_size,const rinpdf_metadata *metadata);
/* Read a standard Info field (Title, Author, Subject, Keywords, Creator,
 * Producer) as UTF-8. The returned buffer is freed with rinpdf_free_buffer.
 * PDFDocEncoding beyond ASCII is not yet supported. */
rinpdf_status rinpdf_metadata_get_utf8(const uint8_t *pdf,size_t pdf_size,
 const char *key,char **output);
#ifdef __cplusplus
}
#endif
