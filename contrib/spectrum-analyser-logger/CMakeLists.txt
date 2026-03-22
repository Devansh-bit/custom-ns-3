check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        #test/spectrum-analyser-logger-examples-test-suite.cc
        )
endif()

set(source_files
        model/spectrum-analyser-logger.cc
        helper/spectrum-analyser-logger-helper.cc
)

set(header_files
        model/spectrum-analyser-logger.h
        helper/spectrum-analyser-logger-helper.h
)

build_lib(
        LIBNAME spectrum-analyser-logger
        SOURCE_FILES ${source_files}
        HEADER_FILES ${header_files}
        LIBRARIES_TO_LINK
        ${libcore}
        ${libspectrum}
        ${libnetwork}
        ${libmobility}
)
