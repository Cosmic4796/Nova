cmake_minimum_required(VERSION 3.10)
project(nova C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Default to Release build
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2")

# Warnings
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# Nova core as a static library (shared by executable and stdlib modules)
set(NOVA_CORE_SOURCES
    src/memory.c
    src/value.c
    src/object.c
    src/table.c
    src/token.c
    src/lexer.c
    src/ast.c
    src/parser.c
    src/env.c
    src/interpreter.c
    src/builtins.c
    src/module.c
    src/error.c
)

add_library(nova_core STATIC ${NOVA_CORE_SOURCES})
target_include_directories(nova_core PUBLIC include)
set_target_properties(nova_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Nova executable
add_executable(nova src/main.c)
target_include_directories(nova PRIVATE include)
target_link_libraries(nova nova_core m dl)

# Export symbols from executable so dlopen'd libs can use them
if(APPLE)
    set_target_properties(nova PROPERTIES LINK_FLAGS "-Wl,-export_dynamic")
else()
    set_target_properties(nova PROPERTIES LINK_FLAGS "-Wl,-export-dynamic")
endif()

# Install stdlib relative to binary
set(NOVA_STDLIB_DIR "${CMAKE_BINARY_DIR}/stdlib")
file(MAKE_DIRECTORY ${NOVA_STDLIB_DIR})

# Helper function to build stdlib modules
function(nova_stdlib_module name)
    add_library(nova_${name} SHARED stdlib/${name}.c)
    target_include_directories(nova_${name} PRIVATE include)
    # Do NOT link nova_core into modules — they resolve symbols from the
    # host executable at runtime.  This prevents duplicate global state
    # (e.g. the string intern table) which breaks interning.
    if(APPLE)
        target_link_libraries(nova_${name} ${ARGN})
        set_target_properties(nova_${name} PROPERTIES
            LINK_FLAGS "-undefined dynamic_lookup"
        )
    else()
        target_link_libraries(nova_${name} ${ARGN})
    endif()
    set_target_properties(nova_${name} PROPERTIES
        OUTPUT_NAME "${name}"
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${NOVA_STDLIB_DIR}"
    )
endfunction()

# Stdlib modules
nova_stdlib_module(math m)
nova_stdlib_module(io)
nova_stdlib_module(string)
nova_stdlib_module(os)
nova_stdlib_module(random)
nova_stdlib_module(json)
nova_stdlib_module(collections)
nova_stdlib_module(path)
nova_stdlib_module(datetime)
target_compile_definitions(nova_datetime PRIVATE _XOPEN_SOURCE=700)
nova_stdlib_module(csv)
nova_stdlib_module(regex)

# Optional: HTTP module (requires libcurl)
find_package(CURL QUIET)
if(CURL_FOUND)
    nova_stdlib_module(http ${CURL_LIBRARIES})
    target_include_directories(nova_http PRIVATE ${CURL_INCLUDE_DIRS})
endif()

# Optional: Game module (requires raylib)
find_package(raylib QUIET)
if(raylib_FOUND)
    nova_stdlib_module(game raylib)
    message(STATUS "Building game module with raylib")
else()
    message(STATUS "raylib not found — skipping game module (brew install raylib)")
endif()

# Copy examples to build dir
file(GLOB NOVA_EXAMPLES "examples/*.nova")
file(COPY ${NOVA_EXAMPLES} DESTINATION "${CMAKE_BINARY_DIR}/examples")
