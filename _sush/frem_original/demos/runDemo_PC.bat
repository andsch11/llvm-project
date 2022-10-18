REM parse demo.hpp
REM outputfile : generated.yaml

  FremGen.exe -ftemplate-depth=1024 -std=c++17 -Wno-error -target arm-none-eabi -I..\include  --out generated.yaml --source demo.hpp  --header-archive header-archive_GCC_ARM_9.2.1.dat
  pause
