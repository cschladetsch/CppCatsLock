cmake_minimum_required(VERSION 3.20)

project(CppCatsLock LANGUAGES CXX)

add_executable(catslock WIN32
    src/catslock.cpp
)

target_compile_features(catslock PRIVATE cxx_std_17)
target_compile_definitions(catslock PRIVATE UNICODE _UNICODE)
