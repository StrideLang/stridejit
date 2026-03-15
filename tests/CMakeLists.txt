cmake_minimum_required(VERSION 3.8)
project(stridejit_tests)

option(STRIDEJIT_BUILD_TESTS "Build and run unit test for the Stride parser" OFF)

if (STRIDEJIT_BUILD_TESTS)
    # Get and build googletest
    if (NOT gtest)
#        message("Building googletest with stridejit build")
        include(FetchContent)
        FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        release-1.12.0
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
    set(TEST_SOURCES main.cpp
      basic.cpp
      jit.cpp
      error_tests.cpp
      ast_node_tests.cpp
    )
    add_executable(stridejit_tests ${TEST_SOURCES})

    set_target_properties(stridejit_tests PROPERTIES
      CXX_STANDARD 17
      CXX_STANDARD_REQUIRED ON
    )
    add_test(NAME stridejit_tests COMMAND stridejit_tests)

    target_link_libraries(stridejit_tests PUBLIC StrideParser StrideJit gtest)
    target_compile_definitions(stridejit_tests PUBLIC
      "-DSTRIDEJIT_TESTS_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data/\"")

    if (STRIDEPARSER_CODE_COVERAGE)
      set_target_properties(stridejit_tests PROPERTIES COMPILE_FLAGS "-fPIE -g -O0 -Wall -fprofile-arcs -ftest-coverage")
      target_link_libraries(stridejit_tests PUBLIC gcov)
    endif()
endif()
