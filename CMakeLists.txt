cmake_minimum_required(VERSION 3.11)
project(simpleshell)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BINARY_NAME simpleshell)


option(CMAKE_INSTALL_PREFIX "Install prefix" "/usr")
option(CPACK_PACKAGING_INSTALL_PREFIX "Install prefix" "/usr")

if (CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
    message(STATUS "CMAKE_BUILD_TYPE is not set, defaulting to Release")
endif()



# packaging
set(CPACK_PACKAGE_CONTACT "Ferenc Szontágh <szf@fsociety.hu>")
set(CPACK_DEBIAN_PACKAGE_DEPENDS  "libc6 (>= 2.28), libreadline8t64 (>= 8.0), liblua5.3-0 (>= 5.3.5)")

set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")


include(CPack)
include(FetchContent)

FetchContent_Declare(
    inih
    GIT_REPOSITORY https://github.com/benhoyt/inih.git
    GIT_TAG        r59
)

FetchContent_MakeAvailable(inih)
FetchContent_GetProperties(inih)
if(NOT inih_POPULATED)
    FetchContent_Populate(inih)
endif()

add_library(inih STATIC
    ${inih_SOURCE_DIR}/ini.c
)
target_include_directories(inih PUBLIC ${inih_SOURCE_DIR})


find_package(PkgConfig REQUIRED)
pkg_check_modules(readline readline REQUIRED)
pkg_check_modules(LUA REQUIRED lua5.3)

include_directories(${inih_SOURCE_DIR})

add_executable(${BINARY_NAME} src/main.cpp src/SimpleShell.cpp ${inih_SOURCE_DIR}/ini.c)

target_link_libraries(${BINARY_NAME} inih readline)

install(TARGETS ${BINARY_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
install(FILES ${CMAKE_SOURCE_DIR}/config/.pshell DESTINATION ${CMAKE_INSTALL_PREFIX}/etc/skel)
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION ${CMAKE_INSTALL_PREFIX}/share/licenses/simpleshell)