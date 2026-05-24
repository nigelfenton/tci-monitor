cmake_minimum_required(VERSION 3.20)
project(TciMonitor
    VERSION 0.3.0
    DESCRIPTION "Diagnostic monitor for the TCI WebSocket protocol"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS
    Core
    Gui
    Network
    Widgets
    WebSockets
)

# ── Git build-info ────────────────────────────────────────────────────────
# Capture commit hash / date / branch / dirty flag at configure time and
# bake them into BuildInfo.cpp so every saved calibration CSV (and any
# future "About" surface) can identify exactly which binary produced it.
# Re-run configure to refresh the hash; the compile date in BuildInfo
# catches cases where you rebuild without reconfiguring.
find_package(Git QUIET)
set(TCIMON_GIT_HASH "unknown")
set(TCIMON_GIT_DATE "unknown")
set(TCIMON_GIT_BRANCH "unknown")
set(TCIMON_GIT_DIRTY "unknown")
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_SOURCE_DIR} rev-parse --short HEAD
        OUTPUT_VARIABLE _tcimon_git_hash
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(_tcimon_git_hash)
        set(TCIMON_GIT_HASH "${_tcimon_git_hash}")
    endif()
    execute_process(
        COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_SOURCE_DIR} log -1 --format=%cI
        OUTPUT_VARIABLE _tcimon_git_date
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(_tcimon_git_date)
        set(TCIMON_GIT_DATE "${_tcimon_git_date}")
    endif()
    execute_process(
        COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_SOURCE_DIR} rev-parse --abbrev-ref HEAD
        OUTPUT_VARIABLE _tcimon_git_branch
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(_tcimon_git_branch)
        set(TCIMON_GIT_BRANCH "${_tcimon_git_branch}")
    endif()
    execute_process(
        COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_SOURCE_DIR} status --porcelain
        OUTPUT_VARIABLE _tcimon_git_status
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(_tcimon_git_status)
        set(TCIMON_GIT_DIRTY "yes")
    else()
        set(TCIMON_GIT_DIRTY "no")
    endif()
endif()
configure_file(
    ${CMAKE_SOURCE_DIR}/src/BuildInfo.cpp.in
    ${CMAKE_BINARY_DIR}/generated/BuildInfo.cpp
    @ONLY)
message(STATUS "TCI Monitor build: ${TCIMON_GIT_HASH} (${TCIMON_GIT_BRANCH})"
        " dirty=${TCIMON_GIT_DIRTY} committed=${TCIMON_GIT_DATE}")

set(TCIMON_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/TciClient.cpp
    src/TciClient.h
    src/InspectorPanel.cpp
    src/InspectorPanel.h
    src/ConsolePanel.cpp
    src/ConsolePanel.h
    src/ComparePanel.cpp
    src/ComparePanel.h
    src/ReplayPanel.cpp
    src/ReplayPanel.h
    src/SwrPlot.cpp
    src/SwrPlot.h
    src/CalPlot.cpp
    src/CalPlot.h
    src/CalibrationPanel.cpp
    src/CalibrationPanel.h
    src/BuildInfo.h
    ${CMAKE_BINARY_DIR}/generated/BuildInfo.cpp
    src/TciCommands.cpp
    src/TciCommands.h
    src/CommandDescriptionDialog.cpp
    src/CommandDescriptionDialog.h
    src/CommandsReferenceDialog.cpp
    src/CommandsReferenceDialog.h
    src/MdnsBrowser.cpp
    src/MdnsBrowser.h
    src/DiscoveryDialog.cpp
    src/DiscoveryDialog.h
    src/SignalPanel.cpp
    src/SignalPanel.h
    src/WaterfallIdPanel.cpp
    src/WaterfallIdPanel.h
)

if(WIN32)
    add_executable(TciMonitor WIN32 ${TCIMON_SOURCES})
elseif(APPLE)
    add_executable(TciMonitor MACOSX_BUNDLE ${TCIMON_SOURCES})
else()
    add_executable(TciMonitor ${TCIMON_SOURCES})
endif()

target_include_directories(TciMonitor PRIVATE src)
target_link_libraries(TciMonitor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
    Qt6::WebSockets
)

if(MSVC)
    target_compile_options(TciMonitor PRIVATE /W3 /permissive-)
    target_compile_definitions(TciMonitor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(TciMonitor PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Windows: copy Qt DLLs + plugins next to the binary so it runs by
# double-click without Qt on PATH.
if(WIN32)
    get_target_property(_qt6_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt6_bin "${_qt6_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${_qt6_bin}")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET TciMonitor POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}"
                    $<IF:$<CONFIG:Debug>,--debug,--release>
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    --compiler-runtime
                    "$<TARGET_FILE:TciMonitor>"
            COMMENT "Running windeployqt to bundle Qt runtime DLLs"
            VERBATIM
        )
    endif()
endif()

if(APPLE)
    set_target_properties(TciMonitor PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME           "TCI Monitor"
        MACOSX_BUNDLE_GUI_IDENTIFIER        "com.g0jkn.tcimonitor"
        MACOSX_BUNDLE_BUNDLE_VERSION        "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING  "${PROJECT_VERSION}"
    )
endif()
