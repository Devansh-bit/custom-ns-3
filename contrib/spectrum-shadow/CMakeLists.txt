check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        #test/spectrum-shadow-examples-test-suite.cc
    )
endif()

set(source_files
    model/spectrum-shadow.cc
    helper/spectrum-shadow-helper.cc
    helper/spectrum-kafka-producer.cc
)

set(header_files
    model/spectrum-shadow.h
    helper/spectrum-shadow-helper.h
    helper/spectrum-kafka-producer.h
)

# Find rdkafka C library (required by modern-cpp-kafka)
find_library(RDKAFKA_LIB rdkafka++)
find_library(RDKAFKA_C_LIB rdkafka)

# Add modern-cpp-kafka header-only library include path
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

build_lib(
    LIBNAME spectrum-shadow
    SOURCE_FILES ${source_files}
    HEADER_FILES ${header_files}
    LIBRARIES_TO_LINK
        ${libcore}
        ${libnetwork}
        ${libmobility}
        ${libspectrum}
        ${libwifi}
        ${libpropagation}
        ${libspectrogram-generation}
        ${libspectrum-pipe-streamer}
        ${libspectrum-analyser-logger}
        ${libwaypoint-simulation}
        ${RDKAFKA_LIB}
        ${RDKAFKA_C_LIB}
    TEST_SOURCES
        test/spectrum-shadow-test-suite.cc
        test/spectrum-shadow-integration-test.cc
        ${examples_as_tests_sources}
)
