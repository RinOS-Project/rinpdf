#pragma once
#include "document.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Zero-based page indices. */
rinpdf_status rinpdf_document_page_count(const rinpdf_document *doc,size_t *count);
typedef struct rinpdf_page_geometry {
 double x0,y0,x1,y1;
 unsigned rotation;
} rinpdf_page_geometry;
rinpdf_status rinpdf_document_page_geometry(const rinpdf_document *doc,
 size_t page_index,rinpdf_page_geometry *geometry);
/* Physical page dimensions in PDF points (1/72 inch), taking /UserUnit
 * and /Rotate into account. Geometry coordinates remain unscaled.
 */
rinpdf_status rinpdf_document_page_physical_size(const rinpdf_document *doc,
 size_t page_index,double *width_points,double *height_points,double *user_unit);
#ifdef __cplusplus
}
#endif
