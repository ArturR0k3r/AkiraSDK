cmake_minimum_required(VERSION 3.20)

project(AkiraSdk LANGUAGES C)

# The Akira SDK is header-only. Create an INTERFACE target so CMake-based
# consumers can simply `target_link_libraries(... PRIVATE akira_api)` and get
# the include directories without requiring a static library.
add_library(akira_api INTERFACE)
target_include_directories(akira_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
message(STATUS "AkiraSDK: configured as header-only INTERFACE target 'akira_api'")

# Also provide an ocre_api compatibility target (local implementation) if sources are present
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/ocre_api.c")
	add_library(ocre_api STATIC src/ocre_api.c)
	target_include_directories(ocre_api PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
	target_compile_options(ocre_api PRIVATE -O3 -Wall -Wextra -Wno-unused-parameter -Wno-unknown-attributes)
endif()

message(STATUS "AkiraSDK: configured (header-only)")

# Install headers and provide a simple INTERFACE target for CMake consumers.
install(FILES include/akira_api.h DESTINATION include)
