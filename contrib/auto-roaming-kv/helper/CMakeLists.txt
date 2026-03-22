check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        #test/auto-roaming-kv-examples-test-suite.cc
        )
endif()

build_lib(
    LIBNAME auto-roaming-kv
    SOURCE_FILES model/auto-roaming-kv.cc
                 helper/auto-roaming-kv-helper.cc
                 helper/ap-link-measurement-helper.cc
    HEADER_FILES model/auto-roaming-kv.h
                 helper/auto-roaming-kv-helper.h
                 helper/ap-link-measurement-helper.h
    LIBRARIES_TO_LINK ${libcore}
                      ${libwifi}
                      ${liblink-protocol-11k}
                      ${libbeacon-neighbor-protocol-11k}
                      ${libbss_tm_11v}
                      ${libdual-phy-sniffer}
                      ${libcsma}
                      ${libapplications}
                      ${libbridge}
    TEST_SOURCES test/auto-roaming-kv-test-suite.cc
                 ${examples_as_tests_sources}
)
