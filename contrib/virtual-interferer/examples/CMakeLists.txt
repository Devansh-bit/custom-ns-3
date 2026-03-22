build_lib_example(
    NAME virtual-interferer-example
    SOURCE_FILES virtual-interferer-example.cc
    LIBRARIES_TO_LINK
        ${libvirtual-interferer}
        ${libcore}
        ${libnetwork}
        ${libmobility}
        ${libwifi}
        ${libapplications}
        ${libinternet}
)
