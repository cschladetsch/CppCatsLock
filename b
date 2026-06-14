#!/usr/bin/env sh
set -eu

cmake -S . -B build-clang-uac -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build-clang-uac
