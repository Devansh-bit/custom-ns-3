check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        #test/spectrum-pipe-streamer-examples-test-suite.cc
        )
endif()

set(source_files
        model/spectrum-pipe-streamer.cc
        helper/spectrum-pipe-streamer-helper.cc
)

set(header_files
        model/spectrum-pipe-streamer.h
        helper/spectrum-pipe-streamer-helper.h
)

build_lib(
        LIBNAME spectrum-pipe-streamer
        SOURCE_FILES ${source_files}
        HEADER_FILES ${header_files}
        LIBRARIES_TO_LINK
        ${libcore}
        ${libspectrum}
        ${libnetwork}
        ${libmobility}
        ${libinternet}
)
