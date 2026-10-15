cmake_minimum_required(VERSION 3.16.0)

project(lxqt-schedule VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Core DBus)

add_subdirectory(src)

install(FILES autostart/lxqt-schedule.desktop
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/xdg/autostart)
