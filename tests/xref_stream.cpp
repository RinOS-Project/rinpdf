#include "../src/parser/xref_stream.h"
#include <vector>
#include <cstdint>
#include <string>
#include "rinpdf/rinpdf.h"
using namespace rinpdf::parser;
int main(){
 std::vector<XrefStreamEntry> out{{99,1,4,0}};
 // W=[1,2,1], /Index=[0 3]: free, ordinary, object stream.
 const uint8_t sample[]={0,0,0,255, 1,0,42,0, 2,0,7,3};
 if(!decode_xref_stream_entries(sample,sizeof(sample),{1,2,1},{0,3},out))return 1;
 if(out.size()!=3||out[0].type!=0||out[1].field1!=42||
    out[2].type!=2||out[2].field1!=7||out[2].field2!=3)return 2;
 const uint8_t implicit[]={0,3,0,0};
 if(!decode_xref_stream_entries(implicit,sizeof(implicit),{0,3,1},{5,1},out))return 3;
 if(out.size()!=1||out[0].object!=5||out[0].type!=1||out[0].field1!=768)return 4;
 if(decode_xref_stream_entries(sample,sizeof(sample),{1,2,1},{0,4},out))return 5;
 if(decode_xref_stream_entries(sample,sizeof(sample),{1,9,1},{0,3},out))return 6;
 if(decode_xref_stream_entries(sample,sizeof(sample),{1,2,1},{2,2,3,1},out))return 7;
 if(decode_xref_stream_entries(sample,sizeof(sample),{1,2,1},{0,1000001},out))return 8;
 if(out.size()!=1||out[0].object!=5)return 9; // failed calls preserve output
 const uint8_t unsupported[]={3,0,0,0};
 if(decode_xref_stream_entries(unsupported,sizeof(unsupported),{1,2,1},{0,1},out))return 10;
 const uint8_t excessive_generation[]={1,0,0,1,0,0};
 if(decode_xref_stream_entries(excessive_generation,sizeof(excessive_generation),
                               {1,2,3},{1,1},out))return 11;
 // Read an actual indirect XRef stream with a direct /Length.
 std::string pdf="%PDF-1.5\n";
 const size_t offset=pdf.size();
 pdf+="5 0 obj\n<< /Type /XRef /Size 3 /W [1 2 1] /Length 12 >>\nstream\n";
 pdf.append(reinterpret_cast<const char*>(sample),sizeof(sample));
 pdf+="\nendstream\nendobj\n";
 Value trailer;
 if(!read_xref_stream_at(reinterpret_cast<const uint8_t*>(pdf.data()),
        pdf.size(),offset,out,trailer))return 12;
 if(out.size()!=3||out[2].type!=2||
    trailer.dictionary.find("Size")==trailer.dictionary.end())return 13;
 std::string damaged=pdf;
 damaged.replace(damaged.find("/Length 12"),10,"/Length 11");
 if(read_xref_stream_at(reinterpret_cast<const uint8_t*>(damaged.data()),
        damaged.size(),offset,out,trailer))return 14;
 // Dispatch based on the actual startxref offset (not PDF version).
 pdf+="startxref\n"+std::to_string(offset)+"\n%%EOF\n";
 XrefFormat format=XrefFormat::Classic;
 std::vector<XrefEntry> classic;
 if(!read_xref_section(reinterpret_cast<const uint8_t*>(pdf.data()),
       pdf.size(),format,classic,out,trailer))return 15;
 if(format!=XrefFormat::Stream||out.size()!=3||!classic.empty())return 16;
 // The malformed pointer must not silently scan for another xref.
 std::string wrong=pdf.substr(0,pdf.find("startxref"));
 wrong+="startxref\n999999\n%%EOF\n";
 if(read_xref_section(reinterpret_cast<const uint8_t*>(wrong.data()),
       wrong.size(),format,classic,out,trailer))return 17;
 // End-to-end public page tree lookup using an uncompressed XRef stream.
 std::string example="%PDF-1.5\n";
 std::vector<size_t> offsets(5,0);
 auto append_object=[&](unsigned id,const std::string& body){
  offsets[id]=example.size();
  example+=std::to_string(id)+" 0 obj\n"+body+"\nendobj\n";
 };
 append_object(1,"<< /Type /Catalog /Pages 2 0 R >>");
 append_object(2,"<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
 append_object(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 20 20] >>");
 offsets[4]=example.size();
 std::string binary;
 auto entry=[&](unsigned type,size_t position,unsigned generation){
  binary.push_back(static_cast<char>(type));
  binary.push_back(static_cast<char>((position>>16)&255));
  binary.push_back(static_cast<char>((position>>8)&255));
  binary.push_back(static_cast<char>(position&255));
  binary.push_back(static_cast<char>(generation));
 };
 entry(0,0,255);
 for(unsigned id=1;id<5;++id)entry(1,offsets[id],0);
 example+="4 0 obj\n<< /Type /XRef /Size 5 /Root 1 0 R /W [1 3 1] /Length 25 >>\nstream\n";
 example+=binary;
 example+="\nendstream\nendobj\nstartxref\n"+std::to_string(offsets[4])+"\n%%EOF\n";
 rinpdf_document* document=nullptr;
 if(rinpdf_document_open_memory(reinterpret_cast<const uint8_t*>(example.data()),
                                example.size(),&document)!=RINPDF_OK)return 18;
 size_t pages=0;
 const auto page_status=rinpdf_document_page_count(document,&pages);
 rinpdf_document_close(document);
 if(page_status!=RINPDF_OK||pages!=1)return 19;
 // Latest XRef Stream referencing an older Classic section.
 std::string hybrid_history="%PDF-1.5\n";
 const size_t old_object=hybrid_history.size();
 hybrid_history+="1 0 obj\n<< /Type /Catalog >>\nendobj\n";
 const size_t old_section=hybrid_history.size();
 auto width10=[](size_t v){std::string s=std::to_string(v);return std::string(10-s.size(),'0')+s;};
 hybrid_history+="xref\n0 2\n0000000000 65535 f \n"+
  width10(old_object)+" 00000 n \ntrailer\n<< /Size 3 /Root 1 0 R >>\n";
 const size_t newest=hybrid_history.size();
 hybrid_history+="2 0 obj\n<< /Type /XRef /Size 3 /Index [2 1] /W [1 3 1] /Length 5 /Prev "+
  std::to_string(old_section)+" >>\nstream\n";
 // The xref stream's own offset is the type-1 entry.
 std::string encoded(5,'\0');
 encoded[0]=1;
 encoded[1]=static_cast<char>((newest>>16)&255);
 encoded[2]=static_cast<char>((newest>>8)&255);
 encoded[3]=static_cast<char>(newest&255);
 hybrid_history+=encoded;
 hybrid_history+="\nendstream\nendobj\nstartxref\n"+
  std::to_string(newest)+"\n%%EOF\n";
 std::vector<XrefEntry> normal_entries;
 std::vector<XrefStreamEntry> stream_entries;
 Value mixed_trailer;
 if(!read_object_xref(reinterpret_cast<const uint8_t*>(hybrid_history.data()),
       hybrid_history.size(),normal_entries,stream_entries,mixed_trailer))return 18;
 if(normal_entries.size()!=2||normal_entries[0].object!=1||
    normal_entries[0].offset!=old_object||
    !mixed_trailer.dictionary.count("Root"))return 19;
 // Same-revision hybrid: Classic table plus supplementary /XRefStm.
 std::string hybrid="%PDF-1.5\\n";
 const size_t catalog_pos=hybrid.size();
 hybrid+="1 0 obj\\n<< /Type /Catalog >>\\nendobj\\n";
 const size_t container_pos=hybrid.size();
 hybrid+="4 0 obj\\n<< /Type /ObjStm /N 1 /First 4 /Length 6 >>\\nstream\\n";
 hybrid+="3 0 42\\n\\nendstream\\nendobj\\n";
 const size_t supplementary_pos=hybrid.size();
 std::string compressed_entry(5,'\\0');
 compressed_entry[0]=2;
 compressed_entry[3]=4;
 hybrid+="5 0 obj\\n<< /Type /XRef /Size 6 /Index [3 1] /W [1 3 1] /Length 5 >>\\nstream\\n";
 hybrid+=compressed_entry;
 hybrid+="\\nendstream\\nendobj\\n";
 const size_t classic_pos=hybrid.size();
 hybrid+="xref\\n0 2\\n0000000000 65535 f \\n"+width10(catalog_pos)+
  " 00000 n \\n4 2\\n"+width10(container_pos)+" 00000 n \\n"+
  width10(supplementary_pos)+" 00000 n \\n";
 hybrid+="trailer\\n<< /Size 6 /Root 1 0 R /XRefStm "+
  std::to_string(supplementary_pos)+" >>\\nstartxref\\n"+
  std::to_string(classic_pos)+"\\n%%EOF\\n";
 if(!read_object_xref(reinterpret_cast<const uint8_t*>(hybrid.data()),
      hybrid.size(),normal_entries,stream_entries,mixed_trailer))return 20;
 bool found_compressed=false;
 for(const auto& item:stream_entries)
  if(item.object==3&&item.type==2&&item.field1==4&&item.field2==0)
   found_compressed=true;
 if(!found_compressed||normal_entries.size()!=4)return 21;
 // Reject an out-of-range supplementary stream pointer.
 std::string bad_hybrid=hybrid;
 const std::string pointer="/XRefStm "+std::to_string(supplementary_pos);
 bad_hybrid.replace(bad_hybrid.find(pointer),pointer.size(),"/XRefStm 999999");
 if(read_object_xref(reinterpret_cast<const uint8_t*>(bad_hybrid.data()),
      bad_hybrid.size(),normal_entries,stream_entries,mixed_trailer))return 22;
 return 0;
}
