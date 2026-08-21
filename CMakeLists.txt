cmake_minimum_required(VERSION 3.16)
project(monopane LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(monopane WIN32
    src/main.cpp
    src/window_list.cpp
    src/fuzzy.cpp
    src/settings.cpp
    src/aliases.cpp
    external/keychord/keychord_name.cpp
    external/keychord/keychord_chord.cpp
    external/keychord/keychord_capture.cpp
    res/monopane.rc
)

target_compile_definitions(monopane PRIVATE UNICODE _UNICODE NOMINMAX)

if(MSVC)
    target_compile_options(monopane PRIVATE /utf-8)
endif()

target_link_libraries(monopane PRIVATE
    user32 gdi32 shell32 comctl32 dwmapi advapi32 ole32 version
)
