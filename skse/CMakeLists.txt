cmake_minimum_required(VERSION 3.25)

project(SkyCraft VERSION 0.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# CommonLibSSE-NG (SE + AE; VR off to keep the build smaller)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)
add_subdirectory(extern/CommonLibSSE-NG CommonLibSSE EXCLUDE_FROM_ALL)
if(NOT COMMAND add_commonlibsse_plugin)
	include(extern/CommonLibSSE-NG/cmake/CommonLibSSE.cmake)
endif()

file(GLOB_RECURSE SKYCRAFT_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)

add_commonlibsse_plugin(
	SkyCraft
	NAME "SkyCraft"
	AUTHOR "chasmlol"
	USE_ADDRESS_LIBRARY
	SOURCES ${SKYCRAFT_SOURCES}
)

target_include_directories(SkyCraft PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../protocol)
target_precompile_headers(SkyCraft PRIVATE src/PCH.h)
target_link_libraries(SkyCraft PRIVATE d3d11.lib d3dcompiler.lib dxgi.lib ole32.lib oleaut32.lib)
target_compile_definitions(SkyCraft PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
	target_compile_options(SkyCraft PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8 /MP /wd4200)
	target_link_options(SkyCraft PRIVATE /DEBUG:FULL)
endif()

# Development: copy the built DLL into an MO2 mod folder (-DSKYCRAFT_DEPLOY_DIR=...), when it exists.
if(SKYCRAFT_DEPLOY_DIR AND EXISTS "${SKYCRAFT_DEPLOY_DIR}")
	add_custom_command(TARGET SkyCraft POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${SKYCRAFT_DEPLOY_DIR}/SKSE/Plugins"
		COMMAND ${CMAKE_COMMAND} "-DDLL=$<TARGET_FILE:SkyCraft>" "-DPDB=$<TARGET_PDB_FILE:SkyCraft>" "-DDEST=${SKYCRAFT_DEPLOY_DIR}/SKSE/Plugins" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/deploy.cmake"
		VERBATIM)
endif()
