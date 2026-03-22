check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        # test/unified-phy-sniffer-examples-test-suite.cc
    )
endif()

build_lib(
    LIBNAME unified-phy-sniffer
    SOURCE_FILES
        model/parsed-frame-context.cc
        model/unified-phy-sniffer.cc
        helper/unified-phy-sniffer-helper.cc
    HEADER_FILES
        model/object-pool.h
        model/parsed-frame-context.h
        model/unified-phy-sniffer.h
        helper/unified-phy-sniffer-helper.h
    LIBRARIES_TO_LINK
        ${libcore}
        ${libwifi}
        ${libnetwork}
    TEST_SOURCES
        test/unified-phy-sniffer-test-suite.cc
        ${examples_as_tests_sources}
)
