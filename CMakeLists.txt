cmake_minimum_required(VERSION 3.13) 
project(stridejit)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# For details on LLVM Cmake options https://llvm.org/docs/CMake.html
message("Building StrideJit with LLVM")

# For Windows:
# First clone llvm, then build and intall through cmake
# git clone https://github.com/llvm/llvm-project.git --branch llvmorg-14.0.6 --depth 1
# Make sure the installation path is not too long as it causes build issues.
# TODO check if cloning through cmake as external project works.
# Did not work for me on windows, perhaps just the path was the issue?

include(ExternalProject)
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

set(LLVM_DIR "C:\\Users\\Andres\\source\\repos\\llvm-install\\lib\\cmake\\llvm")

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
  include/stride/stridejit/exprast.hpp
  include/stride/stridejit/listexprast.hpp
  include/stride/stridejit/numberexprast.hpp
  include/stride/stridejit/functionast.hpp

  include/stride/stridejit/strideenvironment.hpp
  include/stride/stridejit/stridecompiler.hpp
  include/stride/stridejit/stridegenerator.hpp
)

add_library(StrideJit 
  ${STRIDEJIT_SRC} ${STRIDEJIT_HEADERS}
)

target_compile_options(StrideJit PRIVATE -fPIC)
target_link_libraries(StrideJit PUBLIC StrideParser StrideUtils StrideCodegen)
target_include_directories(StrideJit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
#add_dependencies(StrideJit llvm)

# Link against LLVM libraries
target_include_directories(StrideJit PUBLIC ${LLVM_INCLUDE_DIRS})

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})

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

#set(STRIDELIB_SRC
#  src/libcompare.cpp
#)

#set(STRIDELIB_HEADERS
#  include/libcompare.hpp
#)

#add_library(StrideLib SHARED
#  ${STRIDELIB_SRC} ${STRIDELIB_HEADERS}
#)

#target_compile_options(StrideLib PUBLIC "-rdynamic")
#target_include_directories(StrideLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
#target_compile_definitions(StrideLib PUBLIC
#  "-DSTRIDELIB_PATH=\"${CMAKE_CURRENT_BINARY_DIR}\"")

add_subdirectory(tests)

