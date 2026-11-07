add_library(galleon-core STATIC
    Config.cpp
    Deck.cpp
    Keyboard.cpp
    Render.cpp
)

target_include_directories(galleon-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(galleon-core PUBLIC
    Qt6::Core
    Qt6::Gui
    PkgConfig::LIBEVDEV
)

kde_target_enable_exceptions(galleon-core PRIVATE)

target_compile_definitions(galleon-core PUBLIC
    GALLEAN_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/../examples"
)
