# CMakeList.txt : CMake project for VoxelPT, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)


if(MSVC)
	SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} /std:c++17 /arch:AVX2")
	SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} /MP")
	SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} /D_ENABLE_EXTENDED_ALIGNED_STORAGE")
else()
	set(CMAKE_CXX_STANDARD 17)
endif()
include_directories("include/")
include_directories("include/nanovoxel")
include_directories("C:\\Program Files (x86)\\IntelSWTools\\OpenCL\\sdk\\include")
link_directories("C:\\Program Files (x86)\\IntelSWTools\\OpenCL\\sdk\\lib\\x64")
link_libraries(glfw3 opengl32 OpenCL)
file(GLOB thirdpartySRC
        "include/fmt/format.cc"
        "include/fmt/posix/cc"
		)
file(GLOB guiSRC
	"include/imgui/*.c"
	"include/imgui/*.cpp")

# Add source to this project's executable.
add_executable (nanovoxel  "src/main.cc" "src/corecl.cc" ${guiSRC} ${thirdpartySRC})
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# TODO: Add tests and install targets if needed.
