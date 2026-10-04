cmake_minimum_required(VERSION 3.21)
project(UnnamedGpl3QtApp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(UNNAMED_BUILD_APP "Build the Qt/Kirigami application (needs Qt 6 and KDE Frameworks 6)" ON)
option(UNNAMED_BUILD_TESTS "Build unit tests" ON)

include(cmake/Features.cmake)

# --- core: GUI-free logic (filesystem / format abstractions) ----------------
add_library(unnamed_core STATIC
    include/core/Status.hpp
    include/core/Stream.hpp
    include/core/Transfer.hpp
    include/core/FileSystem.hpp
    include/core/Format.hpp
    include/core/PathUtil.hpp
    include/core/DemoFileSystem.hpp
    include/core/LocalFileSystem.hpp
    include/core/FileSystemRegistry.hpp
    src/core/Stream.cpp
    src/core/Transfer.cpp
    src/core/FileSystem.cpp
    src/core/DemoFileSystem.cpp
    src/core/LocalFileSystem.cpp
    src/core/FileSystemRegistry.cpp
)
target_include_directories(unnamed_core PUBLIC include)
target_compile_features(unnamed_core PUBLIC cxx_std_20)

# --- application: Qt Quick + Kirigami UI -------------------------------------
if(UNNAMED_BUILD_APP)
    set(CMAKE_AUTOMOC ON)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 Widgets)
    find_package(KF6Kirigami REQUIRED)
    find_package(KF6IconThemes REQUIRED)

    qt_add_executable(UnnamedGpl3QtApp src/Main.cpp)

    qt_add_qml_module(UnnamedGpl3QtApp
        URI org.exposuremg.unnamed
        VERSION 1.0
        QML_FILES
            QML/Main.qml
            QML/components/DetailsView.qml
            QML/components/FileGridView.qml
            QML/components/FileListView.qml
            QML/components/ItemActions.qml
            QML/components/JobsPopup.qml
            QML/components/KindIcon.qml
            QML/components/PathBar.qml
            QML/components/PlacesSidebar.qml
            QML/components/StatusBar.qml
            QML/pages/About.qml
            QML/pages/Browser.qml
            QML/pages/Details.qml
        SOURCES
            include/gui/FileBrowser.hpp
            include/gui/FileSystemModel.hpp
            include/gui/JobModel.hpp
            include/gui/MountModel.hpp
            include/gui/MountRuntime.hpp
            src/gui/FileBrowser.cpp
            src/gui/FileSystemModel.cpp
            src/gui/JobModel.cpp
            src/gui/MountModel.cpp
    )
    # qmltyperegistrar includes the QML_ELEMENT headers by bare file name.
    target_include_directories(UnnamedGpl3QtApp PRIVATE include/gui)
    target_link_libraries(UnnamedGpl3QtApp PRIVATE
        unnamed_core
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Widgets
        KF6::Kirigami
        KF6::IconThemes
    )
endif()

# --- tests ------------------------------------------------------------------
if(UNNAMED_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
