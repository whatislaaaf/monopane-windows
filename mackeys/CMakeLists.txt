cmake_minimum_required(VERSION 3.16)
project(mackeys LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(mackeys WIN32
    src/main.cpp
    src/settings.cpp
    res/mackeys.rc
)

target_compile_definitions(mackeys PRIVATE UNICODE _UNICODE NOMINMAX)

if(MSVC)
    target_compile_options(mackeys PRIVATE /utf-8)
endif()

target_link_libraries(mackeys PRIVATE
    user32 shell32 advapi32
)
