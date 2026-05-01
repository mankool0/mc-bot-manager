cmake_minimum_required(VERSION 3.16)

project(mc-bot-manager VERSION 1.6.0 LANGUAGES CXX)

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
        )
        FetchContent_GetProperties(python_standalone)
        if(NOT python_standalone_POPULATED)
            FetchContent_Populate(python_standalone)
        endif()
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
)
FetchContent_MakeAvailable(monaco_editor)

find_program(ESBUILD_EXECUTABLE esbuild REQUIRED)

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
        COMMAND ${ESBUILD_EXECUTABLE}
            "${MONACO_ESM_ENTRY}"
            --bundle
            --format=iife
            --global-name=monaco
            --minify
            --loader:.ttf=dataurl
            --outfile=${MONACO_BUNDLE_JS}
        RESULT_VARIABLE ESBUILD_RESULT
    )
    if(NOT ESBUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "esbuild failed to bundle Monaco Editor")
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

# Download Jedi and Parso wheels for Python autocompletion
file(DOWNLOAD
    "https://files.pythonhosted.org/packages/c0/5a/9cac0c82afec3d09ccd97c8b6502d48f165f9124db81b4bcb90b4af974ee/jedi-0.19.2-py2.py3-none-any.whl"
    "${CMAKE_BINARY_DIR}/jedi.whl"
    EXPECTED_HASH SHA256=a8ef22bde8490f57fe5c7681a3c83cb58874daf72b4784de3cce5b6ef6edb5b9
    SHOW_PROGRESS
)
file(DOWNLOAD
    "https://files.pythonhosted.org/packages/c6/ac/dac4a63f978e4dcb3c6d3a78c4d8e0192a113d288502a1216950c41b1027/parso-0.8.4-py2.py3-none-any.whl"
    "${CMAKE_BINARY_DIR}/parso.whl"
    EXPECTED_HASH SHA256=a418670a20291dacd2dddc80c377c5c3791378ee1e8d12bffc35420643d43f18
    SHOW_PROGRESS
)

set(JEDI_QRC "${CMAKE_CURRENT_BINARY_DIR}/jedi_resources.qrc")
file(WRITE  "${JEDI_QRC}" "<RCC>\n  <qresource prefix=\"/pythonlibs\">\n")
file(APPEND "${JEDI_QRC}" "    <file alias=\"jedi.whl\">${CMAKE_BINARY_DIR}/jedi.whl</file>\n")
file(APPEND "${JEDI_QRC}" "    <file alias=\"parso.whl\">${CMAKE_BINARY_DIR}/parso.whl</file>\n")
file(APPEND "${JEDI_QRC}" "  </qresource>\n</RCC>\n")

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
    "${JEDI_QRC}"
)

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
)
FetchContent_GetProperties(prismlauncher)
if(NOT prismlauncher_POPULATED)
    FetchContent_Populate(prismlauncher)
endif()

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
        COMMAND linuxdeploy-x86_64.AppImage --appdir AppDir --desktop-file AppDir/usr/share/applications/mc-bot-manager.desktop --plugin qt --output appimage
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building AppImage in build directory..."
    )
    add_dependencies(appimage mc-bot-manager)
endif()
