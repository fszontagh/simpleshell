cmake_minimum_required(VERSION 3.11)
project(simpleshell)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BINARY_NAME simpleshell)


option(CMAKE_INSTALL_PREFIX "Install prefix" "/usr")

if (CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
    message(STATUS "CMAKE_BUILD_TYPE is not set, defaulting to Release")
endif()
include(GNUInstallDirs)
include(CPack)
include(FetchContent)

FetchContent_Declare(
    inih
    GIT_REPOSITORY https://github.com/benhoyt/inih.git
    GIT_TAG        r59
)

FetchContent_MakeAvailable(inih)

find_package(PkgConfig REQUIRED)
pkg_check_modules(readline readline REQUIRED)
pkg_check_modules(LUA REQUIRED lua5.3)

include_directories(${inih_SOURCE_DIR})

add_executable(${BINARY_NAME} src/main.cpp src/SimpleShell.cpp ${inih_SOURCE_DIR}/ini.c)

target_link_libraries(${BINARY_NAME} inih readline)

install(TARGETS ${BINARY_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_SOURCE_DIR}/config/.pshell DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/skel)