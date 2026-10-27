cmake_minimum_required(VERSION 3.16)
project(fenrir LANGUAGES CXX)

enable_testing()

option(BUILD_TESTING "Build the test suite" OFF)

add_subdirectory(third_party/litehtml EXCLUDE_FROM_ALL)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

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
    src/core/Clipboard.cpp
    src/core/FileSystem.cpp
    src/core/FileWatchdog.cpp
    src/core/Themes.cpp
    src/core/Settings.cpp
    src/core/Prefs.cpp
    src/core/Notifications.cpp
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

add_library(fenrir_browser STATIC
    apps/browser/BrowserNetwork.cpp
    apps/browser/BrowserPage.cpp
    apps/browser/BrowserView.cpp
    apps/browser/BrowserApp.cpp
)
target_include_directories(fenrir_browser PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/browser
)
target_link_libraries(fenrir_browser PUBLIC fenrir litehtml CURL::libcurl Threads::Threads)
target_link_libraries(fenrir_desktop PRIVATE fenrir_browser)
set_target_properties(fenrir_desktop PROPERTIES OUTPUT_NAME fenrir)

# --- tests -----------------------------------------------------------------
# ONE test binary: the test sources are split by area (tests/test_*.cpp) and
# the runner (tests/main.cpp) executes every suite. `make test` runs it.
if(BUILD_TESTING)
    add_executable(fenrir_tests
        tests/main.cpp
        tests/test_vt.cpp
        tests/test_filetree.cpp
        tests/test_watchdog.cpp
        tests/test_editor.cpp
        tests/test_clipboard.cpp
        tests/test_handbook.cpp
        tests/test_window.cpp
        tests/test_browser.cpp
    )
    target_link_libraries(fenrir_tests PRIVATE fenrir_browser)
    add_test(NAME fenrir_tests COMMAND fenrir_tests)
endif()

# --- install rules ---------------------------------------------------------
# Only the compiled binary is installed.
install(TARGETS fenrir_desktop
    RUNTIME DESTINATION bin
)
