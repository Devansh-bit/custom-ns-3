build_lib_example(
    NAME lever-api-example
    SOURCE_FILES lever-api-example.cc
    LIBRARIES_TO_LINK
        ${liblever-api}
        ${libcore}
        ${libnetwork}
        ${libinternet}
        ${libmobility}
        ${libwifi}
        ${libapplications}
        ${libspectrum}
)

build_lib_example(
    NAME multi-ap-roaming-example
    SOURCE_FILES multi-ap-roaming-example.cc
    LIBRARIES_TO_LINK
        ${liblever-api}
        ${libcore}
        ${libnetwork}
        ${libinternet}
        ${libmobility}
        ${libwifi}
        ${libapplications}
        ${libspectrum}
        ${libcsma}
)

build_lib_example(
    NAME lever-api-comprehensive-test
    SOURCE_FILES lever-api-comprehensive-test.cc
    LIBRARIES_TO_LINK
        ${liblever-api}
        ${libcore}
        ${libnetwork}
        ${libinternet}
        ${libmobility}
        ${libwifi}
        ${libapplications}
        ${libspectrum}
        ${libflow-monitor}
)

build_lib_example(
    NAME channel-switch-demo
    SOURCE_FILES channel-switch-demo.cc
    LIBRARIES_TO_LINK
        ${liblever-api}
        ${libcore}
        ${libnetwork}
        ${libinternet}
        ${libmobility}
        ${libwifi}
        ${libapplications}
        ${libspectrum}
)

build_lib_example(
    NAME lever-api-channel-switch-example
    SOURCE_FILES lever-api-channel-switch-example.cc
    LIBRARIES_TO_LINK
        ${liblever-api}
        ${libcore}
        ${libnetwork}
        ${libinternet}
        ${libmobility}
        ${libwifi}
        ${libapplications}
        ${libspectrum}
)

build_lib_example(
    NAME sta-channel-following-example
    SOURCE_FILES sta-channel-following-example.cc
    LIBRARIES_TO_LINK
        ${liblever-api}
        ${libcore}
        ${libnetwork}
        ${libinternet}
        ${libmobility}
        ${libwifi}
        ${libapplications}
        ${libspectrum}
)

build_lib_example(
    NAME test-propagation
    SOURCE_FILES test-propagation.cc
    LIBRARIES_TO_LINK
        ${liblever-api}
        ${libcore}
        ${libnetwork}
        ${libinternet}
        ${libmobility}
        ${libwifi}
        ${libapplications}
        ${libspectrum}
)
