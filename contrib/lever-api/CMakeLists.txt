check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        #test/lever-api-examples-test-suite.cc
        )
endif()

build_lib(
    LIBNAME lever-api
    SOURCE_FILES model/lever-api.cc
                 helper/lever-api-helper.cc
    HEADER_FILES model/lever-api.h
                 helper/lever-api-helper.h
    LIBRARIES_TO_LINK ${libcore}
                      ${libnetwork}
                      ${libwifi}
    TEST_SOURCES test/lever-api-test-suite.cc
                 ${examples_as_tests_sources}
)
