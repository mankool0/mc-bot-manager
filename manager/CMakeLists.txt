cmake_minimum_required(VERSION 3.16)

project(mc-bot-manager VERSION 1.8.2 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Treat warnings as errors
if(MSVC)
    set(PROJECT_WARNING_FLAGS /W4)
else()
    set(PROJECT_WARNING_FLAGS -Wall -Wextra -Werror)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Protobuf Concurrent WebEngineWidgets)

include(FetchContent)

set(PYTHON_STANDALONE_VERSION "3.12.13")
set(PYTHON_STANDALONE_DATE "20260414")

if(NOT Python3_EXECUTABLE)
    if(UNIX AND NOT APPLE)
        set(PYTHON_STANDALONE_URL
            "https://github.com/astral-sh/python-build-standalone/releases/download/${PYTHON_STANDALONE_DATE}/cpython-${PYTHON_STANDALONE_VERSION}+${PYTHON_STANDALONE_DATE}-x86_64-unknown-linux-gnu-install_only.tar.gz")
        set(PYTHON_STANDALONE_HASH "SHA256=cdcf8724d46e4857f8db5ee9f4252dc2f5da34f7940294ec6b312389dd3f41e0")
    elseif(WIN32)
        set(PYTHON_STANDALONE_URL
            "https://github.com/astral-sh/python-build-standalone/releases/download/${PYTHON_STANDALONE_DATE}/cpython-${PYTHON_STANDALONE_VERSION}+${PYTHON_STANDALONE_DATE}-x86_64-pc-windows-msvc-install_only.tar.gz")
        set(PYTHON_STANDALONE_HASH "SHA256=c5a9e011e284c49c48106ca177342f3e3f64e95b4c6652d4a382cc7c9bb1cc46")
    endif()

    if(DEFINED PYTHON_STANDALONE_URL)
        FetchContent_Declare(
            python_standalone
            URL "${PYTHON_STANDALONE_URL}"
            URL_HASH "${PYTHON_STANDALONE_HASH}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(python_standalone)
        set(_py_root "${python_standalone_SOURCE_DIR}")
        find_program(_py_exe
            NAMES python3.12 python3 python
            PATHS "${_py_root}/bin"
            NO_DEFAULT_PATH
        )
        set(Python3_EXECUTABLE "${_py_exe}" CACHE FILEPATH "" FORCE)
        set(Python3_ROOT_DIR "${_py_root}" CACHE PATH "" FORCE)
    endif()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v3.0.1
)
set(PYBIND11_TEST OFF CACHE BOOL "Disable pybind11 tests" FORCE)
FetchContent_MakeAvailable(pybind11)

