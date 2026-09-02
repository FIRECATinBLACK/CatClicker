cmake_minimum_required(VERSION 3.24)

project(CatClicker VERSION 0.1.0 LANGUAGES C CXX)

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE CATCLICKER_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT CATCLICKER_GIT_COMMIT)
    set(CATCLICKER_GIT_COMMIT "unknown")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(CTest)
include(GNUInstallDirs)

file(RELATIVE_PATH CATCLICKER_PERMISSION_HELPER_RELATIVE_PATH
    "/${CMAKE_INSTALL_BINDIR}"
    "/${CMAKE_INSTALL_LIBEXECDIR}/catclicker/setup-input-permissions.sh")

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick DBus Test)
find_package(PkgConfig QUIET)

set(CATCLICKER_HAS_PIPEWIRE OFF)
set(CATCLICKER_HAS_SPA OFF)
set(CATCLICKER_HAS_LIBEI OFF)
set(CATCLICKER_HAS_LIBEVDEV OFF)
set(CATCLICKER_HAS_COSMIC_CURSOR_PROTOCOL OFF)

if(PkgConfig_FOUND)
    pkg_check_modules(PIPEWIRE QUIET libpipewire-0.3)
    pkg_check_modules(SPA QUIET libspa-0.2)
    pkg_check_modules(LIBEI QUIET libei-1.0)
    pkg_check_modules(LIBEVDEV QUIET libevdev)
    pkg_check_modules(WAYLAND_CLIENT QUIET wayland-client)

    if(PIPEWIRE_FOUND)
        set(CATCLICKER_HAS_PIPEWIRE ON)
    endif()
    if(SPA_FOUND)
        set(CATCLICKER_HAS_SPA ON)
    endif()
    if(LIBEI_FOUND)
        set(CATCLICKER_HAS_LIBEI ON)
    endif()
    if(LIBEVDEV_FOUND)
        set(CATCLICKER_HAS_LIBEVDEV ON)
    endif()
endif()

find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner)
set(WAYLAND_PROTOCOL_DATA_CANDIDATES
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wayland-protocols"
    /usr/share/wayland-protocols
    /usr/local/share/wayland-protocols
)
find_path(EXT_IMAGE_COPY_PROTOCOL_DIR
    NAMES staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml
    PATHS ${WAYLAND_PROTOCOL_DATA_CANDIDATES}
    NO_DEFAULT_PATH)
find_path(EXT_IMAGE_SOURCE_PROTOCOL_DIR
    NAMES staging/ext-image-capture-source/ext-image-capture-source-v1.xml
    PATHS ${WAYLAND_PROTOCOL_DATA_CANDIDATES}
    NO_DEFAULT_PATH)
find_path(EXT_FOREIGN_TOPLEVEL_PROTOCOL_DIR
    NAMES staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml
    PATHS ${WAYLAND_PROTOCOL_DATA_CANDIDATES}
    NO_DEFAULT_PATH)

set(CATCLICKER_PROTOCOL_GENERATED_SOURCES)
if(WAYLAND_CLIENT_FOUND AND WAYLAND_SCANNER_EXECUTABLE
   AND EXT_IMAGE_COPY_PROTOCOL_DIR AND EXT_IMAGE_SOURCE_PROTOCOL_DIR
   AND EXT_FOREIGN_TOPLEVEL_PROTOCOL_DIR)
    set(CATCLICKER_HAS_COSMIC_CURSOR_PROTOCOL ON)
    set(PROTOCOL_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    foreach(PROTOCOL_NAME IN ITEMS ext-image-copy-capture-v1 ext-image-capture-source-v1 ext-foreign-toplevel-list-v1)
        if(PROTOCOL_NAME STREQUAL "ext-image-copy-capture-v1")
            set(PROTOCOL_XML "${EXT_IMAGE_COPY_PROTOCOL_DIR}/staging/ext-image-copy-capture/${PROTOCOL_NAME}.xml")
        elseif(PROTOCOL_NAME STREQUAL "ext-image-capture-source-v1")
            set(PROTOCOL_XML "${EXT_IMAGE_SOURCE_PROTOCOL_DIR}/staging/ext-image-capture-source/${PROTOCOL_NAME}.xml")
        else()
            set(PROTOCOL_XML "${EXT_FOREIGN_TOPLEVEL_PROTOCOL_DIR}/staging/ext-foreign-toplevel-list/${PROTOCOL_NAME}.xml")
        endif()
        set(PROTOCOL_HEADER "${PROTOCOL_GENERATED_DIR}/${PROTOCOL_NAME}-client-protocol.h")
        set(PROTOCOL_CODE "${PROTOCOL_GENERATED_DIR}/${PROTOCOL_NAME}-protocol.c")
        add_custom_command(OUTPUT "${PROTOCOL_HEADER}"
            COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header "${PROTOCOL_XML}" "${PROTOCOL_HEADER}"
            DEPENDS "${PROTOCOL_XML}" VERBATIM)
        add_custom_command(OUTPUT "${PROTOCOL_CODE}"
            COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code "${PROTOCOL_XML}" "${PROTOCOL_CODE}"
            DEPENDS "${PROTOCOL_XML}" "${PROTOCOL_HEADER}" VERBATIM)
        list(APPEND CATCLICKER_PROTOCOL_GENERATED_SOURCES "${PROTOCOL_CODE}")
    endforeach()
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config/BuildConfig.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/BuildConfig.h
)

