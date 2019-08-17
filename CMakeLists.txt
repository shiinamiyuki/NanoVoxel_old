# CMakeList.txt : CMake project for VoxelPT, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)
project(nanovoxel)

if(MSVC)
	SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} /std:c++14 /arch:AVX2")
	SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} /MP")
	SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} /D_ENABLE_EXTENDED_ALIGNED_STORAGE")
	
else()
	set(CMAKE_CXX_STANDARD 14)
endif()
include_directories("include/")
include_directories("include/nanovoxel")
include_directories("C:\\Program Files (x86)\\IntelSWTools\\OpenCL\\sdk\\include")
link_directories("C:\\Program Files (x86)\\IntelSWTools\\OpenCL\\sdk\\lib\\x64")
link_libraries(glfw3 opengl32 OpenCL)
file(GLOB thirdpartySRC
        "include/fmt/format.cc"
        "include/fmt/posix/cc"
		"include/lodepng/lodepng.cpp"
		)
file(GLOB guiSRC
	"include/imgui/*.c"
	"include/imgui/*.cpp")
file(GLOB_RECURSE mykSRC
	"include/miyuki/*.cc")
file(GLOB_RECURSE kernelSRC "include/nanovoxel/kernel/*.*")
# Add source to this project's executable.
add_executable (nanovoxel  "src/main.cc" "src/corecl.cc" "src/scene.cc" "src/ui.cc" "src/film.cc"
${mykSRC} ${guiSRC} ${thirdpartySRC} ${kernelSRC} )

add_custom_command(TARGET nanovoxel PRE_LINK DEPENDS ${kernelSRC}
                   COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/include/nanovoxel/kernel
                        $<TARGET_FILE_DIR:nanovoxel>/kernel
						)

# file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/include/nanovoxel/kernel DESTINATION ${CMAKE_BINARY_DIR})
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# TODO: Add tests and install targets if needed.
