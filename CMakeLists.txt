cmake_minimum_required(VERSION 3.5)
project(simpleshell
    VERSION 0.1.0
    DESCRIPTION "A simple shell"
    HOMEPAGE_URL "https://github.com/fszontagh/simpleshell"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BINARY_NAME simpleshell)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "CPACK_SOURCE_GENERATOR: ${CPACK_SOURCE_GENERATOR}")


if (CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
    message(STATUS "CMAKE_BUILD_TYPE is not set, defaulting to Release")
    else()
    set(DEBUG 1)
endif()



# packaging
    set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
    set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/assets/linux/DESCRIPTION.txt")


set(CPACK_PACKAGE_CONTACT "Ferenc Szontágh <szf@fsociety.hu>")
#set(CPACK_DEBIAN_PACKAGE_DEPENDS  "libc6 (>= 2.28), libreadline8t64 (>= 8.0), liblua5.3-0 (>= 5.3.5)")
set(CPACK_PACKAGE_VENDOR "fszontagh")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/szontagh/simpleshell")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Simple shell")
set(CPACK_PACKAGE_CHECKSUM "SHA512")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_EXECUTABLES ${BINARY_NAME} "Simple Shell")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS  ON)
set(CPACK_STRIP_FILES YES)
set(
    CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE
)
set(CPACK_PACKAGING_INSTALL_PREFIX "/")

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    execute_process(
        COMMAND bash -c "source /etc/lsb-release && echo \"$DISTRIB_ID$DISTRIB_RELEASE\""
        OUTPUT_VARIABLE LSB_CODENAME
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(CPACK_SYSTEM_NAME ${LSB_CODENAME})
    message(STATUS "Detected LSB_CODENAME: ${LSB_CODENAME}")
endif()

include(FetchContent)

###  inih
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

### SOL
include(FetchContent)

FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG        v3.3.0
)

if(NOT sol2_POPULATED)
    FetchContent_Populate(sol2)
endif()
set(sol2_EXCLUDE_FROM_ALL TRUE)



find_package(PkgConfig REQUIRED)
pkg_check_modules(readline readline REQUIRED)

find_package(Lua REQUIRED)

add_executable(${BINARY_NAME} src/main.cpp src/SimpleShell.cpp src/PluginManager.cpp ${inih_SOURCE_DIR}/ini.c)

target_link_libraries(${BINARY_NAME} inih readline ${LUA_LIBRARIES})
target_include_directories(${BINARY_NAME} PRIVATE ${inih_SOURCE_DIR} ${LUA_INCLUDE_DIR} ${CMAKE_BINARY_DIR}/include ${sol2_SOURCE_DIR}/include)



if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set_property(CACHE CMAKE_INSTALL_PREFIX PROPERTY VALUE "/")
endif()

include(GNUInstallDirs)

set("PLUGINS_DIR" "${CMAKE_INSTALL_FULL_DATAROOTDIR}/simpleshell/plugins")
# install
message(STATUS "Plugins directory: ${PLUGINS_DIR}")


configure_file(${CMAKE_SOURCE_DIR}/assets/plugins/Plugin.lua.in ${CMAKE_BINARY_DIR}/plugins/base/Plugin.lua @ONLY FILE_PERMISSIONS OWNER_READ GROUP_READ WORLD_READ)
configure_file(${CMAKE_SOURCE_DIR}/assets/plugins/Example.lua ${CMAKE_BINARY_DIR}/plugins/Example.lua COPYONLY FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
configure_file(${CMAKE_SOURCE_DIR}/config/options.h.in ${CMAKE_BINARY_DIR}/include/options.hpp @ONLY FILE_PERMISSIONS OWNER_READ GROUP_READ WORLD_READ)

install(TARGETS ${BINARY_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_SOURCE_DIR}/config/.pshell DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/skel)
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/simpleshell)
install(DIRECTORY ${CMAKE_BINARY_DIR}/plugins DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/simpleshell)

include(CPack)