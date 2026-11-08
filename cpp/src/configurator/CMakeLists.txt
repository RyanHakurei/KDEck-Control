add_executable(kdeck-control-config
    main.cpp
    Controller.cpp
)

ecm_add_qml_module(kdeck-control-config
    URI org.kde.kdeckcontrol
    VERSION 1.0
    GENERATE_PLUGIN_SOURCE
)

ecm_target_qml_sources(kdeck-control-config
    SOURCES
        qml/Main.qml
)

kde_target_enable_exceptions(kdeck-control-config PRIVATE)

target_link_libraries(kdeck-control-config PRIVATE
    kdeck-core
    Qt6::Widgets
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    KF6::Kirigami
    KF6::I18n
    KF6::I18nQml
    KF6::CoreAddons
    KF6::IconThemes
)

install(TARGETS kdeck-control-config ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../data/org.kde.kdeckcontrol.desktop
        DESTINATION ${KDE_INSTALL_APPDIR})
