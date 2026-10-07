cmake_minimum_required(VERSION 3.25)

project(BO3Zombies VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# CommonLibSSE-NG at the commit SkyCraft runs on Skyrim 1.7.104 (SE + AE; VR off).
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/CommonLibSSE-NG CommonLibSSE EXCLUDE_FROM_ALL)
if(NOT COMMAND add_commonlibsse_plugin)
	include(extern/CommonLibSSE-NG/cmake/CommonLibSSE.cmake)
endif()

find_package(imgui CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)

file(GLOB BO3Z_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)

add_commonlibsse_plugin(
	BO3Zombies
	NAME "BO3Zombies"
	AUTHOR "Noah Grove"
	USE_ADDRESS_LIBRARY
	SOURCES ${BO3Z_SOURCES}
)

target_include_directories(BO3Zombies PRIVATE src)
target_precompile_headers(BO3Zombies PRIVATE src/PCH.h)
target_link_libraries(BO3Zombies PRIVATE imgui::imgui Microsoft::DirectXTK d3d11.lib dxgi.lib windowscodecs.lib ole32.lib)
target_compile_definitions(BO3Zombies PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
	target_compile_options(BO3Zombies PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8 /wd4200)
	target_link_options(BO3Zombies PRIVATE /DEBUG:FULL)
endif()
