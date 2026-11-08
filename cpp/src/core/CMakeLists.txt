add_library(galleon-core STATIC
    Anim.cpp
    Apps.cpp
    Config.cpp
    Deck.cpp
    Keyboard.cpp
    Media.cpp
    Render.cpp
    Stats.cpp
)

target_include_directories(galleon-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(galleon-core PUBLIC
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Network
    PkgConfig::LIBEVDEV
)

kde_target_enable_exceptions(galleon-core PRIVATE)

# Installed copy is what a package uses. A local build can also compile in
# the source tree so a first run works before the files are installed.
# Packages turn that off: the source path is a makepkg srcdir.
option(GALLEAN_EMBED_SOURCE_EXAMPLES "Compile in the source-tree examples path" ON)
target_compile_definitions(galleon-core PUBLIC
    GALLEAN_EXAMPLES_DIR="${CMAKE_INSTALL_FULL_DATADIR}/galleon-deck/examples"
    GALLEAN_ADDON_SCRIPT="${CMAKE_INSTALL_FULL_DATADIR}/galleon-deck/python/galleon_addon.py"
)
if(GALLEAN_EMBED_SOURCE_EXAMPLES)
    target_compile_definitions(galleon-core PUBLIC
        GALLEAN_EXAMPLES_SRC="${CMAKE_SOURCE_DIR}/../examples"
        GALLEAN_ADDON_SRC="${CMAKE_SOURCE_DIR}/../src/galleon_addon.py"
    )
endif()
