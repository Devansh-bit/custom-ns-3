build_lib_example(
    NAME config-simulation
    SOURCE_FILES config-simulation.cc
    LIBRARIES_TO_LINK
        ${libfinal-simulation}
        ${libcore}
        ${libnetwork}
        ${libinternet}
        ${libmobility}
        ${libwifi}
        ${libspectrum}
        ${libpropagation}
        ${libapplications}
        ${libflow-monitor}
        ${libcsma}
        ${libbridge}
        ${libbeacon-neighbor-protocol-11k}
        ${libbss_tm_11v}
        ${liblever-api}
        ${liblink-protocol-11k}
        ${libwifi-cca-monitor}
        ${libspectrogram-generation}
        ${libsimulation-helper}
        ${libwaypoint-simulation}
        ${libkafka-producer}
        ${libkafka-consumer}
        ${libdual-phy-sniffer}
        ${libsta-channel-hopping}
        ${libauto-roaming-kv}
)

build_lib_example(
    NAME aci-demo
    SOURCE_FILES aci-demo.cc
    LIBRARIES_TO_LINK
        ${libcore}
)

build_lib_example(
    NAME basic-simulation
    SOURCE_FILES basic-simulation.cc
    LIBRARIES_TO_LINK
        ${libcore}
        ${libnetwork}
        ${libinternet}
        ${libmobility}
        ${libwifi}
        ${libapplications}
        ${libflow-monitor}
        ${libcsma}
        ${libbridge}
        ${libkafka-producer}
        ${libunified-phy-sniffer}
        ${libdual-phy-sniffer}
        ${libbeacon-neighbor-protocol-11k}
        ${liblink-protocol-11k}
        ${libsta-channel-hopping}
)

#build_lib_example(
#        NAME reference
#        SOURCE_FILES reference.cc
#        LIBRARIES_TO_LINK
#        ${libcore}
#        ${libnetwork}
#        ${libinternet}
#        ${libmobility}
#        ${libwifi}
#        ${libapplications}
#        ${libflow-monitor}
#        ${libcsma}
#        ${libbridge}
#        ${libkafka-producer}
#        ${libunified-phy-sniffer}
#        ${libdual-phy-sniffer}
#        ${libbeacon-neighbor-protocol-11k}
#        ${liblink-protocol-11k}
#        ${libsta-channel-hopping}
#)
