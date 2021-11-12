set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if (${CMAKE_TOOLCHAIN_FILE} MATCHES "vcpkg.cmake$")
    find_package(date CONFIG REQUIRED)
else()
    list(APPEND CMAKE_MODULE_PATH "${OSMIUM_INCLUDE_DIR}/cmake")
    find_package(Osmium REQUIRED COMPONENTS io proj)
endif()

add_library(io STATIC)

target_sources(io
        PRIVATE
        importer.cpp
        exporter.cpp
        file_exporter.cpp
        csv_importer.cpp
        csv_exporter.cpp
        network/arc_node_importer.cpp
        network/osm_car_importer.cpp
        network/osm_exporter.cpp
        network/osm_handler.cpp
        network/osm_importer.cpp
        network/osm_relations_manager.cpp
        track/edges_list_importer.cpp
        track/csv_track_importer.cpp
        track/input_importer.cpp)

target_include_directories(io
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..)


if (${CMAKE_TOOLCHAIN_FILE} MATCHES "vcpkg.cmake$")
    target_include_directories(io SYSTEM
            PUBLIC
            ${CSV_PARSER_INCLUDE_DIRS})

    target_link_libraries(io
            PUBLIC
            geometry
            Threads::Threads
            date::date)
else()
    target_include_directories(io SYSTEM
            PUBLIC
            ${CSV_PARSER_INCLUDE_DIRS}
            ${OSMIUM_INCLUDE_DIR}/include)

    target_link_libraries(io
            PUBLIC
            geometry
            Threads::Threads
            date::date
            ${OSMIUM_LIBRARIES})
endif()
