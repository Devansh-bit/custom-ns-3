build_lib_example(
    NAME sta-channel-hopping-example
    SOURCE_FILES sta-channel-hopping-example.cc
    LIBRARIES_TO_LINK
        ${libsta-channel-hopping}
        ${libcore}
        ${libnetwork}
        ${libwifi}
        ${libmobility}
        ${libinternet}
        ${libapplications}
        ${libspectrum}
        ${libpropagation}
        ${libdual-phy-sniffer}
)

build_lib_example(
    NAME multi-sta-channel-hopping-example
    SOURCE_FILES multi-sta-channel-hopping-example.cc
    LIBRARIES_TO_LINK
        ${libsta-channel-hopping}
        ${libcore}
        ${libnetwork}
        ${libwifi}
        ${libmobility}
        ${libinternet}
        ${libapplications}
        ${libspectrum}
        ${libpropagation}
        ${libdual-phy-sniffer}
)
