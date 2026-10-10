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

    add_executable(switch-auto-v3-smoke
        tests/smoke_main.cpp
    )
    target_compile_features(switch-auto-v3-smoke PRIVATE cxx_std_17)

    add_test(NAME switch-auto-v3.smoke COMMAND switch-auto-v3-smoke)

    add_executable(switch-auto-v3-base-contracts
        tests/base/base_contracts_test.cpp
    )
    target_compile_features(switch-auto-v3-base-contracts PRIVATE cxx_std_17)
    target_include_directories(switch-auto-v3-base-contracts PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(MSVC)
        target_compile_options(switch-auto-v3-base-contracts PRIVATE
            /W4
            /WX
        )
    else()
        target_compile_options(switch-auto-v3-base-contracts PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -fno-exceptions
        )
    endif()

    add_test(
        NAME switch-auto-v3.base-contracts
        COMMAND switch-auto-v3-base-contracts
    )
endif()
