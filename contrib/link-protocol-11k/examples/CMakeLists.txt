build_lib_example(
        NAME link-protocol-test
        SOURCE_FILES link-protocol-11k-example.cc
        LIBRARIES_TO_LINK ${libcore}
        ${libnetwork}
        ${libinternet}
        ${libwifi}
        ${libmobility}
        ${libapplications}
        ${libspectrum}
        ${libpropagation}
        ${liblink-measurement-protocol}
)
