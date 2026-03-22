check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        #test/dual-phy-sniffer-examples-test-suite.cc
        )
endif()

build_lib(
    LIBNAME dual-phy-sniffer
    SOURCE_FILES helper/dual-phy-sniffer-helper.cc
    HEADER_FILES helper/dual-phy-sniffer-helper.h
    LIBRARIES_TO_LINK ${libcore}
                      ${libwifi}
                      ${libspectrum}
                      ${libpropagation}
                      ${libnetwork}
                      ${libunified-phy-sniffer}
    TEST_SOURCES test/dual-phy-sniffer-test-suite.cc
                 ${examples_as_tests_sources}
)
