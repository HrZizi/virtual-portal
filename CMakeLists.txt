cmake_minimum_required(VERSION 3.20)
project(virtual_portal LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force x86 (32-bit) build
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This project must be compiled as x86 (32-bit). Please use an x86 generator.")
endif()

# Include directories
include_directories(src/common)
include_directories(src/portal)
include_directories(src/graphics)
include_directories(deps/MinHook/include)

# Common source files
file(GLOB COMMON_SRC 
    src/common/*.cpp 
    src/common/*.h
)

# MinHook source files
file(GLOB MINHOOK_SRC 
    deps/MinHook/src/buffer.c
    deps/MinHook/src/hook.c
    deps/MinHook/src/trampoline.c
    deps/MinHook/src/hde/hde32.c
)

# Portal + Graphics (dinput8.dll)
file(GLOB PORTAL_SRC 
    src/portal/*.cpp 
    src/portal/*.h
)

add_library(dinput8 SHARED 
    ${COMMON_SRC}
    ${PORTAL_SRC}
    ${MINHOOK_SRC}
    src/portal/dinput8_exports.def
)

target_link_libraries(dinput8 PRIVATE setupapi hid user32)

set_target_properties(dinput8 PROPERTIES 
    PREFIX ""
    OUTPUT_NAME "dinput8"
)
