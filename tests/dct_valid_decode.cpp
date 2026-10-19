#include "../src/parser/dct_image.h"
#include <jpeglib.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

static bool verify(unsigned channels){
 constexpr unsigned width=8,height=8;
 const size_t pitch=width*channels;
 std::vector<uint8_t> source(pitch*height);
 // Uniform channels avoid chroma subsampling artifacts while still
 // checking distinct RGB component values and image dimensions.
 for(unsigned y=0;y<height;++y)
  for(unsigned x=0;x<width;++x)
   for(unsigned c=0;c<channels;++c)
    source[y*pitch+x*channels+c]=
      channels==1?uint8_t(144):uint8_t(c==0?32:c==1?128:224);
 jpeg_compress_struct encoder{};
 jpeg_error_mgr error{};
 encoder.err=jpeg_std_error(&error);
 jpeg_create_compress(&encoder);
 unsigned char* compressed=nullptr;
 unsigned long compressed_size=0;
 jpeg_mem_dest(&encoder,&compressed,&compressed_size);
 encoder.image_width=width;
 encoder.image_height=height;
 encoder.input_components=int(channels);
 encoder.in_color_space=channels==1?JCS_GRAYSCALE:JCS_RGB;
 jpeg_set_defaults(&encoder);
 jpeg_set_quality(&encoder,95,TRUE);
 jpeg_start_compress(&encoder,TRUE);
 while(encoder.next_scanline<height){
  JSAMPROW row=source.data()+size_t(encoder.next_scanline)*pitch;
  jpeg_write_scanlines(&encoder,&row,1);
 }
 jpeg_finish_compress(&encoder);
 jpeg_destroy_compress(&encoder);
 std::vector<uint8_t> bytes(compressed,compressed+compressed_size);
 std::free(compressed);
 std::vector<uint8_t> decoded;
 if(!rinpdf::parser::decode_dct_image(bytes.data(),bytes.size(),
                                      width,height,channels,decoded)||
    decoded.size()!=source.size())return false;
 for(size_t i=0;i<source.size();++i){
  const int difference=int(decoded[i])-int(source[i]);
  if(difference< -3||difference>3)return false;
 }
 // Mismatched image geometry must not return pixels from a valid JPEG.
 decoded.assign(4,123);
 if(rinpdf::parser::decode_dct_image(bytes.data(),bytes.size(),
                                    width+1,height,channels,decoded)||
    !decoded.empty())return false;
 decoded.assign(4,123);
 if(rinpdf::parser::decode_dct_image(bytes.data(),bytes.size(),
                                    width,height,channels==1?3:1,decoded)||
    !decoded.empty())return false;
 return true;
}
int main(){
 if(!verify(1)||!verify(3)){
  std::fprintf(stderr,"JPEG Gray/RGB roundtrip regression\n");
  return 1;
 }
 return 0;
}
