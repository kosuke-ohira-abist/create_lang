cmake_minimum_required(VERSION 3.24)
project(lang VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE LANG_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM LANG_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_library(lang_core STATIC ${LANG_CORE_SOURCES})
target_include_directories(lang_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(lang_core PUBLIC cxx_std_20)

add_executable(lang src/main.cpp)
target_link_libraries(lang PRIVATE lang_core)
set_target_properties(lang PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

function(lang_enable_warnings target)
    target_compile_options(${target} PRIVATE
        "$<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>")
endfunction()
lang_enable_warnings(lang_core)
lang_enable_warnings(lang)

option(BUILD_FPS_DEMO "Build FPS demo" OFF)

if(BUILD_FPS_DEMO)
    add_subdirectory(examples/fps_demo)
endif()

option(BUILD_BLOCK_DEMO "Build falling-block example" OFF)

if(BUILD_BLOCK_DEMO)
    add_subdirectory(examples/block_demo)
endif()

include(CTest)
if(BUILD_TESTING)
    include(FetchContent)
    set(BUILD_GMOCK OFF CACHE BOOL "Build GoogleMock")
    set(INSTALL_GTEST OFF CACHE BOOL "Install GoogleTest")
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        # GoogleTest v1.18.0, pinned to an immutable commit.
        GIT_TAG 063de7e9578f82b369302001269680b4b1553359
    )
    FetchContent_MakeAvailable(googletest)

    file(GLOB_RECURSE LANG_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    add_executable(lang_tests ${LANG_TEST_SOURCES})
    target_link_libraries(lang_tests PRIVATE lang_core GTest::gtest_main)
    lang_enable_warnings(lang_tests)
    include(GoogleTest)
    gtest_discover_tests(lang_tests)
endif()
