include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 6.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

add_executable(block-demo
    ${PROJECT_SOURCE_DIR}/src/main.cpp
    raylib_api.cpp
    array_api.cpp
)

target_link_libraries(block-demo PRIVATE lang_core raylib)
target_compile_definitions(block-demo PRIVATE ENABLE_HOST_API)
set_target_properties(block-demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
