# Test-only integration: compile the unmodified production CPU and scheduler.
option(PCBOX_ARCHTEST "Build the imported Pentium III architectural suite" OFF)
if(ARCH STREQUAL "x86_64" AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(timing_lookup "${CMAKE_CURRENT_BINARY_DIR}/pentium3_timing_lookup.h")
    add_custom_command(OUTPUT "${timing_lookup}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/prepare_timing_lookup.py"
            "${CPU_SOURCE_ROOT}/src/cpu/codegen_timing_p6.c" "${timing_lookup}"
        DEPENDS prepare_timing_lookup.py "${CPU_SOURCE_ROOT}/src/cpu/codegen_timing_p6.c"
        VERBATIM)
    add_executable(pentium3_timing_test pentium3_timing_test.c "${timing_lookup}"
        "${CPU_SOURCE_ROOT}/src/cpu/codegen_timing_common.c")
    target_compile_definitions(pentium3_timing_test PRIVATE USE_DYNAREC USE_NEW_DYNAREC
        P3_TIMING_BASELINE="${CMAKE_CURRENT_SOURCE_DIR}/pentium3_timing_baseline.csv")
    target_include_directories(pentium3_timing_test PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}" "${CPU_SOURCE_ROOT}/src/include"
        "${CPU_SOURCE_ROOT}/src/cpu" "${CPU_SOURCE_ROOT}/src/codegen_new")
    if(BUILD_TESTING)
        add_test(NAME pentium3_timing_test COMMAND pentium3_timing_test
            --csv "${CMAKE_CURRENT_BINARY_DIR}/pentium3-timing.csv"
            --reference-csv "${CMAKE_CURRENT_BINARY_DIR}/pentium3-timing-reference.csv")
        add_test(NAME pentium3_opcode_coverage COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/audit_pentium3_opcodes.py"
            --executable $<TARGET_FILE:pentium3_timing_test>)
        set_tests_properties(pentium3_timing_test pentium3_opcode_coverage
            PROPERTIES TIMEOUT 120 LABELS "cpu;guest-timing")
    endif()
endif()
if(PCBOX_ARCHTEST)
    add_subdirectory(archtest)
endif()
