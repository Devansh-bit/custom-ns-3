# Basic example demonstrating DualPhySnifferHelper with YansWifiChannel
# Simple beacon monitoring across 2.4 GHz channels (1, 6, 11)
build_lib_example(
    NAME dual-phy-sniffer-example
    SOURCE_FILES dual-phy-sniffer-example.cc
    LIBRARIES_TO_LINK ${libdual-phy-sniffer}
)

# Advanced example demonstrating RRM measurements with SpectrumWifiPhy
# Creates 3D RCPI matrices (channel x transmitter x receiver) and exports to CSV
build_lib_example(
        NAME spectrum-dual-radio-example
        SOURCE_FILES spectrum-dual-radio-example.cc
        LIBRARIES_TO_LINK ${libdual-phy-sniffer}
)
