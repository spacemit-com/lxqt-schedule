add_executable(lxqt-schedule
    main.cpp
    scheduledaemon.cpp
    scheduledaemon.h
)

target_link_libraries(lxqt-schedule PRIVATE Qt6::Core Qt6::DBus)
target_include_directories(lxqt-schedule PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

install(TARGETS lxqt-schedule
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_subdirectory(cli)
