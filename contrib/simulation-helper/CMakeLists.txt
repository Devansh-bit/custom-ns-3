check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        #test/simulation-helper-examples-test-suite.cc
        )
endif()

build_lib(
    LIBNAME simulation-helper
    SOURCE_FILES model/sim-model.cc
                 helper/simulation-helper.cc
                 helper/metrics-helper.cc
                 helper/roaming-trace-helper.cc
    HEADER_FILES model/sim-model.h
                 helper/simulation-helper.h
                 helper/metrics-helper.h
                 helper/roaming-trace-helper.h
    LIBRARIES_TO_LINK ${libcore}
                      ${libnetwork}
                      ${libwifi}
                      ${libspectrum}
                      ${libmobility}
                      ${libdual-phy-sniffer}
                      ${libbeacon-neighbor-protocol-11k}
                      ${libbss_tm_11v}
                      ${libauto-roaming-kv}
                      ${libkafka-producer}
                      ${liblever-api}
                      ${liblink-protocol-11k}
    TEST_SOURCES test/simulation-helper-test-suite.cc
                 ${examples_as_tests_sources}
)
