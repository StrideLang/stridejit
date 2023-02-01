cmake_minimum_required(VERSION 3.13) 
project(stridejit)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# For details on LLVM Cmake options https://llvm.org/docs/CMake.html
message("Building StrideJit with LLVM")

#include(ExternalProject)
#ExternalProject_Add(llvm
#    GIT_REPOSITORY https://github.com/llvm/llvm-project.git
#    GIT_TAG        llvmorg-14.0.6
#    SOURCE_SUBDIR llvm
#    CMAKE_ARGS -DLLVM_TARGETS_TO_BUILD=X86
#    BUILD_COMMAND cmake --build . --target install
#    LOG_CONFIGURE 1
#    LOG_BUILD 1
#    LOG_INSTALL 1
#)


set(LLVM_DIR ${CMAKE_MODULE_PATH} "C:/Users/Andres/source/repos/boardgame/libgame/external/stridejit/external/llvm-prefix/src/llvm/llvm/out/install/x64-Release/lib/cmake/llvm")
set(LLVM_DIR ${CMAKE_MODULE_PATH} "C:/Users/Andres/source/repos/boardgame/libgame/external/stridejit/external/llvm-prefix/src/llvm/llvm/out/install/x64-Debug/lib/cmake/llvm")

find_package(LLVM REQUIRED CONFIG)

set(STRIDEJIT_SRC
  src/exprast.cpp
  src/listexprast.cpp
  src/numberexprast.cpp
  src/functionast.cpp

  src/strideenvironment.cpp
  src/stridecompiler.cpp
  src/stridegenerator.cpp
)

set(STRIDEJIT_HEADERS
  include/exprast.hpp
  include/listexprast.hpp
  include/numberexprast.hpp
  include/functionast.hpp

  include/strideenvironment.hpp
  include/stridecompiler.hpp
  include/stridegenerator.hpp
)

add_library(StrideJit 
  ${STRIDEJIT_SRC} ${STRIDEJIT_HEADERS}
)

target_compile_options(StrideJit PRIVATE -fPIC)
target_link_libraries(StrideJit PUBLIC StrideParser StrideCodegen)
target_include_directories(StrideJit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
#add_dependencies(StrideJit llvm)

#llvm_map_components_to_libnames(llvm_libs support core irreader)

# Link against LLVM libraries
target_include_directories(StrideJit PUBLIC ${LLVM_INCLUDE_DIRS})

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
#target_compile_definitions(StrideJit PUBLIC ${LLVM_DEFINITIONS_LIST})

llvm_map_components_to_libnames(llvm_libs
    support core irreader orcjit
    nativecodegen
    executionengine
    jitlink
    #[[native]]
    # For object generation
    #aarch64
    aarch64asmparser
    aarch64codegen #aarch64desc aarch64disassembler aarch64info aarch64utils
    #x86
    x86asmparser
    x86codegen #x86desc x86disassembler x86info x86targetmca
    #arm
    armasmparser
    armcodegen #armdesc armdisassembler arminfo armutils
    #webassembly
    webassemblyasmparser
    webassemblycodegen #webassemblydesc webassemblydisassembler webassemblyinfo webassemblyutils
)
target_link_libraries(StrideJit PUBLIC ${llvm_libs})

set(STRIDELIB_SRC
  src/libcompare.cpp
)

set(STRIDELIB_HEADERS
  include/libcompare.hpp
)

add_library(StrideLib SHARED
  ${STRIDELIB_SRC} ${STRIDELIB_HEADERS}
)

target_compile_options(StrideLib PUBLIC "-rdynamic")
target_include_directories(StrideLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(StrideLib PUBLIC
  "-DSTRIDELIB_PATH=\"${CMAKE_CURRENT_BINARY_DIR}\"")

add_subdirectory(tests)