qt_standard_project_setup(REQUIRES 6.4)

qt_add_executable(CatClicker
    src/main.cpp
    src/app/ApplicationController.cpp
    src/app/PlaybackLoopController.cpp
    src/macro/Macro.cpp
    src/macro/MacroEvent.cpp
    src/macro/MacroRecorder.cpp
    src/macro/MacroPlayer.cpp
    src/input/InputCaptureBackend.h
    src/input/InputCaptureBackend.cpp
    src/input/FileChooserPortal.cpp
    src/input/PortalController.cpp
    src/input/CursorTracker.cpp
    src/input/CosmicCursorPositionProvider.cpp
    ${CATCLICKER_PROTOCOL_GENERATED_SOURCES}
    src/input/EvdevDeviceInspector.cpp
    src/input/EvdevCaptureBackend.cpp
    src/input/GlobalInputMonitor.cpp
    src/input/QtFocusedCaptureBackend.cpp
    src/input/InputSenderBackend.h
    src/input/InputSenderBackend.cpp
    src/input/EiInputSender.cpp
    src/input/PlaybackBackendSelector.cpp
    src/input/UinputIo.cpp
    src/input/UinputInputSender.cpp
    src/input/VirtualDeviceIdentity.cpp
    src/hotkeys/GlobalShortcutManager.cpp
    src/persistence/MacroSerializer.cpp
    src/persistence/Settings.cpp
    src/diagnostics/Diagnostics.cpp
)

