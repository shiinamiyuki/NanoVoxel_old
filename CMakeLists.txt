cmake_minimum_required(VERSION 3.1)

project(NanoVoxel)

set(CMAKE_CXX_STANDARD 17)

include_directories(include/)
include_directories(external/glfw/include)
include_directories(external/glm/)

add_subdirectory(external/glfw)


add_executable(NanoVoxel src/main.cpp src/gl3w.c)
target_link_libraries(NanoVoxel glfw)