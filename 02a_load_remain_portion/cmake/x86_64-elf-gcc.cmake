# Cross-compilation toolchain file for bare-metal x86 (32-bit)
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-elf-gcc.cmake ..

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86)

# Toolchain prefix: x86_64-elf- for macOS/Windows, x86_64-linux-gnu- for Linux
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(TOOL_PREFIX "x86_64-linux-gnu-")
else()
    set(TOOL_PREFIX "x86_64-elf-")
endif()

set(CMAKE_C_COMPILER   ${TOOL_PREFIX}gcc)
set(CMAKE_ASM_COMPILER ${TOOL_PREFIX}gcc)
set(CMAKE_LINKER        ${TOOL_PREFIX}ld)
set(CMAKE_OBJCOPY       ${TOOL_PREFIX}objcopy)
set(CMAKE_OBJDUMP       ${TOOL_PREFIX}objdump)
set(CMAKE_READELF       ${TOOL_PREFIX}readelf)

# Skip compiler checks (bare-metal, no standard library)
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_ASM_COMPILER_WORKS 1)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Bare-metal compile flags
set(CMAKE_C_FLAGS   "-g -O0 -m32 -fno-pie -fno-stack-protector -nostdlib -nostdinc" CACHE STRING "" FORCE)
set(CMAKE_ASM_FLAGS "-g -O0 -m32 -fno-pie -fno-stack-protector -nostdlib -nostdinc" CACHE STRING "" FORCE)
