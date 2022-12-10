cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS_RELEASE "-pthread")

project(435_the2_client)
add_executable(435_the2_client client.cpp helpers.h helpers.cpp)

project(435_the2_server)
add_executable(435_the2_server server.cpp helpers.h helpers.cpp)