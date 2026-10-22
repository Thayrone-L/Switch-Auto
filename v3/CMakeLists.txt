cmake_minimum_required(VERSION 3.20)

project(switch_auto_v3 VERSION 0.1.0 LANGUAGES CXX)

if(NINTENDO_SWITCH OR CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    add_executable(switch-auto-v3
        apps/switch/main.cpp
    )
    target_compile_features(switch-auto-v3 PRIVATE cxx_std_17)
    target_compile_options(switch-auto-v3 PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    nx_create_nro(switch-auto-v3-nro
        TARGET switch-auto-v3
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/switch-auto-v3.nro"
    )
else()
    include(CTest)

    function(sa_add_test name)
        add_executable(${name} ${ARGN})
        target_compile_features(${name} PRIVATE cxx_std_17)
        target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        if(MSVC)
            target_compile_options(${name} PRIVATE /W4 /WX /permissive- /utf-8)
        else()
            target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    sa_add_test(v3-smoke tests/smoke_main.cpp)
    sa_add_test(v3-base tests/base/base_contracts_test.cpp)
    sa_add_test(v3-contracts tests/contracts/runtime_contracts_test.cpp)
    sa_add_test(v3-harness tests/support/m0_harness_test.cpp)
endif()
