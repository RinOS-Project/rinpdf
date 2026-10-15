#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef enum rinpdf_status {
 RINPDF_OK=0, RINPDF_INVALID_ARGUMENT=1, RINPDF_IO_ERROR=2,
 RINPDF_INVALID_PDF=3, RINPDF_UNSUPPORTED=4, RINPDF_OUT_OF_MEMORY=5
} rinpdf_status;
const char *rinpdf_status_string(rinpdf_status status);
#ifdef __cplusplus
}
#endif
