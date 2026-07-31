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
    apps/ui/source/android_auto_video_view.cpp
    apps/ui/source/connection_view.cpp
    apps/ui/source/dashboard_view.cpp
    apps/ui/source/localization_adapter.cpp
    apps/ui/source/main.cpp
    apps/ui/source/qr_code_view.cpp
    core/source/android_auto.cpp
    core/source/android_auto_protocol.cpp
    core/source/companion.cpp
    core/source/crypto.cpp
    core/source/localization.cpp
    core/source/permissions.cpp
    core/source/widget_layout.cpp
    extern/qrcodegen/qrcodegen.cpp
)

if(PLATFORM_SWITCH)
    list(APPEND SWITCH_AUTO_SOURCES
        apps/platform/switch/switch_android_auto_host.cpp
        apps/platform/switch/switch_h264_decoder.cpp
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
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SWITCH_FFMPEG REQUIRED IMPORTED_TARGET
        libavcodec libavutil libswscale)
    target_include_directories(switch-auto PRIVATE apps/platform/switch)
    target_link_libraries(switch-auto PRIVATE PkgConfig::SWITCH_FFMPEG)

    set(SWITCH_AUTO_V2_SOURCES
        v2/apps/switch/source/main.cpp
        v2/platform/switch/source/nvtegra_probe.cpp
    )
    program_target(switch-auto-v2 "${SWITCH_AUTO_V2_SOURCES}")
    set_target_properties(switch-auto-v2 PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    target_include_directories(switch-auto-v2 PRIVATE
        v2/core/include
        v2/platform/switch/include
    )
    target_compile_options(switch-auto-v2 PRIVATE
        -Wall
        -Wextra
        -ffunction-sections
        -fdata-sections
        ${BRLS_PLATFORM_OPTION}
    )
    target_link_libraries(switch-auto-v2 PRIVATE
        borealis
        ${APP_PLATFORM_LIB}
        PkgConfig::SWITCH_FFMPEG
    )
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

    add_custom_target(switch-auto-v2.nro DEPENDS switch-auto-v2
        COMMAND ${NX_NACPTOOL_EXE} --create "Switch Auto V2" "${PROJECT_AUTHOR}"
                "${PROJECT_VERSION}" switch-auto-v2.nacp --titleid=${PROJECT_TITLEID}
        COMMAND ${NX_ELF2NRO_EXE} switch-auto-v2.elf switch-auto-v2.nro
                --icon=${PROJECT_ICON}
                --nacp=switch-auto-v2.nacp
                --romfsdir=${CMAKE_BINARY_DIR}/resources
        COMMENT "Packaging Switch Auto V2 NVDEC probe"
    )
    add_dependencies(switch-auto-v2 switch-auto-resources)
endif()
