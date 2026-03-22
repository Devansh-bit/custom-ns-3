# CMake build file for simulation-events module

# Find librdkafka using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(RDKAFKA rdkafka++)

if(NOT RDKAFKA_FOUND)
    message(WARNING "librdkafka++ not found - simulation-events module will not be built")
    return()
endif()

# Add rdkafka include directories
include_directories(${RDKAFKA_INCLUDE_DIRS})
link_directories(${RDKAFKA_LIBRARY_DIRS})

# Add modern-cpp-kafka headers (header-only library)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Build the simulation-events module
build_lib(
    LIBNAME simulation-events
    SOURCE_FILES
        model/simulation-event.cc
        model/simulation-event-producer.cc
        helper/simulation-event-helper.cc
    HEADER_FILES
        model/simulation-event.h
        model/simulation-event-producer.h
        helper/simulation-event-helper.h
    LIBRARIES_TO_LINK
        ${libcore}
        ${libnetwork}
        ${libwifi}
        ${libapplications}
        rdkafka++
        rdkafka
    TEST_SOURCES
)
