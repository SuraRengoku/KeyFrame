cmake_minimum_required(VERSION 3.16)

project(KeyFrame VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


if(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    message(STATUS "Configuring for macOS")
    # Do not include the specific lines for macOS
    add_definitions(-DPLATFORM_MAC)
else()
    message(STATUS "Configuring for other platforms")
    # Include the specific lines for other platforms
    set(Vulkan_LIBRARY "C:/VulkanSDK/1.3.283.0/Lib")
    set(Vulkan_INCLUDE_DIR "C:/VulkanSDK/1.3.283.0/Include")
    add_definitions(-DPLATFORM_WINDOWS)
endif()


find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools)
find_package(Qt6 REQUIRED COMPONENTS OpenGL)
find_package(Qt6 REQUIRED COMPONENTS OpenGLWidgets)
find_package(Qt6 REQUIRED COMPONENTS Concurrent Core Gui Widgets)
find_package(Vulkan REQUIRED)

include_directories("${PROJECT_SOURCE_DIR}/src/components")

set(SHADER_DIR "${CMAKE_SOURCE_DIR}/src/shaders")
add_definitions(-DSHADER_DIR="${SHADER_DIR}")

set(MESH_DIR "${CMAKE_SOURCE_DIR}/resource/meshes")
add_definitions(-DMESH_DIR="${MESH_DIR}")

set(CSV_DIR "${CMAKE_SOURCE_DIR}/resource/csv")
add_definitions(-DCSV_DIR="${CSV_DIR}")

set(TS_FILES KeyFrame_zh_CN.ts)

set(PROJECT_SOURCES
        src/main.cpp
        src/mainwindow.cpp
        src/mainwindow.h
        src/mainwindow.ui
        ${TS_FILES}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)

# target_sources(KeyFrame
#   PRIVATE
#     src/shaders/axis.vs
#     src/shaders/axis.fs
#     src/shaders/mesh.fs
#     src/shaders/mesh.vs
#     src/shaders/point.fs
#     src/shaders/point.vs
# )
qt_add_executable(KeyFrame
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        src/main.cpp
        src/components/glview.h src/components/glview.cpp
        src/components/vkview.h src/components/vkview.cpp
        src/components/renderer.h src/components/renderer.cpp
        src/components/mesh.h src/components/mesh.cpp
        src/components/shader.h src/components/shader.cpp
        src/components/camera.h src/components/camera.cpp
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET KeyFrame APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation

    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    if(ANDROID)
        add_library(KeyFrame SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(KeyFrame
            ${PROJECT_SOURCES}
        )
    endif()

    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

target_link_libraries(KeyFrame PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Concurrent
    Qt6::Core
    Qt6::Gui
)


# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.KeyFrame)
endif()
set_target_properties(KeyFrame PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS KeyFrame
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(KeyFrame)
endif()
