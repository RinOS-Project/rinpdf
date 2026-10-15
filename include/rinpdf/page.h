#pragma once
#include "document.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Page enumeration is not implemented in the initial parser. */
rinpdf_status rinpdf_document_page_count(const rinpdf_document *doc,size_t *count);
#ifdef __cplusplus
}
#endif
