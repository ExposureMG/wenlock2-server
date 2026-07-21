cmake_minimum_required(VERSION 3.20)
project(wenlock2-server LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(wenlock2-server src/main.cpp)

target_include_directories(wenlock2-server PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(wenlock2-server PRIVATE pthread)
