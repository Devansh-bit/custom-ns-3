# CMake build file for kafka-consumer module

# Find librdkafka using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(RDKAFKA rdkafka++)

if(NOT RDKAFKA_FOUND)
    message(WARNING "librdkafka++ not found - kafka-consumer module will not be built")
    return()
endif()

# Add rdkafka include directories
include_directories(${RDKAFKA_INCLUDE_DIRS})
link_directories(${RDKAFKA_LIBRARY_DIRS})

# Add modern-cpp-kafka headers (header-only library)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Build the kafka-consumer module
build_lib(
    LIBNAME kafka-consumer
    SOURCE_FILES model/kafka-consumer.cc
                 model/optimization-command.cc
                 helper/kafka-consumer-helper.cc
    HEADER_FILES model/kafka-consumer.h
                 model/optimization-command.h
                 helper/kafka-consumer-helper.h
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