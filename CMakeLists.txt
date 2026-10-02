cmake_minimum_required(VERSION 3.21)
project(UnnamedGpl3QtApp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)

option(UNNAMED_BUILD_TESTS "Build unit tests" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 Widgets)
find_package(KF6Kirigami REQUIRED)

# Optional external libraries live in extern/ (git submodules). They are not
# required yet; wire them into `unnamed_core` below once they are checked out:
#   git submodule update --init --recursive

# --- core: GUI-free logic (filesystem / format abstractions) ----------------
add_library(unnamed_core STATIC
    include/core/Status.hpp
    include/core/FileSystem.hpp
    include/core/LocalFileSystem.hpp
    include/core/FileSystemRegistry.hpp
    src/core/FileSystem.cpp
    src/core/LocalFileSystem.cpp
    src/core/FileSystemRegistry.cpp
)
target_include_directories(unnamed_core PUBLIC include)
target_compile_features(unnamed_core PUBLIC cxx_std_20)

# --- application: Qt Quick + Kirigami UI -------------------------------------
qt_add_executable(UnnamedGpl3QtApp src/Main.cpp)

qt_add_qml_module(UnnamedGpl3QtApp
    URI org.exposuremg.unnamed
    VERSION 1.0
    QML_FILES
        QML/Main.qml
        QML/components/StatusBar.qml
        QML/pages/About.qml
        QML/pages/Browser.qml
    SOURCES
        include/gui/FileBrowser.hpp
        include/gui/FileSystemModel.hpp
        src/gui/FileBrowser.cpp
        src/gui/FileSystemModel.cpp
)
# qmltyperegistrar includes the QML_ELEMENT headers by bare file name.
target_include_directories(UnnamedGpl3QtApp PRIVATE include/gui)
target_link_libraries(UnnamedGpl3QtApp PRIVATE
    unnamed_core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    KF6::Kirigami
)

# --- tests ------------------------------------------------------------------
if(UNNAMED_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
