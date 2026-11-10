cmake_minimum_required(VERSION 3.29)
project(TcpServer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(TcpServer src/Main.cc src/HttpHandle.cc)

target_include_directories(TcpServer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(TcpServer
    PRIVATE
        pthread
)

target_compile_options(TcpServer
    PRIVATE
        -Wall
        -Wextra
)