# Basic BSS TM example with mobility and hardcoded candidates
build_lib_example(
    NAME bss_tm_11v-example
    SOURCE_FILES bss_tm_11v-example.cc
    LIBRARIES_TO_LINK ${libbss_tm_11v}
)

# Advanced BSS TM example with Distribution System (bridges + CSMA backbone)
build_lib_example(
    NAME bss-tm-dummy-example
    SOURCE_FILES bss-tm-dummy-example.cc
    LIBRARIES_TO_LINK ${libbss_tm_11v} ${libcsma}
)

# Network-controlled roaming example (forces roaming to AP with worse RSSI)
build_lib_example(
    NAME bss-tm-force-low-rssi-roaming
    SOURCE_FILES bss-tm-force-low-rssi-roaming.cc
    LIBRARIES_TO_LINK ${libbss_tm_11v}
)
