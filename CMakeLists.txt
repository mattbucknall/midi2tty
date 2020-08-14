cmake_minimum_required(VERSION 3.12)
project(midi2tty LANGUAGES C)

find_package(ALSA REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS -Wall)

add_executable(midi2tty main.c)
target_include_directories(midi2tty PUBLIC ${ALSA_INCLUDE_DIRS})
target_link_libraries(midi2tty ${ALSA_LIBRARIES})
