#include "flate.h"
#include <zlib.h>
#include <algorithm>
#include <limits>
namespace rinpdf::parser {
bool flate_decode(const uint8_t *data,size_t size,std::vector<uint8_t>& output,size_t max_output){
 output.clear();
 if((!data&&size)||max_output==0)return false;
 z_stream z{};
 if(inflateInit(&z)!=Z_OK)return false;
 size_t consumed=0;
 bool success=false;
 unsigned char buffer[16384];
 for(;;){
  if(z.avail_in==0&&consumed<size){
   size_t chunk=std::min(size-consumed,sizeof(buffer));
   z.next_in=const_cast<Bytef*>(data+consumed);
   z.avail_in=static_cast<uInt>(chunk);
   consumed+=chunk;
  }
  z.next_out=buffer;
  z.avail_out=static_cast<uInt>(sizeof(buffer));
  int result=inflate(&z,Z_NO_FLUSH);
  size_t produced=sizeof(buffer)-z.avail_out;
  if(output.size()>max_output||produced>max_output-output.size())break;
  output.insert(output.end(),buffer,buffer+produced);
  if(result==Z_STREAM_END){
   success=consumed==size&&z.avail_in==0;
   break;
  }
  if(result!=Z_OK)break;
  if(produced==0&&z.avail_in==0&&consumed==size)break;
 }
 inflateEnd(&z);
 if(!success)output.clear();
 return success;
}
}
