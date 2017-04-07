cmake_minimum_required(VERSION 2.8)

project(multirole)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O3")

set(SRC_LIST multirole.c utils.c)

add_executable(${PROJECT_NAME} ${SRC_LIST})

target_link_libraries(${PROJECT_NAME} -lpthread)