# For NBT file handling
set(NBT_BUILD_TESTS OFF CACHE BOOL "Disable libnbtplusplus tests" FORCE)
FetchContent_Declare(
    libnbtplusplus
    GIT_REPOSITORY https://github.com/PrismLauncher/libnbtplusplus.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(libnbtplusplus)

FetchContent_Declare(
    monaco_editor
    URL      https://registry.npmjs.org/monaco-editor/-/monaco-editor-0.55.1.tgz
    URL_HASH SHA256=eec3721fb6b1dc5a0bd1a73e38a5eb5d0c3791af684f7d2571efb90ad8634871
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(monaco_editor)

find_program(ESBUILD_EXECUTABLE esbuild REQUIRED)

# On Windows, npm installs esbuild as a .cmd batch wrapper which cannot be
# executed directly by cmake's execute_process - it needs cmd /c.
if(WIN32)
    set(ESBUILD_CMD cmd /c "${ESBUILD_EXECUTABLE}")
else()
    set(ESBUILD_CMD "${ESBUILD_EXECUTABLE}")
endif()

if(EXISTS "${monaco_editor_SOURCE_DIR}/esm/vs/editor/editor.main.js")
    set(MONACO_ESM_ENTRY "${monaco_editor_SOURCE_DIR}/esm/vs/editor/editor.main.js")
else()
    set(MONACO_ESM_ENTRY "${monaco_editor_SOURCE_DIR}/package/esm/vs/editor/editor.main.js")
endif()

set(MONACO_BUNDLE_JS  "${CMAKE_CURRENT_BINARY_DIR}/monaco-bundle.js")
set(MONACO_BUNDLE_CSS "${CMAKE_CURRENT_BINARY_DIR}/monaco-bundle.css")
if(NOT EXISTS "${MONACO_BUNDLE_JS}")
    message(STATUS "Bundling Monaco Editor with esbuild...")
    execute_process(
        COMMAND ${ESBUILD_CMD}
            "${MONACO_ESM_ENTRY}"
            --bundle
            --format=iife
            --global-name=monaco
            --minify
            --loader:.ttf=dataurl
            --outfile=${MONACO_BUNDLE_JS}
        RESULT_VARIABLE ESBUILD_RESULT
        ERROR_VARIABLE ESBUILD_ERROR
    )
    if(NOT ESBUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "esbuild failed to bundle Monaco Editor:\n${ESBUILD_ERROR}")
    endif()
endif()

set(MONACO_QRC "${CMAKE_CURRENT_BINARY_DIR}/monaco_resources.qrc")
file(WRITE  "${MONACO_QRC}" "<RCC>\n  <qresource prefix=\"/\">\n")
file(APPEND "${MONACO_QRC}" "    <file alias=\"monaco-bundle.js\">${MONACO_BUNDLE_JS}</file>\n")
file(APPEND "${MONACO_QRC}" "    <file alias=\"monaco-bundle.css\">${MONACO_BUNDLE_CSS}</file>\n")
file(APPEND "${MONACO_QRC}" "  </qresource>\n</RCC>\n")

set(EDITOR_QRC "${CMAKE_CURRENT_BINARY_DIR}/editor_resources.qrc")
file(WRITE  "${EDITOR_QRC}" "<RCC>\n  <qresource prefix=\"/\">\n")
file(APPEND "${EDITOR_QRC}"
    "    <file alias=\"editor.html\">${CMAKE_CURRENT_SOURCE_DIR}/resources/editor.html</file>\n")
file(APPEND "${EDITOR_QRC}" "  </qresource>\n</RCC>\n")

# Auto-generate Python stub files from PythonModules.cpp and PythonAPI.h
set(STUBS_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/stubs")
set(STUB_MODULES bot baritone meteor world utils server)
set(STUB_OUTPUT_FILES)
foreach(stub ${STUB_MODULES})
    list(APPEND STUB_OUTPUT_FILES "${STUBS_BUILD_DIR}/${stub}.pyi")
endforeach()

add_custom_command(
    OUTPUT ${STUB_OUTPUT_FILES}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STUBS_BUILD_DIR}"
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_stubs.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripting/PythonModules.cpp"
            --header "${CMAKE_CURRENT_SOURCE_DIR}/scripting/PythonAPI.h"
            --output "${STUBS_BUILD_DIR}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/scripting/PythonModules.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripting/PythonAPI.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_stubs.py"
    COMMENT "Generating Python API stubs"
)

add_custom_target(generate_stubs DEPENDS ${STUB_OUTPUT_FILES})

set(STUBS_QRC "${CMAKE_CURRENT_BINARY_DIR}/stubs_resources.qrc")
file(WRITE  "${STUBS_QRC}" "<RCC>\n  <qresource prefix=\"/stubs\">\n")
foreach(stub ${STUB_MODULES})
    file(APPEND "${STUBS_QRC}"
        "    <file alias=\"${stub}.pyi\">${STUBS_BUILD_DIR}/${stub}.pyi</file>\n")
endforeach()
file(APPEND "${STUBS_QRC}" "  </qresource>\n</RCC>\n")

# Generate QtProtobuf files from proto directory
file(GLOB PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/*.proto
)

qt_add_protobuf(mc-bot-manager-proto
    PROTO_FILES ${PROTO_FILES}
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_compile_options(mc-bot-manager-proto PRIVATE ${PROJECT_WARNING_FLAGS})

set(PROJECT_MAIN_SOURCE "main.cpp")

file(GLOB_RECURSE PROJECT_SOURCES
    "ui/*.cpp"
    "ui/*.h"
    "ui/*.ui"
    "logging/*.cpp"
    "logging/*.h"
    "prism/*.cpp"
    "prism/*.h"
    "network/*.cpp"
    "network/*.h"
    "bot/*.cpp"
    "bot/*.h"
    "world/*.cpp"
    "world/*.h"
    "scripting/*.cpp"
    "scripting/*.h"
    "saving/*.cpp"
    "saving/*.h"
    "crafting/*.cpp"
    "crafting/*.h"
)

list(REMOVE_ITEM PROJECT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/prism/PrismHook.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/prism/PrismHookLoader.cpp"
)

# Windows resource file for application icon
if(WIN32)
    set(app_icon_resource_windows "${CMAKE_CURRENT_SOURCE_DIR}/mc-bot-manager.rc")
endif()

qt_add_executable(mc-bot-manager
    MANUAL_FINALIZATION
    ${PROJECT_MAIN_SOURCE}
    ${PROJECT_SOURCES}
    ${app_icon_resource_windows}
    pythonlibs.qrc
    icons.qrc
    "${MONACO_QRC}"
    "${EDITOR_QRC}"
    "${STUBS_QRC}"
)

add_dependencies(mc-bot-manager generate_stubs)

target_link_libraries(mc-bot-manager PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Protobuf
    Qt6::Concurrent
    Qt6::WebEngineWidgets
    mc-bot-manager-proto
    pybind11::embed
    Python3::Python
    nbt++
)
target_compile_options(mc-bot-manager PRIVATE ${PROJECT_WARNING_FLAGS})

# Add current directory and generated proto directory to include path
target_include_directories(mc-bot-manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_BINARY_DIR}/_deps/libnbtplusplus-src/include
)

# Define version for use in code
target_compile_definitions(mc-bot-manager PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

set_target_properties(mc-bot-manager PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS mc-bot-manager mc-bot-manager-proto
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(mc-bot-manager)

# Prism hook shared library
FetchContent_Declare(
    prismlauncher
    GIT_REPOSITORY https://github.com/PrismLauncher/PrismLauncher.git
    GIT_TAG        develop
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _no_build
)
FetchContent_MakeAvailable(prismlauncher)

find_package(Threads REQUIRED)

set(PRISMHOOK_INCLUDE_DIRS
    ${prismlauncher_SOURCE_DIR}/launcher
    ${prismlauncher_SOURCE_DIR}/libraries/LocalPeer/include
    $<TARGET_PROPERTY:Qt6::Core,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:Qt6::Gui,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:Qt6::Network,INTERFACE_INCLUDE_DIRECTORIES>
)

if(WIN32)
    add_library(prismhook SHARED prism/PrismHook.cpp)
    target_include_directories(prismhook PRIVATE ${PRISMHOOK_INCLUDE_DIRS})
    target_link_libraries(prismhook PRIVATE Qt6::Core Qt6::Gui Qt6::Network)
else()
    # Linux: split into a proxy loader (no deps) and the real core hook.
    # The loader is preloaded and only loads the core if in PrismLauncher.

    # Core hook: Qt symbols resolve at runtime from PrismLauncher's loaded libs.
    add_library(prismhook_core SHARED prism/PrismHook.cpp)
    target_include_directories(prismhook_core PRIVATE ${PRISMHOOK_INCLUDE_DIRS})
    target_link_libraries(prismhook_core PRIVATE Threads::Threads)
    target_link_options(prismhook_core PRIVATE -Wl,--allow-shlib-undefined)

    # Proxy loader (no deps, safe to preload into any process)
    add_library(prismhook SHARED prism/PrismHookLoader.cpp)
    target_link_libraries(prismhook PRIVATE ${CMAKE_DL_LIBS})
endif()

# AppImage support
option(BUILD_APPIMAGE "Build AppImage" OFF)

if(BUILD_APPIMAGE AND UNIX AND NOT APPLE)
    # Custom target to build AppImage in build directory
    add_custom_target(appimage
        COMMAND ${CMAKE_COMMAND} -E make_directory AppDir/usr/bin
        COMMAND ${CMAKE_COMMAND} -E make_directory AppDir/usr/lib
        COMMAND ${CMAKE_COMMAND} -E make_directory AppDir/usr/share/applications
        COMMAND ${CMAKE_COMMAND} -E make_directory AppDir/usr/share/icons/hicolor/256x256/apps
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/mc-bot-manager AppDir/usr/bin/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/libmc-bot-manager-proto.so AppDir/usr/lib/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/libprismhook.so AppDir/usr/bin/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/libprismhook_core.so AppDir/usr/bin/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/mc-bot-manager.desktop AppDir/usr/share/applications/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/icons/mc-bot-manager.png AppDir/usr/share/icons/hicolor/256x256/apps/ || true
        COMMAND ${CMAKE_COMMAND} -E make_directory "AppDir/usr/lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${Python3_STDLIB}" "AppDir/usr/lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}"
        COMMAND find "AppDir/usr/lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/lib-dynload" -name "_tkinter*" -delete
        # Bundle zuban from the standalone Python install
        COMMAND ${CMAKE_COMMAND} -E copy "${Python3_ROOT_DIR}/bin/zuban" "AppDir/usr/bin/zuban"
        COMMAND linuxdeploy-x86_64.AppImage --appdir AppDir --desktop-file AppDir/usr/share/applications/mc-bot-manager.desktop --plugin qt --output appimage
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building AppImage in build directory..."
    )
    add_dependencies(appimage mc-bot-manager)
endif()
