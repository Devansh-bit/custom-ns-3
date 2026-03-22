check_include_file_cxx(stdint.h HAVE_STDINT_H)
if(HAVE_STDINT_H)
    add_definitions(-DHAVE_STDINT_H)
endif()

set(examples_as_tests_sources)
if(${ENABLE_EXAMPLES})
    set(examples_as_tests_sources
        #test/final-simulation-examples-test-suite.cc
        )
endif()

build_lib(
    LIBNAME final-simulation
    SOURCE_FILES model/final-simulation.cc
                 model/aci-simulation.cc
                 model/ofdma-simulation.cc
                 model/load-balance-helper.cc
                 model/adaptive-udp-application.cc
                 helper/final-simulation-helper.cc
                 helper/adaptive-udp-helper.cc
    HEADER_FILES model/final-simulation.h
                 model/aci-simulation.h
                 model/ofdma-simulation.h
                 model/load-balance-helper.h
                 model/adaptive-udp-application.h
                 helper/final-simulation-helper.h
                 helper/adaptive-udp-helper.h
    LIBRARIES_TO_LINK ${libcore}
                      ${libcsma}
                      ${libinternet}
                      ${libwifi}
                      ${libspectrum}
                      ${libpropagation}
                      ${libmobility}
                      ${libapplications}
                      ${libflow-monitor}
                      ${libbridge}
                      ${libsimulation-helper}
                      ${libwaypoint-simulation}
                      ${libauto-roaming-kv}
                      ${libbeacon-neighbor-protocol-11k}
                      ${libbss_tm_11v}
                      ${libdual-phy-sniffer}
                      ${liblink-protocol-11k}
                      ${libwifi-cca-monitor}
                      ${libkafka-producer}
                      ${libkafka-consumer}
                      ${libsimulation-events}
                      ${liblever-api}
                      ${libchannel-scoring}
                      ${libpower-scoring}
                      ${libvirtual-interferer}

    TEST_SOURCES test/final-simulation-test-suite.cc
                 ${examples_as_tests_sources}
)
