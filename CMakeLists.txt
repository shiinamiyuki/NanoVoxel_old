cmake_minimum_required(VERSION 3.1)

project(NanoVoxel)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -fopenmp)

include_directories(include/)
include_directories(external/glfw/include)
include_directories(external/glm/)
include_directories(external/imgui)

add_subdirectory(external/glfw)

file(GLOB IMGUI_SRC external/imgui/*.* external/imgui/examples/*.*)

add_executable(NanoVoxel src/main.cpp src/gl3w.c src/enkimi.c src/miniz.c ${IMGUI_SRC})
target_link_libraries(NanoVoxel glfw)