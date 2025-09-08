cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

set(ENVSDK $ENV{PLAYDATE_SDK_PATH})

if (NOT ${ENVSDK} STREQUAL "")
	# Convert path from Windows
	file(TO_CMAKE_PATH ${ENVSDK} SDK)
else()
	execute_process(
		COMMAND bash -c "egrep '^\\s*SDKRoot' $HOME/.Playdate/config"
		COMMAND head -n 1
		COMMAND cut -c9-
		OUTPUT_VARIABLE SDK
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
endif()

if (NOT EXISTS ${SDK})
	message(FATAL_ERROR "SDK Path not found; set ENV value PLAYDATE_SDK_PATH")
	return()
endif()

set(PLAYDATE_GAME_NAME PD4j)
set(PLAYDATE_GAME_DEVICE PD4j_DEVICE)
set(CMAKE_BUILD_TYPE Release)

project(${PLAYDATE_GAME_NAME} C ASM)

set(PD4J_SRCS
	src/pd4j/class_loader.c
	src/pd4j/class.c
	src/pd4j/descriptor.c
	src/pd4j/file.c
	src/pd4j/list.c
	src/pd4j/lua_glue.c
	src/pd4j/memory.c
	src/pd4j/module.c
	src/pd4j/resolve.c
	src/pd4j/thread.c
	src/pd4j/utf8.c
)

if(TOOLCHAIN STREQUAL "armgcc")
	add_executable(${PLAYDATE_GAME_DEVICE} ${SDK}/C_API/buildsupport/setup.c src/main.c 3rdparty/miniz/miniz.c ${PD4J_SRCS})
	target_include_directories(${PLAYDATE_GAME_DEVICE} PRIVATE 3rdparty/miniz)
else()
	add_library(${PLAYDATE_GAME_NAME} SHARED src/main.c 3rdparty/miniz/miniz.c 3rdparty/miniz/miniz.c ${PD4J_SRCS})
	target_include_directories(${PLAYDATE_GAME_NAME} PRIVATE 3rdparty/miniz)
endif()

include(${SDK}/C_API/buildsupport/playdate_game.cmake)

