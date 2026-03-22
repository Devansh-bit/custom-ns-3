build_lib_example(
    NAME spectrum-pipe-streamer-example
    SOURCE_FILES spectrum-pipe-streamer-example.cc
    LIBRARIES_TO_LINK
    ${libspectrum-pipe-streamer}
    ${libspectrogram-generation}
    ${libspectrum-analyser-logger}
    ${libcore}
    ${libnetwork}
    ${libspectrum}
    ${libmobility}
    ${libapplications}
)
