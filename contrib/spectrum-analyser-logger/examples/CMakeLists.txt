build_lib_example(
        NAME spectrum-analyser-logger-example
        SOURCE_FILES spectrum-analyser-logger-example.cc
        LIBRARIES_TO_LINK
        ${libcore}
        ${libspectrum}
        ${libmobility}
        ${libnetwork}
        ${libspectrum-analyser-logger}
        ${libspectrogram-generation}
)
