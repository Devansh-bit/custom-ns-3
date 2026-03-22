check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        #test/bss_tm_11v-examples-test-suite.cc
        )
endif()

build_lib(
    LIBNAME bss_tm_11v
    SOURCE_FILES model/bss_tm_11v.cc
                model/kv-interface-model.cc
                 helper/bss_tm_11v-helper.cc
                 helper/kv-interface.cc
    HEADER_FILES model/bss_tm_11v.h
                model/kv-interface-model.h
                 helper/bss_tm_11v-helper.h
                 helper/kv-interface.h
    LIBRARIES_TO_LINK
        ${libcore}
        ${libnetwork}
        ${libwifi}
        ${libmobility}
        ${libapplications}
        ${libbeacon-neighbor-protocol-11k}
        ${liblever-api}
        ${libunified-phy-sniffer}
        ${libdual-phy-sniffer}
    TEST_SOURCES test/bss_tm_11v-test-suite.cc
                 ${examples_as_tests_sources}
)
