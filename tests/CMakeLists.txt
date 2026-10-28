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
    set(TEST_SOURCES
      main.cpp

      # Unit Tests
      ast_node_tests.cpp
      compiler_tests.cpp
      environment_tests.cpp
      expression_tests.cpp
      generator_tests.cpp
      list_expression_tests.cpp
      namemangler_tests.cpp
      testing_framework_tests.cpp

      # JIT Integration Tests
      jit_array_tests.cpp
      jit_buffer_tests.cpp
      jit_control_flow_tests.cpp
      jit_core_tests.cpp
      jit_domain_tests.cpp
      jit_function_tests.cpp
      jit_multirate_tests.cpp
      jit_nested_function_tests.cpp
      jit_statemachine_tests.cpp
      jit_stream_tests.cpp
      jit_string_tests.cpp
      jit_type_tests.cpp

      # Tooling & Diagnostics
      debug_tests.cpp
      error_tests.cpp
      stridec_tests.cpp
    )
    add_executable(stridejit_tests ${TEST_SOURCES})

    set_target_properties(stridejit_tests PROPERTIES
      CXX_STANDARD 17
      CXX_STANDARD_REQUIRED ON
    )
    add_test(NAME stridejit_tests COMMAND stridejit_tests)

    if (TARGET StrideTesting)
      target_link_libraries(stridejit_tests PUBLIC StrideParser StrideJit StrideTesting gtest)
    else()
      target_link_libraries(stridejit_tests PUBLIC StrideParser StrideJit gtest)
    endif()
    target_compile_definitions(stridejit_tests PUBLIC
      "-DSTRIDEJIT_TESTS_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data/\"")

    if (STRIDEPARSER_CODE_COVERAGE)
      set_target_properties(stridejit_tests PROPERTIES COMPILE_FLAGS "-fPIE -g -O0 -Wall -fprofile-arcs -ftest-coverage")
      target_link_libraries(stridejit_tests PUBLIC gcov)
    endif()

    if (TARGET stridetest)
      file(GLOB STRIDE_TEST_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/*Test.stride")
      foreach(TEST_FILE ${STRIDE_TEST_FILES})
        get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
        if (STRIDEROOT)
          add_test(
            NAME "stridetest.${TEST_NAME}"
            COMMAND stridetest "${TEST_FILE}" --root "${STRIDEROOT}"
          )
        else()
          add_test(
            NAME "stridetest.${TEST_NAME}"
            COMMAND stridetest "${TEST_FILE}"
          )
        endif()
        set_tests_properties("stridetest.${TEST_NAME}" PROPERTIES
          LABELS "StrideLanguage"
          TIMEOUT 30
        )
      endforeach()

      if (STRIDEROOT AND EXISTS "${STRIDEROOT}/library/1.0/tests")
        file(GLOB STRIDE_LIB_TEST_FILES CONFIGURE_DEPENDS "${STRIDEROOT}/library/1.0/tests/*Test.stride")
        foreach(LIB_TEST_FILE ${STRIDE_LIB_TEST_FILES})
          get_filename_component(LIB_TEST_NAME ${LIB_TEST_FILE} NAME_WE)
          add_test(
            NAME "stridetest.library.${LIB_TEST_NAME}"
            COMMAND stridetest "${LIB_TEST_FILE}" --root "${STRIDEROOT}"
          )
          set_tests_properties("stridetest.library.${LIB_TEST_NAME}" PROPERTIES
            LABELS "StrideLibrary"
            TIMEOUT 30
          )
        endforeach()
      endif()
    endif()
endif()
