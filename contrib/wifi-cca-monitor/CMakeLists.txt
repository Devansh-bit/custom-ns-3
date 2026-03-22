build_lib(
  LIBNAME wifi-cca-monitor
  SOURCE_FILES
    model/wifi-cca-monitor.cc
    helper/wifi-cca-monitor-helper.cc
  HEADER_FILES
    model/wifi-cca-monitor.h
    helper/wifi-cca-monitor-helper.h
  LIBRARIES_TO_LINK
    ${libcore}
    ${libnetwork}
    ${libwifi}
    ${libinternet}
    ${libapplications}
    ${libvirtual-interferer}
)
