cmake_minimum_required(VERSION 3.16)
project(fenrir LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# --- biblioteka fenrir ----------------------------------------------------
# Warstwy (od dolu): terminal -> core/input/renderer/cursor -> widgets ->
# window -> app (kontrakt App/AppContext/AppRegistry) -> desktop (framework
# spinajacy wszystko, implementuje tez AppContext).
add_library(fenrir STATIC
    src/terminal/Terminal.cpp
    src/core/Screen.cpp
    src/core/FileSystem.cpp
    src/input/KeyParser.cpp
    src/renderer/Renderer.cpp
    src/cursor/Cursor.cpp
    src/widgets/TextArea.cpp
    src/widgets/DirectoryListing.cpp
    src/widgets/FileDialog.cpp
    src/widgets/FileManagerView.cpp
    src/window/Window.cpp
    src/window/WindowManager.cpp
    src/desktop/Desktop.cpp
)

target_include_directories(fenrir PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)



# --- shell: den ------------------------------------------------------------
# To jedyny plik "aplikacyjny" ktory NIE jest App-em - laczy Desktop z
# zarejestrowanymi aplikacjami (patrz apps/shell/main.cpp).
add_executable(fenrir_desktop
    apps/shell/main.cpp
)
target_link_libraries(fenrir_desktop PRIVATE fenrir)
set_target_properties(fenrir_desktop PROPERTIES OUTPUT_NAME fenrir)
