#include "rinpdf/error.h"
extern "C" const char *rinpdf_status_string(rinpdf_status s){
 switch(s){case RINPDF_OK:return "ok";case RINPDF_INVALID_ARGUMENT:return "invalid argument";
 case RINPDF_IO_ERROR:return "I/O error";case RINPDF_INVALID_PDF:return "invalid PDF header";
 case RINPDF_UNSUPPORTED:return "unsupported";case RINPDF_OUT_OF_MEMORY:return "out of memory";}
 return "unknown status";
}
