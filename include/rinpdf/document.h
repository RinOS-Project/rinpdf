#pragma once
#include <stddef.h>
#include <stdint.h>
#include "error.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct rinpdf_document rinpdf_document;
rinpdf_status rinpdf_document_open_memory(const uint8_t *data,size_t size,rinpdf_document **out);
rinpdf_status rinpdf_document_open_file(const char *path,rinpdf_document **out);
void rinpdf_document_close(rinpdf_document *doc);
rinpdf_status rinpdf_document_version(const rinpdf_document *doc,unsigned *major,unsigned *minor);
/* Parse and cache the document cross-reference index; report live object counts. */
rinpdf_status rinpdf_document_xref_statistics(const rinpdf_document *doc,
 size_t *ordinary,size_t *compressed);
/* Decoded-content cache controls. Statistics count resident content bytes only;
 * mapped source file and other resource caches are not included.
 * Concurrent renders may temporarily hold evicted entries alive.
 */
rinpdf_status rinpdf_document_content_cache_stats(const rinpdf_document *doc,
 size_t *resident_bytes,size_t *entry_count,size_t *budget_bytes);
rinpdf_status rinpdf_document_set_content_cache_budget(rinpdf_document *doc,
 size_t budget_bytes);
rinpdf_status rinpdf_document_clear_content_cache(rinpdf_document *doc);
rinpdf_status rinpdf_document_clear_render_caches(rinpdf_document *doc);
typedef struct rinpdf_render_cache_counts {
 size_t content_pages;
 size_t font_pages;
 size_t image_pages;
 size_t form_pages;
 size_t graphics_pages;
 size_t property_pages;
 size_t geometry_pages;
} rinpdf_render_cache_counts;
rinpdf_status rinpdf_document_render_cache_counts(const rinpdf_document *doc,
 rinpdf_render_cache_counts *counts);
#ifdef __cplusplus
}
#endif