set_target_properties(CatClicker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(CatClicker
    URI CatClicker
    VERSION 1.0
    NO_CACHEGEN
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/components/ActionButton.qml
        qml/components/InfoCard.qml
        qml/components/ShortcutField.qml
        qml/components/StatusPill.qml
        qml/components/ToggleSwitch.qml
)

set(CATCLICKER_RUNTIME_ICON
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/hicolor/256x256/apps/catclicker.png")
set_source_files_properties("${CATCLICKER_RUNTIME_ICON}" PROPERTIES
    QT_RESOURCE_ALIAS catclicker.png)
qt_add_resources(CatClicker "catclicker_branding"
    PREFIX "/CatClicker/branding"
    FILES "${CATCLICKER_RUNTIME_ICON}"
)

set(CATCLICKER_GITHUB_MARK_DARK "${CMAKE_CURRENT_SOURCE_DIR}/assets/third_party/github-mark.svg")
set(CATCLICKER_GITHUB_MARK_LIGHT "${CMAKE_CURRENT_SOURCE_DIR}/assets/third_party/github-mark-light.svg")
set_source_files_properties("${CATCLICKER_GITHUB_MARK_DARK}" PROPERTIES
    QT_RESOURCE_ALIAS github-mark-dark.svg)
set_source_files_properties("${CATCLICKER_GITHUB_MARK_LIGHT}" PROPERTIES
    QT_RESOURCE_ALIAS github-mark-light.svg)
qt_add_resources(CatClicker "catclicker_github_mark"
    PREFIX "/CatClicker/branding"
    FILES "${CATCLICKER_GITHUB_MARK_DARK}" "${CATCLICKER_GITHUB_MARK_LIGHT}"
)

add_custom_target(audit-no-network
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AuditNoNetwork.cmake
    COMMENT "Auditing source for accidental runtime networking"
)

target_include_directories(CatClicker
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(CatClicker
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::DBus
)

if(PIPEWIRE_FOUND)
    target_include_directories(CatClicker PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
    target_link_libraries(CatClicker PRIVATE ${PIPEWIRE_LINK_LIBRARIES})
endif()

if(SPA_FOUND)
    target_include_directories(CatClicker PRIVATE ${SPA_INCLUDE_DIRS})
    target_link_libraries(CatClicker PRIVATE ${SPA_LINK_LIBRARIES})
endif()

if(LIBEI_FOUND)
    target_include_directories(CatClicker PRIVATE ${LIBEI_INCLUDE_DIRS})
    target_link_libraries(CatClicker PRIVATE ${LIBEI_LINK_LIBRARIES})
endif()

if(LIBEVDEV_FOUND)
    target_include_directories(CatClicker PRIVATE ${LIBEVDEV_INCLUDE_DIRS})
    target_link_libraries(CatClicker PRIVATE ${LIBEVDEV_LINK_LIBRARIES})
endif()

if(CATCLICKER_HAS_COSMIC_CURSOR_PROTOCOL)
    target_include_directories(CatClicker PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIRS})
    target_link_libraries(CatClicker PRIVATE ${WAYLAND_CLIENT_LINK_LIBRARIES})
endif()

install(TARGETS CatClicker
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES packaging/linux/catclicker.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(PROGRAMS scripts/setup-input-permissions.sh
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/catclicker
)

foreach(CATCLICKER_ICON_SIZE IN ITEMS 16 24 32 48 64 96 128 256 512)
    install(FILES
        "assets/icons/hicolor/${CATCLICKER_ICON_SIZE}x${CATCLICKER_ICON_SIZE}/apps/catclicker.png"
        DESTINATION
        "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${CATCLICKER_ICON_SIZE}x${CATCLICKER_ICON_SIZE}/apps"
    )
endforeach()

if(BUILD_TESTING)
    qt_add_executable(CatClickerTests
        tests/MacroCoreTests.cpp
        src/hotkeys/GlobalShortcutManager.cpp
        src/macro/Macro.cpp
        src/macro/MacroEvent.cpp
        src/macro/MacroRecorder.cpp
        src/macro/MacroPlayer.cpp
        src/app/PlaybackLoopController.cpp
        src/input/InputSenderBackend.cpp
        src/input/InputCaptureBackend.cpp
        src/input/EvdevDeviceInspector.cpp
        src/input/EvdevCaptureBackend.cpp
        src/input/CosmicCursorPositionProvider.cpp
        ${CATCLICKER_PROTOCOL_GENERATED_SOURCES}
        src/input/GlobalInputMonitor.cpp
        src/input/FileChooserPortal.cpp
        src/input/PlaybackBackendSelector.cpp
        src/input/PortalController.cpp
        src/input/QtFocusedCaptureBackend.cpp
        src/input/UinputIo.cpp
        src/input/UinputInputSender.cpp
        src/input/VirtualDeviceIdentity.cpp
        src/persistence/MacroSerializer.cpp
    )

    set_target_properties(CatClickerTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    target_include_directories(CatClickerTests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_BINARY_DIR}/generated
    )

    target_link_libraries(CatClickerTests
        PRIVATE
            Qt6::Core
            Qt6::DBus
            Qt6::Gui
            Qt6::Test
    )

    if(PIPEWIRE_FOUND)
        target_include_directories(CatClickerTests PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
        target_link_libraries(CatClickerTests PRIVATE ${PIPEWIRE_LINK_LIBRARIES})
    endif()

    if(SPA_FOUND)
        target_include_directories(CatClickerTests PRIVATE ${SPA_INCLUDE_DIRS})
        target_link_libraries(CatClickerTests PRIVATE ${SPA_LINK_LIBRARIES})
    endif()

    if(CATCLICKER_HAS_COSMIC_CURSOR_PROTOCOL)
        target_include_directories(CatClickerTests PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIRS})
        target_link_libraries(CatClickerTests PRIVATE ${WAYLAND_CLIENT_LINK_LIBRARIES})
    endif()

    add_test(NAME CatClickerTests COMMAND $<TARGET_FILE:CatClickerTests>)
    set_tests_properties(CatClickerTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_test(NAME SafeDiagnosticsAudit
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AuditSafeDiagnostics.cmake)
    add_test(NAME NoRuntimeNetworkAudit
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AuditNoNetwork.cmake)
endif()
