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
#ifdef __cplusplus
}
#endif
