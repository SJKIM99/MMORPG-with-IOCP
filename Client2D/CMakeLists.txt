cmake_minimum_required(VERSION 3.20)
project(Client2D LANGUAGES C CXX)

# ─────────────────────────────────────────────────────────────────────────────
# MMORPG-with-IOCP 2D 클라이언트 (C++20 + raylib)
#
#   Visual Studio 2022: "폴더 열기"로 이 디렉터리를 열면 CMake가 자동 구성된다.
#   명령줄:             cmake -S . -B build && cmake --build build --config Release
#
# 서버의 GameServer/Protocol.h 와 공유 데이터(Data/*.h, Item/ItemTableData.h)를
# 그대로 include 한다 — 서버와 클라가 같은 패킷 정의/같은 게임 데이터를 읽는다.
# ─────────────────────────────────────────────────────────────────────────────

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(SERVER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../GameServer)
if(NOT EXISTS ${SERVER_DIR}/Protocol.h)
    message(FATAL_ERROR "GameServer/Protocol.h를 찾을 수 없습니다. Client2D 폴더는 저장소 루트(GameServer 옆)에 있어야 합니다.")
endif()

# ── raylib ──────────────────────────────────────────────────────────────────
# 기본은 GitHub 공식 릴리스(5.5)를 받아서 함께 빌드한다(인터넷 필요, 최초 1회).
# 오프라인 환경이면 raylib 소스 경로를 -DRAYLIB_SOURCE_DIR=... 로 넘기면 된다.
set(RAYLIB_SOURCE_DIR "" CACHE PATH "로컬 raylib 소스 경로(비우면 GitHub에서 5.5를 받는다)")
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD OFF CACHE BOOL "" FORCE)
if(RAYLIB_SOURCE_DIR)
    add_subdirectory(${RAYLIB_SOURCE_DIR} ${CMAKE_BINARY_DIR}/raylib EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(raylib
        URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(raylib)
endif()

# ── 실행 파일 ────────────────────────────────────────────────────────────────
file(GLOB_RECURSE CLIENT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_executable(Client2D ${CLIENT_SOURCES})
target_include_directories(Client2D PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SERVER_DIR}
    ${SERVER_DIR}/ObjID
    ${SERVER_DIR}/Item)

# 서버 저장소의 파일명은 ContentiD.h인데 Protocol.h는 "ContentID.h"로 include한다.
# Windows(빌드하는 컴퓨터의 파일 시스템)는 대소문자를 구분하지 않아 문제가 없고,
# 그 밖의 OS(리눅스에서 Windows용 교차 빌드 포함)에서는 이름을 맞춘 얇은 래퍼 헤더를
# 빌드 폴더에 만들어 둔다.
if(NOT CMAKE_HOST_WIN32)
    file(WRITE ${CMAKE_BINARY_DIR}/case_shim/ContentID.h "#pragma once\n#include \"${SERVER_DIR}/ObjID/ContentiD.h\"\n")
    target_include_directories(Client2D BEFORE PRIVATE ${CMAKE_BINARY_DIR}/case_shim)
endif()

target_link_libraries(Client2D PRIVATE raylib)
if(WIN32)
    target_link_libraries(Client2D PRIVATE ws2_32 imm32 winmm opengl32)
    target_compile_definitions(Client2D PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(MSVC)
    # 한글 문자열 리터럴(UTF-8) — 서버 프로젝트와 같은 /utf-8
    target_compile_options(Client2D PRIVATE /utf-8 /W3 /permissive- /MP)
    set_property(TARGET Client2D PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:Client2D>)
    # 콘솔 창 없이 실행(Release)
    set_target_properties(Client2D PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
else()
    target_compile_options(Client2D PRIVATE -Wall -Wno-unused-parameter -Wno-missing-field-initializers -Wno-pedantic)
    if(MINGW)
        # MinGW Release도 콘솔 창 없이 실행
        target_link_options(Client2D PRIVATE $<$<CONFIG:Release>:-mwindows>)
    endif()
endif()

# 폰트 등 에셋을 실행 파일 옆으로 복사
add_custom_command(TARGET Client2D POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:Client2D>/assets)
