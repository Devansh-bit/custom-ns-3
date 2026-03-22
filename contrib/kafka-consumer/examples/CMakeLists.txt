# Examples for kafka-consumer module

# Basic Kafka Consumer example
build_lib_example(
    NAME kafka-consumer-example
    SOURCE_FILES kafka-consumer-example.cc
    LIBRARIES_TO_LINK
        ${libkafka-consumer}
        ${libcore}
        ${libnetwork}
        ${libwifi}
        ${libapplications}
)

# # Kafka + LeverApi integration example
# build_lib_example(
#     NAME kafka-lever-integration-example
#     SOURCE_FILES kafka-lever-integration-example.cc
#     LIBRARIES_TO_LINK
#         ${libkafka-consumer}
#         ${liblever-api}
#         ${libcore}
#         ${libnetwork}
#         ${libwifi}
#         ${libinternet}
#         ${libapplications}
#         ${libmobility}
# )
