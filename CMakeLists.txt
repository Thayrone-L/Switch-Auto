cmake_minimum_required(VERSION 3.16)

set(BOREALIS_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/extern/borealis/library")
include("${BOREALIS_LIBRARY}/cmake/commonOption.cmake")
include("${BOREALIS_LIBRARY}/cmake/toolchain.cmake")

project(switch_auto VERSION 0.1.0 LANGUAGES C CXX)

set(PACKAGE_NAME "dev.switchauto.core")
set(PROJECT_AUTHOR "Switch Auto")
set(PROJECT_TITLEID "05A570A000000000")
set(PROJECT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/extern/borealis/resources/img/demo_icon.jpg")
if(PLATFORM_DESKTOP)
    set(BRLS_RESOURCES_DIR ".")
else()
    set(BRLS_RESOURCES_DIR "${CMAKE_BINARY_DIR}")
endif()

add_subdirectory(extern/borealis/library)

set(SWITCH_AUTO_SOURCES
    apps/ui/source/connection_view.cpp
    apps/ui/source/dashboard_view.cpp
    apps/ui/source/main.cpp
    apps/ui/source/qr_code_view.cpp
    core/source/companion.cpp
    core/source/crypto.cpp
    extern/qrcodegen/qrcodegen.cpp
)

if(PLATFORM_SWITCH)
    list(APPEND SWITCH_AUTO_SOURCES
        apps/platform/switch/switch_tcp_companion_service.cpp
        apps/platform/switch/switch_device_identity.cpp
        extern/borealis/library/lib/platforms/switch/switch_wrapper.c
    )
endif()

program_target(switch-auto "${SWITCH_AUTO_SOURCES}")
set_target_properties(switch-auto PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(switch-auto PRIVATE
    apps/ui/include
    core/include
    extern/qrcodegen
)
if(PLATFORM_SWITCH)
    target_include_directories(switch-auto PRIVATE apps/platform/switch)
endif()
target_compile_options(switch-auto PRIVATE
    -Wall
    -Wextra
    -ffunction-sections
    -fdata-sections
    ${BRLS_PLATFORM_OPTION}
)
target_link_libraries(switch-auto PRIVATE borealis ${APP_PLATFORM_LIB})

add_custom_target(switch-auto-resources
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${CMAKE_BINARY_DIR}/resources"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/extern/borealis/resources"
            "${CMAKE_BINARY_DIR}/resources"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "${CMAKE_BINARY_DIR}/resources"
    COMMENT "Staging Borealis and Switch Auto resources"
)
add_dependencies(switch-auto switch-auto-resources)

if(PLATFORM_DESKTOP AND WIN32 AND MINGW)
    target_link_options(switch-auto PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

if(PLATFORM_SWITCH)
    add_custom_target(switch-auto.nro DEPENDS switch-auto
        COMMAND ${NX_NACPTOOL_EXE} --create "Switch Auto" "${PROJECT_AUTHOR}"
                "${PROJECT_VERSION}" switch-auto.nacp --titleid=${PROJECT_TITLEID}
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${CMAKE_BINARY_DIR}/resources/font"
        COMMAND ${NX_ELF2NRO_EXE} switch-auto.elf switch-auto.nro
                --icon=${PROJECT_ICON}
                --nacp=switch-auto.nacp
                --romfsdir=${CMAKE_BINARY_DIR}/resources
        COMMENT "Packaging Switch Auto NRO"
    )
endif()
