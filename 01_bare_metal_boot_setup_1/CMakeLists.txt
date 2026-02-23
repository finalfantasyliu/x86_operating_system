cmake_minimum_required(VERSION 3.16)

project(bare_metal_boot LANGUAGES C ASM)

# ── Build os.elf ──────────────────────────────────────────────
add_executable(os.elf)

# Use target_sources instead of passing sources to add_executable directly.
# This is the modern CMake style — all properties are bound to the target,
# avoiding intermediate variables like set(SOURCES ...).
target_sources(os.elf PRIVATE
    source/start.S
    source/os.c
)

target_include_directories(os.elf PRIVATE source)

# Use a linker script (os.lds) instead of the raw -Ttext=0x7c00 flag.
# A linker script gives fine-grained control over memory layout:
# section ordering, alignment, entry point, discarding unused sections,
# and later adding boot signature (0x55AA) or boundary symbols.
set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/os.lds)

# "LINKER:" prefix is required for flags meant for ld, not gcc.
# target_link_options passes flags through the compiler driver (gcc),
# where -m means architecture (e.g. -m32), NOT emulation mode.
# "LINKER:-m,elf_i386" tells CMake to translate it to -Wl,-m,elf_i386,
# so ld receives -m elf_i386 (set output format to 32-bit ELF).
# -Ttext=0x7c00, it's a short term for the linker script 
target_link_options(os.elf PRIVATE
    "LINKER:-m,elf_i386"
    -T ${LINKER_SCRIPT}
    -nostdlib
)

set_target_properties(os.elf PROPERTIES
    LINKER_LANGUAGE C
    LINK_FLAGS "-nostdlib"
)

# ── Post-build: objcopy → os.bin ─────────────────────────────
# Convert ELF to flat binary. BIOS doesn't understand ELF format —
# it simply loads raw bytes from disk sector 0 to memory 0x7C00 and
# jumps there. objcopy strips all ELF headers/metadata, leaving only
# the raw machine code that the CPU can execute directly.
add_custom_command(TARGET os.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
        $<TARGET_FILE:os.elf>
        ${CMAKE_CURRENT_BINARY_DIR}/os.bin
    COMMENT "Generating flat binary: os.bin"
)

# ── Post-build: objdump → os_dis.txt ─────────────────────────
add_custom_command(TARGET os.elf POST_BUILD
    COMMAND ${CMAKE_OBJDUMP} -x -d -S
        $<TARGET_FILE:os.elf>
        > ${CMAKE_CURRENT_BINARY_DIR}/os_dis.txt
    COMMENT "Generating disassembly: os_dis.txt"
)

# ── Post-build: readelf → os_elf.txt ─────────────────────────
add_custom_command(TARGET os.elf POST_BUILD
    COMMAND ${CMAKE_READELF} -a
        $<TARGET_FILE:os.elf>
        > ${CMAKE_CURRENT_BINARY_DIR}/os_elf.txt
    COMMENT "Generating ELF info: os_elf.txt"
)

# ── Create blank disk image ──────────────────────────────────
# Generate a blank 10MB raw disk image filled with zeros.
# Size = 512 bytes/sector * 20160 sectors = 10,321,920 bytes
# Geometry: 20 cylinders * 16 heads * 63 sectors/track (standard CHS).
# Using OUTPUT-form add_custom_command so CMake only creates the image
# when it doesn't exist — avoids overwriting the boot sector on rebuilds.
#
# dd (data duplicator) is a block-level copy tool.
#   if=/dev/zero  — input file: Unix special device that always reads 0x00
#   of=disk.img   — output file: the target disk image
#   bs=512        — block size: 512 bytes per read/write (= 1 disk sector)
#   count=20160   — number of blocks to copy
#
# Other useful dd parameters (for future reference):
#   seek=N   — skip N blocks in the output before writing (unit: bs)
#   skip=N   — skip N blocks in the input before reading  (unit: bs)
#   conv=X   — conversion (short for "conversion") options:
#     notrunc  — no truncate: don't truncate the output file
#     noerror  — continue on read errors
#     sync     — pad each input block to bs size with zeros
set(DISK_IMAGE ${CMAKE_CURRENT_BINARY_DIR}/disk.img)

add_custom_command(
    OUTPUT ${DISK_IMAGE}
    COMMAND dd if=/dev/zero of=${DISK_IMAGE} bs=512 count=20160
    COMMENT "Creating blank disk image: disk.img (10MB)"
)

# ── Post-build: dd → write boot sector to disk image ─────────
# Write os.bin to the beginning of disk.img (sector 0).
# dd writes from offset 0 by default. Use seek=N to write at a
# different sector (e.g. bs=512 seek=1 writes starting at sector 1).
# conv=notrunc (no truncate) prevents dd from truncating disk.img
# to the size of os.bin — only the first N bytes are overwritten,
# the rest of the 10MB image remains intact.
add_custom_command(TARGET os.elf POST_BUILD
    COMMAND dd if=${CMAKE_CURRENT_BINARY_DIR}/os.bin
        of=${DISK_IMAGE}
        conv=notrunc
    COMMENT "Writing os.bin to disk image"
    DEPENDS ${DISK_IMAGE}
)

# Create a custom target for disk.img and make os.elf depend on it,
# ensuring the blank disk image exists before the post-build dd step.
add_custom_target(disk_image DEPENDS ${DISK_IMAGE})
add_dependencies(os.elf disk_image)
