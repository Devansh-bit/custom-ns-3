# Updated to use DualPhySnifferHelper API (channel-agnostic, supports both Yans and Spectrum)
build_lib_example(
    NAME beacon-neighbor-protocol-11k-example
    SOURCE_FILES beacon-neighbor-protocol-11k-example.cc
    LIBRARIES_TO_LINK
        ${libbeacon-neighbor-protocol-11k}
        ${libdual-phy-sniffer}
        ${libcore}
        ${libnetwork}
        ${libwifi}
        ${libmobility}
        ${libinternet}
        ${libspectrum}
        ${libpropagation}
)
