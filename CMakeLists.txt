cmake_minimum_required(VERSION 3.20)
project(virtual_portal LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force x86 (32-bit) build
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This project must be compiled as x86 (32-bit). Please use an x86 generator.")
endif()

# Include directories
include_directories(src)
include_directories(deps/MinHook/include)

# Source files
file(GLOB_RECURSE SRC_FILES src/*.cpp src/*.h)
file(GLOB MINHOOK_SRC 
    deps/MinHook/src/buffer.c
    deps/MinHook/src/hook.c
    deps/MinHook/src/trampoline.c
    deps/MinHook/src/hde/hde32.c
)

# Shared library
add_library(dinput8 SHARED 
    ${SRC_FILES} 
    ${MINHOOK_SRC}
    src/dinput8_exports.def
)

# Link libraries
target_link_libraries(dinput8 PRIVATE setupapi hid)

# Output naming
set_target_properties(dinput8 PROPERTIES 
    PREFIX ""
    OUTPUT_NAME "dinput8"
)
