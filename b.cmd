@echo off
setlocal

cmake -S . -B build-clang-uac -G Ninja -DCMAKE_CXX_COMPILER=clang++ || exit /b %ERRORLEVEL%
cmake --build build-clang-uac
