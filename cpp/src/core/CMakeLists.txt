add_library(kdeck-core STATIC
    Anim.cpp
    Apps.cpp
    Config.cpp
    Deck.cpp
    Keyboard.cpp
    Media.cpp
    Render.cpp
    Stats.cpp
)

target_include_directories(kdeck-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(kdeck-core PUBLIC
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Network
    PkgConfig::LIBEVDEV
)

kde_target_enable_exceptions(kdeck-core PRIVATE)

# Installed copy is what a package uses. A local build can also compile in
# the source tree so a first run works before the files are installed.
# Packages turn that off: the source path is a makepkg srcdir.
option(KDECK_EMBED_SOURCE_EXAMPLES "Compile in the source-tree examples path" ON)
target_compile_definitions(kdeck-core PUBLIC
    KDECK_EXAMPLES_DIR="${CMAKE_INSTALL_FULL_DATADIR}/kdeck-control/examples"
    KDECK_ADDON_SCRIPT="${CMAKE_INSTALL_FULL_DATADIR}/kdeck-control/python/kdeck_addon.py"
)
if(KDECK_EMBED_SOURCE_EXAMPLES)
    target_compile_definitions(kdeck-core PUBLIC
        KDECK_EXAMPLES_SRC="${CMAKE_SOURCE_DIR}/../examples"
        KDECK_ADDON_SRC="${CMAKE_SOURCE_DIR}/../src/kdeck_addon.py"
    )
endif()
