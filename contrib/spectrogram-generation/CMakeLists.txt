check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        #test/spectrogram-generation-examples-test-suite.cc
        )
endif()

set(source_files
        model/spectrogram-generation.cc
        helper/spectrogram-generation-helper.cc
)

set(header_files
        model/spectrogram-generation.h
        helper/spectrogram-generation-helper.h
)

build_lib(
        LIBNAME spectrogram-generation
        SOURCE_FILES ${source_files}
        HEADER_FILES ${header_files}
        LIBRARIES_TO_LINK
        ${libcore}
        ${libspectrum}
        ${libmobility}
)

