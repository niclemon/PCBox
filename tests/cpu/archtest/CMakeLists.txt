# Compile the real CPU, memory, decoder, exception machinery and JIT without
# linking a GUI or requiring a BIOS/machine configuration.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
enable_language(CXX)
if(NOT ARCH STREQUAL "x86_64" OR NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "The architectural fixture requires an x86-64 GNU/Clang host")
endif()
find_program(ARCHTEST_CLANG NAMES clang HINTS C:/msys64/clang64/bin)
find_program(ARCHTEST_LLD NAMES ld.lld HINTS C:/msys64/clang64/bin)
find_program(ARCHTEST_AS NAMES as HINTS C:/msys64/mingw64/bin)
find_program(ARCHTEST_OBJCOPY NAMES objcopy HINTS C:/msys64/mingw64/bin)
foreach(tool CLANG LLD AS OBJCOPY)
    if(NOT ARCHTEST_${tool})
        message(FATAL_ERROR "Architectural guest requires ARCHTEST_${tool}; see archtest/README.md")
    endif()
endforeach()

file(GLOB_RECURSE guest_sources CONFIGURE_DEPENDS upstream/src/* upstream/include/*)
set(guest_binary "${CMAKE_CURRENT_BINARY_DIR}/archtest.bin")
add_custom_command(OUTPUT "${guest_binary}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build_guest.py"
        --clang "${ARCHTEST_CLANG}" --linker "${ARCHTEST_LLD}"
        --assembler "${ARCHTEST_AS}" --objcopy "${ARCHTEST_OBJCOPY}"
        --output "${guest_binary}"
    DEPENDS ${guest_sources} guest.c guest.ld build_guest.py upstream/tools/elf_calls.py
    VERBATIM)
add_custom_target(pentium3_archtest_guest DEPENDS "${guest_binary}")

set(core "${CPU_SOURCE_ROOT}/src")
set(cpu_sources cpu.c cpu_table.c fpu.c x86.c 386.c 386_common.c
    386_dynarec.c 386_dynarec_ops.c x86_ops_mmx.c x86seg_common.c
    x86seg.c x86seg_2386.c x87.c x87_timings.c
    codegen_timing_486.c codegen_timing_686.c codegen_timing_common.c
    codegen_timing_k6.c codegen_timing_pentium.c codegen_timing_p6.c
    codegen_timing_winchip.c codegen_timing_winchip2.c)
list(TRANSFORM cpu_sources PREPEND "${core}/cpu/")
file(GLOB jit_sources CONFIGURE_DEPENDS "${core}/codegen_new/*.c")
list(FILTER jit_sources EXCLUDE REGEX "codegen_backend_(arm64|loongarch64)")
# Reuse the production source list without modifying emulator build files.
if(NOT TARGET softfloat3e)
    add_subdirectory("${core}/cpu/softfloat3e" "${CMAKE_CURRENT_BINARY_DIR}/softfloat3e")
    target_include_directories(softfloat3e PRIVATE "${core}/include")
endif()
add_executable(pentium3_archtest runner.c platform.c ${cpu_sources} ${jit_sources}
    $<TARGET_OBJECTS:softfloat3e> "${core}/mem/mem.c" "${core}/mem/mmu_2386.c"
    "${core}/mem/smram.c")
set_target_properties(pentium3_archtest PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_dependencies(pentium3_archtest pentium3_archtest_guest)
target_compile_definitions(pentium3_archtest PRIVATE USE_DYNAREC USE_NEW_DYNAREC
    USE_DEBUG_REGS_486 ARCHTEST_GUEST="${guest_binary}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/86box")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/86box/version.h" CONTENT
    "#define EMU_NAME \"PCBox architectural tests\"\n#define EMU_VERSION \"test\"\n")
target_include_directories(pentium3_archtest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/include"
    "${core}/include" "${core}/cpu" "${core}/codegen_new")
target_compile_options(pentium3_archtest PRIVATE -fno-strict-aliasing -ffunction-sections -fdata-sections
    $<$<PLATFORM_ID:Windows>:-fno-asynchronous-unwind-tables>)
target_link_options(pentium3_archtest PRIVATE
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
    $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--gc-sections>)
if(NOT WIN32)
    target_link_libraries(pentium3_archtest PRIVATE m)
endif()
if(BUILD_TESTING)
    add_test(NAME pentium3_archtest_sources COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/audit_sources.py")
    set_tests_properties(pentium3_archtest_sources PROPERTIES TIMEOUT 30 LABELS "cpu;architectural")
    foreach(engine interpreter dynarec)
        foreach(fpu softfloat native)
            add_test(NAME pentium3_archtest_${engine}_${fpu}
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verify_run.py"
                    --executable $<TARGET_FILE:pentium3_archtest> --engine ${engine} --fpu ${fpu}
                    --output-dir "${CMAKE_CURRENT_BINARY_DIR}/results")
            set_tests_properties(pentium3_archtest_${engine}_${fpu}
                PROPERTIES TIMEOUT 200 LABELS "cpu;architectural")
        endforeach()
    endforeach()
    add_test(NAME pentium3_archtest_guest_timing
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verify_run.py"
            --executable $<TARGET_FILE:pentium3_archtest> --engine dynarec --timing
            --output-dir "${CMAKE_CURRENT_BINARY_DIR}/results")
    set_tests_properties(pentium3_archtest_guest_timing
        PROPERTIES TIMEOUT 200 LABELS "cpu;architectural;guest-timing")
    add_test(NAME pentium3_archtest_runner_guards
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_runner.py"
            --executable $<TARGET_FILE:pentium3_archtest> --guest "${guest_binary}"
            --output-dir "${CMAKE_CURRENT_BINARY_DIR}/negative-tests")
    set_tests_properties(pentium3_archtest_runner_guards
        PROPERTIES TIMEOUT 200 LABELS "cpu;architectural")
endif()
