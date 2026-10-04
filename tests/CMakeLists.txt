add_executable(core_tests core_tests.cpp)
target_link_libraries(core_tests PRIVATE unnamed_core)
add_test(NAME core_tests COMMAND core_tests)

add_executable(drives_tests drives_tests.cpp)
target_link_libraries(drives_tests PRIVATE unnamed_core)
add_test(NAME drives_tests COMMAND drives_tests)

if(UNNAMED_WITH_FATX)
    add_executable(fatx_tests fatx_tests.cpp)
    target_link_libraries(fatx_tests PRIVATE unnamed_core)
    if(TARGET fatx)
        # images are made and checked with the FATX fork's own tool
        add_dependencies(fatx_tests fatx)
        target_compile_definitions(fatx_tests PRIVATE FATX_TOOL="$<TARGET_FILE:fatx>")
    endif()
    add_test(NAME fatx_tests COMMAND fatx_tests)
    set_tests_properties(fatx_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
endif()
