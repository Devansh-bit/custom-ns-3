check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        #test/link-protocol-11k-examples-test-suite.cc
        )
endif()

build_lib(
        LIBNAME link-protocol-11k
        SOURCE_FILES
            model/link-measurement-protocol.cc
            model/link-measurement-request.cc
            model/link-measurement-report.cc
            model/tpc-report-element.cc
            helper/link-measurement-protocol-helper.cc
        HEADER_FILES
            model/link-measurement-protocol.h
            model/link-measurement-request.h
            model/link-measurement-report.h
            model/tpc-report-element.h
            helper/link-measurement-protocol-helper.h
        LIBRARIES_TO_LINK
            ${libwifi}
            ${libinternet}
            ${libmobility}
            ${libunified-phy-sniffer}
        TEST_SOURCES
            test/link-protocol-11k-test-suite.cc
)


