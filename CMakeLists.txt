cmake_minimum_required(VERSION 3.10)
project(Assignment_3 C)

set(CMAKE_C_STANDARD 11)
include_directories(/usr/include/openssl/)
link_libraries(ssl)
link_libraries(crypto)
add_executable(Assignment_3 main.c)
add_executable(proxy httpechosrv.c)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")


