# CMake build file for kafka-producer module

# Find librdkafka using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(RDKAFKA rdkafka++)

if(NOT RDKAFKA_FOUND)
    message(WARNING "librdkafka++ not found - kafka-producer module will not be built")
    return()
endif()

# Add rdkafka include directories
include_directories(${RDKAFKA_INCLUDE_DIRS})
link_directories(${RDKAFKA_LIBRARY_DIRS})

# Add modern-cpp-kafka headers (header-only library)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Build the kafka-producer module
build_lib(
    LIBNAME kafka-producer
    SOURCE_FILES model/kafka-producer.cc
                 helper/kafka-producer-helper.cc
    HEADER_FILES model/kafka-producer.h
                 helper/kafka-producer-helper.h
    LIBRARIES_TO_LINK
        ${libcore}
        ${libnetwork}
        ${libwifi}
        ${libapplications}
        ${libflow-monitor}
        rdkafka++
        rdkafka
    TEST_SOURCES
)
