add_executable(lxqt-schedule-ctl
    lxqt-schedule-ctl.cpp
)

target_link_libraries(lxqt-schedule-ctl PRIVATE Qt6::Core Qt6::DBus)

install(TARGETS lxqt-schedule-ctl
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
