# llvm based parser

## Readings / Information

- Official Docs : getting started with VS
  - https://llvm.org/docs/GettingStartedVS.html
- gettings started with VS short guide
  - https://cxuesong.com/archives/1056
- Youtube Guide
  - https://www.youtube.com/watch?v=8QvLVEaxzC8
  - example clang query 19:40
- Youtube: CLANG AST Description
  - https://www.youtube.com/watch?v=VqCkCDFLSsc
  - Unit Test : 29:17
- AST Matchers guide's
  - Simple : https://xinhuang.github.io/posts/2015-02-08-clang-tutorial-the-ast-matcher.html
- Demo Metareflect
  - https://github.com/Leandros/metareflect


## Clang Query
- Guide from Microsoft
  - https://devblogs.microsoft.com/cppblog/exploring-clang-tooling-part-2-examining-the-clang-ast-with-clang-query/

## AST with CLANG dump
cd C:\temp\00_Coding\github_andsch_llvm-project\build\Debug\bin

clang -Xclang -ast-dump "..\..\..\_sush\demo.hpp"


## BUILD


cmake --help
... to check wchich generators are available#
-> on my PC (visual studio 2019) was marked as default 


// different to tutorial to include clang-tools-extra for clang-query
cd C:\temp\00_Coding\github_llvm\llvm

set MYPYTHON3=C:\Tools\WPy64-3860\python-3.8.6.amd64\python.exe
set MYCMAKE=cmake

%MYCMAKE% -S llvm -B build -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" -DLLVM_TARGETS_TO_BUILD=X86 -Thost=x64 -G "Visual Studio 16 2019" -DPython3_EXECUTABLE=%MYPYTHON3%

REM generates visual studio solution in folder "build"



