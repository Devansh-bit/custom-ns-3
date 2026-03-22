check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        #test/sta-channel-hopping-examples-test-suite.cc
        )
endif()

build_lib(
    LIBNAME sta-channel-hopping
    SOURCE_FILES
        model/sta-channel-hopping-manager.cc
        helper/sta-channel-hopping-helper.cc
    HEADER_FILES
        model/sta-channel-hopping-manager.h
        helper/sta-channel-hopping-helper.h
    LIBRARIES_TO_LINK
        ${libcore}
        ${libnetwork}
        ${libwifi}
        ${libspectrum}
        ${libpropagation}
        ${libdual-phy-sniffer}
    TEST_SOURCES
        test/sta-channel-hopping-test-suite.cc
        ${examples_as_tests_sources}
)
