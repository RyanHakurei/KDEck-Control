add_executable(galleon-deck-config
    main.cpp
    Controller.cpp
)

ecm_add_qml_module(galleon-deck-config
    URI org.kde.galleondeck
    VERSION 1.0
    GENERATE_PLUGIN_SOURCE
)

ecm_target_qml_sources(galleon-deck-config
    SOURCES
        qml/Main.qml
)

kde_target_enable_exceptions(galleon-deck-config PRIVATE)

target_link_libraries(galleon-deck-config PRIVATE
    galleon-core
    Qt6::Widgets
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    KF6::Kirigami
    KF6::I18n
    KF6::CoreAddons
    KF6::IconThemes
)

install(TARGETS galleon-deck-config ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../data/org.kde.galleondeck.desktop
        DESTINATION ${KDE_INSTALL_APPDIR})
