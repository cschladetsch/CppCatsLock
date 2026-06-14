cmake_minimum_required(VERSION 3.20)

project(CppCatsLock LANGUAGES CXX)

add_executable(catslock WIN32
    src/catslock.cpp
)

target_compile_features(catslock PRIVATE cxx_std_17)
target_compile_definitions(catslock PRIVATE UNICODE _UNICODE)
target_link_libraries(catslock PRIVATE Advapi32)
target_link_options(catslock PRIVATE "LINKER:/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
