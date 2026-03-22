build_lib_example(
    NAME spectrum-shadow-sim
    SOURCE_FILES spectrum-shadow-sim.cc
    LIBRARIES_TO_LINK
        ${libspectrum-shadow}
        ${libcore}
        ${libnetwork}
        ${libmobility}
        ${libspectrum}
        ${libwifi}
        ${libpropagation}
        ${libspectrogram-generation}
        ${libspectrum-pipe-streamer}
        ${libspectrum-analyser-logger}
        ${libwaypoint-simulation}
)
