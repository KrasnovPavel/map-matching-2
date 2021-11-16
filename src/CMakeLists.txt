include(GNUInstallDirs)

set(CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/run" CACHE PATH "install location" FORCE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

option(INSTALL_ALL_SHARED_LIBS "Install all shared libraries to install directory" ON)

add_subdirectory(environment)
add_subdirectory(geometry)
add_subdirectory(io)
add_subdirectory(learning)
add_subdirectory(matching)
add_subdirectory(util)

find_package(Boost REQUIRED COMPONENTS program_options thread)

add_executable(map_matching_2)
add_library(MM2
        $<TARGET_OBJECTS:util>
        $<TARGET_OBJECTS:environment>
        $<TARGET_OBJECTS:geometry>
        $<TARGET_OBJECTS:io>
        $<TARGET_OBJECTS:learning>
        $<TARGET_OBJECTS:matching>
        )

target_sources(map_matching_2
        PRIVATE
        main.cpp)

target_compile_definitions(map_matching_2
        PRIVATE
        BOOST_ALLOW_DEPRECATED_HEADERS)
target_compile_definitions(MM2
        PRIVATE
        BOOST_ALLOW_DEPRECATED_HEADERS)

target_link_libraries(map_matching_2
        PRIVATE
        util
        environment
        geometry
        io
        learning
        matching
        rpmalloc
        Boost::boost
        Boost::program_options)
target_link_libraries(MM2
        rpmalloc
        pthread
        ${BOOST_LIBRARIES})

install(TARGETS map_matching_2)

install(TARGETS MM2)

set(MM2_INCLUDE_PATHS
        ${CMAKE_CURRENT_SOURCE_DIR}/environment
        ${CMAKE_CURRENT_SOURCE_DIR}/geometry
        ${CMAKE_CURRENT_SOURCE_DIR}/io
        ${CMAKE_CURRENT_SOURCE_DIR}/learning
        ${CMAKE_CURRENT_SOURCE_DIR}/matching
        ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
set(MM2_THIRD_PARTY_PATHS
        ${CMAKE_CACHEFILE_DIR}/_deps/date-src/include/date
        ${CMAKE_CACHEFILE_DIR}/_deps/protozero-src/include/protozero
        ${CMAKE_CACHEFILE_DIR}/_deps/shortest_points_new-src/include/boost
        ${CMAKE_CACHEFILE_DIR}/_deps/rpmalloc-src/rpmalloc
        ${CMAKE_CACHEFILE_DIR}/_deps/csv_parser-src/include
        ${CMAKE_CACHEFILE_DIR}/_deps/libosmium-src/include
        )

install(DIRECTORY ${MM2_INCLUDE_PATHS}
        DESTINATION "include/mm2"
        FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")

install(DIRECTORY ${MM2_THIRD_PARTY_PATHS}
        DESTINATION "include/third_party"
        FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")

#if (CMAKE_BUILD_TYPE STREQUAL "Release")
#    install(CODE "set(INSTALL_ALL_SHARED_LIBS \"${INSTALL_ALL_SHARED_LIBS}\")")
#    install(CODE "set(CMAKE_INSTALL_RPATH \"${CMAKE_INSTALL_RPATH}\")")
#
#    install(CODE [[
#    if (INSTALL_ALL_SHARED_LIBS)
#        message(STATUS "Install all libraries")
#    else ()
#        message(STATUS "Install only necessary libraries")
#        set(POST_INCLUDE_REGEXES "^${CMAKE_BINARY_DIR}")
#        set(POST_EXCLUDE_REGEXES ".*")
#    endif ()
#
#    file(GET_RUNTIME_DEPENDENCIES
#            RESOLVED_DEPENDENCIES_VAR missing
#            EXECUTABLES "$<TARGET_FILE:map_matching_2>"
#            POST_INCLUDE_REGEXES "${POST_INCLUDE_REGEXES}"
#            POST_EXCLUDE_REGEXES "${POST_EXCLUDE_REGEXES}")
#
#    file(INSTALL ${missing}
#            DESTINATION "${CMAKE_INSTALL_RPATH}"
#            TYPE SHARED_LIBRARY
#            FOLLOW_SYMLINK_CHAIN)
#    ]])
#endif ()
