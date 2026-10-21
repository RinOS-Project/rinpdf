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
#ifdef __cplusplus
}
#endif
