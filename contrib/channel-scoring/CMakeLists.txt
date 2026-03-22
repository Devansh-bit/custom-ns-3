build_lib(
  LIBNAME channel-scoring
  SOURCE_FILES
    model/channel-scoring.cc
    helper/channel-scoring-helper.cc
  HEADER_FILES
    model/channel-scoring.h
    helper/channel-scoring-helper.h
  LIBRARIES_TO_LINK
    ${libcore}
    ${libwifi}
    ${libkafka-producer}
)
