#include "../src/parser/object_stream.h"
#include "../src/parser/xref_stream.h"
#include <string>
#include <vector>
using namespace rinpdf::parser;
int main(){
 // Two objects: a dictionary and a numeric array.
 const std::string body="<< /Type /Catalog >> [1 2 3]";
 const std::string header="3 0 4 20 ";
 const std::string payload=header+body;
 std::vector<ObjectStreamItem> objects;
 if(!decode_object_stream(reinterpret_cast<const uint8_t*>(payload.data()),
       payload.size(),2,header.size(),objects))return 1;
 if(objects.size()!=2||objects[0].object!=3||
    objects[0].value.kind!=Value::Kind::Dictionary||
    objects[1].object!=4||objects[1].value.kind!=Value::Kind::Array)return 2;
 const size_t retained=objects.size();
 if(decode_object_stream(reinterpret_cast<const uint8_t*>(payload.data()),
       payload.size(),2,header.size()-1,objects))return 3;
 if(decode_object_stream(reinterpret_cast<const uint8_t*>(payload.data()),
       payload.size(),2,payload.size(),objects))return 4;
 const std::string duplicate="3 0 3 20 "+body;
 if(decode_object_stream(reinterpret_cast<const uint8_t*>(duplicate.data()),
       duplicate.size(),2,header.size(),objects))return 5;
 const std::string overlap="3 0 4 0 "+body;
 if(decode_object_stream(reinterpret_cast<const uint8_t*>(overlap.data()),
       overlap.size(),2,header.size(),objects))return 6;
 if(objects.size()!=retained)return 7;
 // XRef type 2 resolves a compressed object by its indexed position.
 std::string pdf="%PDF-1.5\n";
 const size_t offset=pdf.size();
 const std::string embedded="7 0 << /Type /Catalog >>";
 pdf+="5 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Length "+
      std::to_string(embedded.size())+" >>\nstream\n";
 pdf+=embedded;
 pdf+="\nendstream\nendobj\n";
 std::vector<XrefStreamEntry> refs{{5,1,offset,0},{7,2,5,0}};
 Value resolved;
 if(!resolve_compressed_object(reinterpret_cast<const uint8_t*>(pdf.data()),
      pdf.size(),refs,7,resolved))return 8;
 if(resolved.kind!=Value::Kind::Dictionary||
    resolved.dictionary.at("Type").text!="Catalog")return 9;
 refs[1].field2=1;
 if(resolve_compressed_object(reinterpret_cast<const uint8_t*>(pdf.data()),
      pdf.size(),refs,7,resolved))return 10;
 refs[1].field2=0;
 refs[0].type=2;
 if(resolve_compressed_object(reinterpret_cast<const uint8_t*>(pdf.data()),
      pdf.size(),refs,7,resolved))return 11;
 return 0;
}
