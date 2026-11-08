add_executable(kdeck-control
    main.cpp
    Service.cpp
)

target_link_libraries(kdeck-control PRIVATE kdeck-core Qt6::Gui)
kde_target_enable_exceptions(kdeck-control PRIVATE)

install(TARGETS kdeck-control ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
