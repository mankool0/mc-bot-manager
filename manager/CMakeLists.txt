cmake_minimum_required(VERSION 3.16)

project(mc-bot-manager VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Protobuf)

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

# Add Windows resource file for icon
if(WIN32)
    list(APPEND PROJECT_SOURCES "mc-bot-manager.rc")
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(mc-bot-manager
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET mc-bot-manager APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(mc-bot-manager SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(mc-bot-manager
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(mc-bot-manager PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Protobuf
    mc-bot-manager-proto
)

# Add current directory and generated proto directory to include path
target_include_directories(mc-bot-manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.mc-bot-manager)
endif()
set_target_properties(mc-bot-manager PROPERTIES
    ${BUNDLE_ID_OPTION}
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

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(mc-bot-manager)
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
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/mc-bot-manager.desktop AppDir/usr/share/applications/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/icons/mc-bot-manager.png AppDir/usr/share/icons/hicolor/256x256/apps/ || true
        COMMAND linuxdeploy-x86_64.AppImage --appdir AppDir --desktop-file AppDir/usr/share/applications/mc-bot-manager.desktop --plugin qt --output appimage
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building AppImage in build directory..."
    )
    add_dependencies(appimage mc-bot-manager)
endif()
