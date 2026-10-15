cmake_minimum_required(VERSION 3.25)

project(CurseOfAlduin VERSION 0.2.0 LANGUAGES CXX)

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
	CurseOfAlduin
	NAME "CurseOfAlduin"
	AUTHOR "Noah Grove"
	USE_ADDRESS_LIBRARY
	SOURCES ${BO3Z_SOURCES}
)

set_target_properties(CurseOfAlduin PROPERTIES OUTPUT_NAME CurseOfAlduin)  # CurseOfAlduin.dll / .asi
target_include_directories(CurseOfAlduin PRIVATE src)
target_precompile_headers(CurseOfAlduin PRIVATE src/PCH.h)
set_source_files_properties(src/Relay.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)  # winsock before windows.h
target_link_libraries(CurseOfAlduin PRIVATE imgui::imgui Microsoft::DirectXTK d3d11.lib dxgi.lib windowscodecs.lib ole32.lib winmm.lib ws2_32.lib)
target_compile_definitions(CurseOfAlduin PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
# the build's commit (CI), so players in a party can see they run the same build (Net.cpp only: no full rebuild)
if(DEFINED ENV{GITHUB_SHA})
	string(SUBSTRING "$ENV{GITHUB_SHA}" 0 7 UN_BUILD_SHA)
	set_source_files_properties(src/Net.cpp PROPERTIES COMPILE_DEFINITIONS "UN_BUILD=\"${UN_BUILD_SHA}\"")
endif()
if(MSVC)
	target_compile_options(CurseOfAlduin PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8 /wd4200)
	target_link_options(CurseOfAlduin PRIVATE /DEBUG:FULL)
endif()
