check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        # test/virtual-interferer-examples-test-suite.cc
    )
endif()

set(source_files
    model/virtual-interferer.cc
    model/virtual-interferer-environment.cc
    model/virtual-interferer-error-model.cc
    model/microwave-interferer.cc
    model/bluetooth-interferer.cc
    model/cordless-interferer.cc
    model/zigbee-interferer.cc
    model/radar-interferer.cc
    helper/virtual-interferer-helper.cc
)

set(header_files
    model/virtual-interferer.h
    model/virtual-interferer-environment.h
    model/virtual-interferer-error-model.h
    model/microwave-interferer.h
    model/bluetooth-interferer.h
    model/cordless-interferer.h
    model/zigbee-interferer.h
    model/radar-interferer.h
    helper/virtual-interferer-helper.h
)

build_lib(
    LIBNAME virtual-interferer
    SOURCE_FILES ${source_files}
    HEADER_FILES ${header_files}
    LIBRARIES_TO_LINK
        ${libcore}
        ${libnetwork}
        ${libmobility}
        ${libwifi}
        ${libspectrum}
    TEST_SOURCES test/virtual-interferer-test-suite.cc
                 ${examples_as_tests_sources}
)
