build_lib_example(
  NAME wifi-cca-monitor-example
  SOURCE_FILES wifi-cca-monitor-example.cc
  LIBRARIES_TO_LINK
    ${libwifi-cca-monitor}
    ${libcore}
    ${libnetwork}
    ${libwifi}
    ${libinternet}
    ${libapplications}
    ${libmobility}
)
