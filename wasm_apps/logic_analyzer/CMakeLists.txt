cmake_minimum_required(VERSION 3.20)
set(CMAKE_TOOLCHAIN_FILE /opt/wasi-sdk/share/cmake/wasi-sdk.cmake CACHE PATH "WASI SDK toolchain file")
set(APPNAME logic-analyzer)
project(${APPNAME} LANGUAGES C)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../CMakeLists.txt")
    message(FATAL_ERROR "Akira SDK not found as expected (add via add_subdirectory ../../ )")
endif()

# Add the parent SDK as a subdirectory so we can consume the akira_api target
if(NOT TARGET akira_api)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../ ${CMAKE_BINARY_DIR}/akira-sdk-build)
endif()

add_executable(${APPNAME}.wasm main.c)
target_include_directories(${APPNAME}.wasm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
if(TARGET akira_api)
    target_link_libraries(${APPNAME}.wasm PRIVATE akira_api)
else()
    message(WARNING "AkiraSDK: akira_api target not available - building header-only; runtime must provide implementations")
endif()
