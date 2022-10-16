# clang-query examples

https://devblogs.microsoft.com/cppblog/exploring-clang-tooling-part-2-examining-the-clang-ast-with-clang-query/

https://firefox-source-docs.mozilla.org/code-quality/static-analysis/writing-new/clang-query.html

https://steveire.wordpress.com/category/clang/
godbolt link


c++ examples: https://github.com/lanl/CoARCT


clang -Xclang -ast-dump  _demo.hpp


clang-query.exe _demo.hpp --


set print-matcher true
set output dump
enable output print

m functionDecl(hasName("test2"))

m cxxRecordDecl(hasName("demo_struct"))