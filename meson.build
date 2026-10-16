project('rinpdf','cpp',version:'0.54.6',default_options:['cpp_std=c++20','warning_level=3'])
inc=include_directories('include')
zlib_dep=dependency('zlib',required:true)
sources=files('src/parser/parser.cpp','src/parser/lexer.cpp','src/parser/value.cpp','src/parser/pages.cpp','src/parser/contents.cpp','src/parser/stream.cpp','src/parser/stream_object.cpp','src/parser/flate.cpp','src/parser/xref.cpp','src/parser/object.cpp','src/document/document.cpp','src/renderer/rectangles.cpp','src/renderer/path_mask.cpp','src/security/error.cpp','src/writer/writer.cpp')
# On WSL builds located on DrvFS (/mnt/c, /mnt/e, ...), Windows
# symlink semantics can prevent the ELF SONAME symlink librinpdf.so.0
# from being created. An unversioned library avoids that runtime dependency.
# The public ABI still needs a deliberate versioning policy before release.
lib=library('rinpdf',sources,include_directories:inc,dependencies:zlib_dep,install:true)
install_headers('include/rinpdf/rinpdf.h','include/rinpdf/document.h','include/rinpdf/page.h','include/rinpdf/render.h','include/rinpdf/error.h',subdir:'rinpdf')
rinpdf_dep=declare_dependency(link_with:lib,include_directories:inc,dependencies:zlib_dep)
subdir('tests')
