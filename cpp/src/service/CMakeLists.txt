add_executable(galleon-deck
    main.cpp
    Service.cpp
)

target_link_libraries(galleon-deck PRIVATE galleon-core Qt6::Gui)
kde_target_enable_exceptions(galleon-deck PRIVATE)
