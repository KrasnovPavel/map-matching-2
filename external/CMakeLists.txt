include(FetchContent)

# date
if (${CMAKE_TOOLCHAIN_FILE} MATCHES "vcpkg.cmake$")
    message("Using date from vcpkg")
    find_package(date QUIET CONFIG REQUIRED)
else()
    FetchContent_Declare(date
            GIT_REPOSITORY https://github.com/HowardHinnant/date.git
            GIT_TAG v3.0.1)

    FetchContent_GetProperties(date)
    if (NOT date_POPULATED)
        FetchContent_Populate(date)
    endif ()
    add_subdirectory("${date_SOURCE_DIR}" "${date_BINARY_DIR}" EXCLUDE_FROM_ALL)
endif()

# csv parser
FetchContent_Declare(csv_parser
        GIT_REPOSITORY https://github.com/vincentlaucsb/csv-parser.git
        GIT_TAG 2.1.3)

FetchContent_GetProperties(csv_parser)
if (NOT csv_parser_POPULATED)
    FetchContent_Populate(csv_parser)
endif ()

add_subdirectory("${csv_parser_SOURCE_DIR}" "${csv_parser_BINARY_DIR}" EXCLUDE_FROM_ALL)
set(CSV_PARSER_INCLUDE_DIRS "${csv_parser_SOURCE_DIR}/single_include" CACHE PATH "csv parser include dir" FORCE)

# rpmalloc
FetchContent_Declare(rpmalloc
        GIT_REPOSITORY https://github.com/mjansson/rpmalloc.git
        GIT_TAG 1.4.3)

FetchContent_GetProperties(rpmalloc)
if (NOT rpmalloc_POPULATED)
    FetchContent_Populate(rpmalloc)
endif ()

file(WRITE "${rpmalloc_SOURCE_DIR}/CMakeLists.txt"
        [[
project(rpmalloc
        VERSION 1.4.3
        DESCRIPTION "rpmalloc"
        LANGUAGES C)

add_library(rpmalloc STATIC)

target_compile_definitions(rpmalloc
        PRIVATE
        _GNU_SOURCE
        ENABLE_PRELOAD=1
        ENABLE_OVERRIDE=1
        DEFAULT_SPAN_MAP_COUNT=256)

target_sources(rpmalloc
        PRIVATE
        rpmalloc/rpmalloc.c)

target_include_directories(rpmalloc
        PUBLIC
        rpmalloc)

target_link_libraries(rpmalloc
        PRIVATE
        ${CMAKE_DL_LIBS})
]])

add_subdirectory("${rpmalloc_SOURCE_DIR}" "${rpmalloc_BINARY_DIR}" EXCLUDE_FROM_ALL)
set(RPMALLOC_INCLUDE_DIRS "${rpmalloc_SOURCE_DIR}/rpmalloc" CACHE PATH "rpmalloc include dir" FORCE)

# boost
if (${CMAKE_TOOLCHAIN_FILE} MATCHES "vcpkg.cmake$")
    message("Using boost from vcpkg")
    find_package(Boost QUIET REQUIRED)
else()
    FetchContent_Declare(boost
            URL https://boostorg.jfrog.io/artifactory/main/release/1.77.0/source/boost_1_77_0.tar.gz
            URL_HASH SHA256=5347464af5b14ac54bb945dc68f1dd7c56f0dad7262816b956138fc53bcc0131)

    FetchContent_GetProperties(boost)
    if (NOT boost_POPULATED)
        FetchContent_Populate(boost)
    endif ()

    if (NOT EXISTS "${boost_SOURCE_DIR}/b2")
        IF (WIN32)
            set(_bootstrap_path ${boost_SOURCE_DIR}/bootstrap.bat)
        ELSE()
            set(_bootstrap_path ${boost_SOURCE_DIR}/bootstrap.sh)
        ENDIF()
        execute_process(COMMAND ${_bootstrap_path} --prefix=${boost_BINARY_DIR}
                WORKING_DIRECTORY "${boost_SOURCE_DIR}")
    endif ()

    set(BOOST_ROOT "${boost_BINARY_DIR}" CACHE PATH "Boost root" FORCE)
    set(Boost_NO_SYSTEM_PATHS ON CACHE BOOL "Boost no system paths" FORCE)

    find_package(Boost QUIET CONFIG HINTS "${boost_BINARY_DIR}" NO_DEFAULT_PATH)

    if (NOT Boost_FOUND)
        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_variant "debug")
        else ()
            set(_variant "release")
        endif ()
        message(STATUS "Set boost variant to ${_variant}")

        if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set(_toolset "toolset=gcc")
        endif ()
        message(STATUS "Set boost ${_toolset}")

        execute_process(COMMAND ./b2 install link=shared threading=multi variant=release ${_toolset} --without-python --prefix=${boost_BINARY_DIR}
                WORKING_DIRECTORY "${boost_SOURCE_DIR}")
    endif ()

    find_package(Boost QUIET REQUIRED CONFIG HINTS "${boost_BINARY_DIR}" NO_DEFAULT_PATH)
endif()

# shortest points new
FetchContent_Declare(shortest_points_new
        GIT_REPOSITORY https://github.com/vissarion/geometry.git
        GIT_TAG feature/shortest_points_new)

FetchContent_MakeAvailable(shortest_points_new)
set(SHORTEST_POINTS_NEW_INCLUDE_DIRS "${shortest_points_new_SOURCE_DIR}/include" CACHE PATH "shortest points new include dir" FORCE)

# protozero
if (${CMAKE_TOOLCHAIN_FILE} MATCHES "vcpkg.cmake$")
    message("Using protozero from vcpkg")
    find_path(PROTOZERO_INCLUDE_DIRS "protozero/basic_pbf_builder.hpp")
    include_directories(${PROTOZERO_INCLUDE_DIRS})
else()
    FetchContent_Declare(protozero
            GIT_REPOSITORY https://github.com/mapbox/protozero.git
            GIT_TAG v1.7.0)

    FetchContent_GetProperties(protozero)
    if (NOT protozero_POPULATED)
        FetchContent_Populate(protozero)
    endif ()
    set(PROTOZERO_INCLUDE_DIR "${protozero_SOURCE_DIR}/include" CACHE PATH "protozero include dir" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "do not build protozero tests" FORCE)
    add_subdirectory("${protozero_SOURCE_DIR}" "${protozero_BINARY_DIR}" EXCLUDE_FROM_ALL)
endif()

# libosmium
if (${CMAKE_TOOLCHAIN_FILE} MATCHES "vcpkg.cmake$")
    message("Using libosmium from vcpkg")
    find_path(LIBOSMIUM_INCLUDE_DIRS "osmium/area/assembler.hpp")
    include_directories(${LIBOSMIUM_INCLUDE_DIRS})
else()
    FetchContent_Declare(libosmium
            GIT_REPOSITORY https://github.com/osmcode/libosmium.git
            GIT_TAG v2.17.0)

    FetchContent_GetProperties(libosmium)
    if (NOT libosmium_POPULATED)
        FetchContent_Populate(libosmium)
    endif ()

    list(APPEND CMAKE_MODULE_PATH "${libosmium_SOURCE_DIR}/cmake")
    set(OSMIUM_INCLUDE_DIR "${libosmium_SOURCE_DIR}" CACHE PATH "osmium include dir" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "do not build libosmium examples" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "do not build libosmium tests" FORCE)

    find_package(Osmium QUIET REQUIRED MODULE)
endif()
