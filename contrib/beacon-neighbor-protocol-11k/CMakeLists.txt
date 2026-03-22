# Create the beacon-neighbor-protocol-11k library
build_lib(
    LIBNAME beacon-neighbor-protocol-11k
    SOURCE_FILES
        model/beacon-neighbor-model.cc
        model/neighbor-report-element.cc
        model/beacon-report-element.cc
        helper/neighbor-protocol-11k-helper.cc
        helper/beacon-protocol-11k-helper.cc
    HEADER_FILES
        model/beacon-neighbor-model.h
        model/neighbor-report-element.h
        model/beacon-report-element.h
        helper/neighbor-protocol-11k-helper.h
        helper/beacon-protocol-11k-helper.h
    LIBRARIES_TO_LINK
        ${libcore}
        ${libnetwork}
        ${libwifi}
        ${libmobility}
        ${libinternet}
        ${libspectrum}
        ${libpropagation}
        ${libdual-phy-sniffer}
        ${libunified-phy-sniffer}
    TEST_SOURCES
        test/beacon-neighbor-protocol-11k-test-suite.cc
)

