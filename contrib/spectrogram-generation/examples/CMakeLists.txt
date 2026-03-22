#build_lib_example(
#        NAME spectrogram-generation-example
#        SOURCE_FILES spectrogram-generation-example.cc
#        LIBRARIES_TO_LINK
#        ${libcore}
#        ${libspectrum}
#        ${libmobility}
#        ${libspectrogram-generation}
#)
#build_lib_example(
#        NAME combination-generation-example
#        SOURCE_FILES combination-generation.cc
#        LIBRARIES_TO_LINK
#        ${libcore}
#        ${libspectrum}
#        ${libmobility}
#        ${libspectrogram-generation}
#)
#build_lib_example(
#        NAME spectrogram-5g
#        SOURCE_FILES spectrogram-5ghz.cc
#        LIBRARIES_TO_LINK
#        ${libcore}
#        ${libspectrum}
#        ${libmobility}
#        ${libspectrogram-generation}
#)
build_lib_example(
        NAME duty-cycle
        SOURCE_FILES duty-cycle.cc
        LIBRARIES_TO_LINK
        ${libcore}
        ${libspectrum}
        ${libmobility}
        ${libspectrogram-generation}
)
