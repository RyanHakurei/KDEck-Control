cmake_minimum_required(VERSION 3.16)

project(kdeck-control VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQmlModule)
include(FeatureSummary)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core DBus Gui Network Widgets Quick Qml QuickControls2)
find_package(KF6 REQUIRED COMPONENTS Kirigami I18n CoreAddons IconThemes)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)

add_subdirectory(src/core)
add_subdirectory(src/service)
add_subdirectory(src/configurator)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../examples/"
        DESTINATION "${KDE_INSTALL_DATADIR}/kdeck-control/examples")

# kdeck-addon is the add-on package tool. It is still Python.
# Packages written for the previous name are not compatible yet.
# kdeck_control.py is what that tool imports for paths and TOML edits.
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/kdeck_addon.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/kdeck_control.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/kdeck_stats.py"
    DESTINATION "${KDE_INSTALL_DATADIR}/kdeck-control/python")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/kdeck-addon.in"
               "${CMAKE_CURRENT_BINARY_DIR}/kdeck-addon" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/kdeck-addon"
        DESTINATION "${KDE_INSTALL_BINDIR}")

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
