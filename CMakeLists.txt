cmake_minimum_required(VERSION 3.16)

project(mc-bot-manager VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Protobuf)

# Generate QtProtobuf files from submodule
file(GLOB PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/submodules/mc-bot-proto/proto/*.proto
)

qt_add_protobuf(mc-bot-manager-proto
    PROTO_FILES ${PROTO_FILES}
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated
)

file(GLOB_RECURSE PROJECT_SOURCES
    "main.cpp"
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
)

# Windows resource file for application icon
if(WIN32)
    set(app_icon_resource_windows "${CMAKE_CURRENT_SOURCE_DIR}/mc-bot-manager.rc")
endif()

qt_add_executable(mc-bot-manager
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
    ${app_icon_resource_windows}
)

target_link_libraries(mc-bot-manager PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Protobuf
    mc-bot-manager-proto
)

# Add current directory and generated proto directory to include path
target_include_directories(mc-bot-manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated
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
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/mc-bot-manager.desktop AppDir/usr/share/applications/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/icons/mc-bot-manager.png AppDir/usr/share/icons/hicolor/256x256/apps/ || true
        COMMAND linuxdeploy-x86_64.AppImage --appdir AppDir --desktop-file AppDir/usr/share/applications/mc-bot-manager.desktop --plugin qt --output appimage
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building AppImage in build directory..."
    )
    add_dependencies(appimage mc-bot-manager)
endif()
