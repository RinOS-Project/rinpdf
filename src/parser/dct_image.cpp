#include "dct_image.h"
#include <csetjmp>
#include <cstring>
#include <utility>
#if defined(RINPDF_HAVE_JPEG)
#include <jpeglib.h>
#endif
namespace rinpdf::parser {
#if defined(RINPDF_HAVE_JPEG)
namespace {
struct JpegError {
 jpeg_error_mgr base;
 jmp_buf jump;
};
void on_jpeg_error(j_common_ptr context){
 auto* err=reinterpret_cast<JpegError*>(context->err);
 longjmp(err->jump,1);
}
}
#endif
bool decode_dct_image(const uint8_t* bytes,size_t length,
 unsigned width,unsigned height,unsigned channels,std::vector<uint8_t>& pixels){
 pixels.clear();
 if(!bytes||!length||!width||!height||
    (channels!=1&&channels!=3&&channels!=4)||width>4096||height>4096||
    length>32*1024*1024)return false;
 const size_t expected=size_t(width)*height*channels;
 if(expected>32*1024*1024)return false;
#if defined(RINPDF_HAVE_JPEG)
 // Allocate before setjmp so a libjpeg error never jumps over C++ objects.
 std::vector<uint8_t> result(expected);
 jpeg_decompress_struct decoder{};
 JpegError error{};
 decoder.err=jpeg_std_error(&error.base);
 error.base.error_exit=on_jpeg_error;
 volatile int created=0;
 if(setjmp(error.jump)){
  if(created)jpeg_destroy_decompress(&decoder);
  return false;
 }
 jpeg_create_decompress(&decoder);
 created=1;
 jpeg_mem_src(&decoder,const_cast<unsigned char*>(bytes),
              static_cast<unsigned long>(length));
 if(jpeg_read_header(&decoder,TRUE)!=JPEG_HEADER_OK||
    decoder.image_width!=width||decoder.image_height!=height||
    decoder.num_components!=static_cast<int>(channels)){
  jpeg_destroy_decompress(&decoder);return false;
 }
 // Preserve four-component CMYK samples; reject unsupported JPEG color models.
 if(channels==4&&decoder.jpeg_color_space!=JCS_CMYK&&
    decoder.jpeg_color_space!=JCS_YCCK){
  jpeg_destroy_decompress(&decoder);return false;
 }
 // Do not silently interpret a non-grayscale JPEG as DeviceGray.
 if(channels==1&&decoder.jpeg_color_space!=JCS_GRAYSCALE){
  jpeg_destroy_decompress(&decoder);return false;
 }
 // RGB JPEGs may be encoded as YCbCr; libjpeg performs that conversion.
 // An unknown or CMYK JPEG must not be accepted as a three-channel image.
 if(channels==3&&decoder.jpeg_color_space!=JCS_RGB&&
    decoder.jpeg_color_space!=JCS_YCbCr){
  jpeg_destroy_decompress(&decoder);return false;
 }
 decoder.out_color_space=channels==1?JCS_GRAYSCALE:
                         channels==3?JCS_RGB:JCS_CMYK;
 if(!jpeg_start_decompress(&decoder)||
    decoder.output_width!=width||decoder.output_height!=height||
    decoder.output_components!=static_cast<int>(channels)){
  jpeg_destroy_decompress(&decoder);return false;
 }
 const size_t pitch=size_t(width)*channels;
 while(decoder.output_scanline<decoder.output_height){
  JSAMPROW row=result.data()+size_t(decoder.output_scanline)*pitch;
  if(jpeg_read_scanlines(&decoder,&row,1)!=1){
   jpeg_destroy_decompress(&decoder);return false;
  }
 }
 if(decoder.output_scanline!=decoder.output_height){
  jpeg_destroy_decompress(&decoder);return false;
 }
 if(!jpeg_finish_decompress(&decoder)){
  jpeg_destroy_decompress(&decoder);return false;
 }
 jpeg_destroy_decompress(&decoder);
 pixels=std::move(result);
 return true;
#else
 (void)expected;
 return false;
#endif
}
}
