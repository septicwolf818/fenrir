cmake_minimum_required(VERSION 3.16)
project(fenrir LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# --- fenrir library --------------------------------------------------------
# Layers from the bottom up: terminal -> core/input/renderer/cursor -> widgets
# -> window -> app (App/AppContext/AppRegistry contracts) -> desktop (the
# framework that ties everything together and implements AppContext).
add_library(fenrir STATIC
    src/app/FileAssociationRegistry.cpp
    src/terminal/Terminal.cpp
    src/core/Screen.cpp
    src/core/FileSystem.cpp
    src/core/Themes.cpp
    src/core/Settings.cpp
    src/core/Prefs.cpp
    src/input/KeyParser.cpp
    src/renderer/Renderer.cpp
    src/cursor/Cursor.cpp
    src/widgets/TextArea.cpp
    src/widgets/DirectoryListing.cpp
    src/widgets/FileDialog.cpp
    src/window/Window.cpp
    src/window/WindowManager.cpp
    src/desktop/Desktop.cpp
)

target_include_directories(fenrir PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# forkpty (terminal) — link libutil on Linux; it is part of libc on macOS.
if(UNIX AND NOT APPLE)
    target_link_libraries(fenrir PUBLIC util)
endif()



# --- shell: den ------------------------------------------------------------
# This is the only application-level file that is not an App implementation;
# it connects Desktop to the registered apps (see apps/shell/main.cpp).
add_executable(fenrir_desktop
    apps/shell/main.cpp
    apps/photoviewer/ImageDecoder.cpp
)
target_link_libraries(fenrir_desktop PRIVATE fenrir)
set_target_properties(fenrir_desktop PROPERTIES OUTPUT_NAME fenrir)
