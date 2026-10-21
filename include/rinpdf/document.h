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
/* Approximate resident decoded-render cache budget. Does not include source
 * mappings, parser indexes, container overhead or in-flight render references.
 */
rinpdf_status rinpdf_document_set_render_cache_budget(rinpdf_document *doc,size_t bytes);
rinpdf_status rinpdf_document_render_cache_usage(const rinpdf_document *doc,
 size_t *estimated_bytes,size_t *budget_bytes);
rinpdf_status rinpdf_document_trim_render_caches(rinpdf_document *doc,size_t target_bytes);
/* Counters cover decoded resource-cache lookups and budget evictions.
 * Counters are cumulative until explicitly reset, not per render.
 */
typedef struct rinpdf_cache_metrics {
 uint64_t hits;
 uint64_t misses;
 uint64_t evictions;
} rinpdf_cache_metrics;
rinpdf_status rinpdf_document_cache_metrics(const rinpdf_document *doc,
 rinpdf_cache_metrics *metrics);
rinpdf_status rinpdf_document_reset_cache_metrics(rinpdf_document *doc);
/* Cumulative monotonic timing for completed render phases, in nanoseconds.
 * Preparation includes waiting for the per-page lock and resource resolution.
 * Raster includes software rendering and output rotation/copy.
 * Calls are counted on successful completion of each phase.
 */
typedef struct rinpdf_render_timings {
 uint64_t preparation_ns;
 uint64_t raster_ns;
 uint64_t prepared_calls;
 uint64_t rendered_calls;
} rinpdf_render_timings;
rinpdf_status rinpdf_document_render_timings(const rinpdf_document *doc,
 rinpdf_render_timings *timings);
rinpdf_status rinpdf_document_reset_render_timings(rinpdf_document *doc);
#ifdef __cplusplus
}
#endif
