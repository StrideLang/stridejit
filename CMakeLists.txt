cmake_minimum_required(VERSION 3.13) 
project(stridejit)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(STRIDEJIT_BUILD_LLVM ON)

message("Building StrideJit with LLVM")


# For details on LLVM Cmake options https://llvm.org/docs/CMake.html
  # LLVM ------------------------------------------------------
  # For Windows:
  # First clone llvm, then build and intall through cmake
  # git clone https://github.com/llvm/llvm-project.git --branch llvmorg-14.0.6 --depth 1
  # Make sure the installation path is not too long as it causes build issues.
  # TODO check if cloning through cmake as external project works.
  # Did not work for me on windows, perhaps just the path was the issue?
  # Also cloning and local building takes a very long time, and makes local configuration time musch longer.

if (STRIDEJIT_BUILD_LLVM)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include(FetchContent)
# set(FETCHCONTENT_BASE_DIR "${PROJECT_SOURCE_DIR}/external")
FetchContent_Declare(
  llvm-project
  GIT_REPOSITORY https://github.com/llvm/llvm-project.git
  GIT_TAG        llvmorg-22.1.5  # Use a specific version tag
  SOURCE_SUBDIR  llvm            # Target the llvm subdirectory in the monorepo
)

# Optional: Set LLVM-specific variables before making it available
# set(LLVM_ENABLE_PROJECTS "clang" CACHE STRING "")
set(LLVM_BUILD_TOOLS OFF CACHE BOOL "")
set(LLVM_TARGETS_TO_BUILD "X86;AArch64;ARM;WebAssembly" CACHE STRING "")
set(LLVM_ENABLE_THREADS OFF CACHE BOOL "")
set(LLVM_INCLUDE_TESTS OFF CACHE BOOL "")
set(LLVM_BUILD_TESTS OFF CACHE BOOL "")
set(CLANG_INCLUDE_TESTS OFF CACHE BOOL "")

FetchContent_MakeAvailable(llvm-project)

message("Bulding LLVM in: ${llvm-project_BINARY_DIR}/include")

add_definitions(${LLVM_DEFINITIONS})
set(LLVM_INCLUDE_DIRS
    ${llvm-project_SOURCE_DIR}/llvm/include
    ${llvm-project_BINARY_DIR}/include
    ${llvm-project_BINARY_DIR}/llvm/include
)

else (STRIDEJIT_BUILD_LLVM)

# llvm_map_components_to_libnames(llvm_libs
#     support core irreader orcjit
#     nativecodegen
#     executionengine
#     jitlink
#     #[[native]]
#     # For object generation
#     #aarch64
#     aarch64asmparser
#     aarch64codegen #aarch64desc aarch64disassembler aarch64info aarch64utils
#     #x86
#     x86asmparser
#     x86codegen #x86desc x86disassembler x86info x86targetmca
#     #arm
#     armasmparser
#     armcodegen #armdesc armdisassembler arminfo armutils
#     #webassembly
#     webassemblyasmparser
#     webassemblycodegen #webassemblydesc webassemblydisassembler webassemblyinfo webassemblyutils
# )
# Downloaded from: https://github.com/llvm/llvm-project/releases/tag/llvmorg-22.1.0


set(LLVM_DIR "${CMAKE_BINARY_DIR}/_deps/llvm-project-build/lib/cmake/llvm")
# This is needed if you have moved the llvm build directory

message("${LLVM_DIR}")

  find_package(LLVM REQUIRED CONFIG
    COMPONENTS
    Support
    Core
    IRReader
    OrcJIT
    NativeCodegen
    Jitlink
    AllTargetsInfos
  )
endif()


# StrideJIT ------------------------------
set(STRIDEJIT_SRC
  src/exprast.cpp
  src/binaryexprast.cpp
  src/listexprast.cpp
  src/numberexprast.cpp
  src/functionast.cpp

  src/strideenvironment.cpp
  src/stridecompiler.cpp
  src/stridegenerator.cpp
)

set(STRIDEJIT_HEADERS
  include/stride/stridejit/exprast.hpp
  include/stride/stridejit/binaryexprast.hpp
  include/stride/stridejit/listexprast.hpp
  include/stride/stridejit/numberexprast.hpp
  include/stride/stridejit/functionast.hpp

  include/stride/stridejit/strideenvironment.hpp
  include/stride/stridejit/stridecompiler.hpp
  include/stride/stridejit/stridegenerator.hpp
)

add_library(StrideJit 
  ${STRIDEJIT_SRC} ${STRIDEJIT_HEADERS}
  doc/code_geneartion.md
)

target_compile_options(StrideJit PRIVATE -fPIC)
target_link_libraries(StrideJit PUBLIC StrideParser StrideUtils StrideCodegen)
target_include_directories(StrideJit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link against LLVM libraries -----------------------

target_include_directories(StrideJit PUBLIC ${LLVM_INCLUDE_DIRS} ${LLVM_STRIDEJIT_INCLUDES})
target_link_libraries(StrideJit PUBLIC
  LLVMSupport
  LLVMCore
  LLVMIRReader
  LLVMOrcJIT
  LLVMExecutionEngine
  LLVMMIRParser
  LLVMAArch64AsmParser
  LLVMAArch64CodeGen
  LLVMX86AsmParser
  LLVMX86CodeGen
  LLVMARMAsmParser
  LLVMARMCodeGen
  LLVMWebAssemblyAsmParser
  LLVMWebAssemblyCodeGen
)


# separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})

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

