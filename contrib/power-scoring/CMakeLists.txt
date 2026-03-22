build_lib(
  LIBNAME power-scoring
  SOURCE_FILES
    model/power-scoring.cc
    helper/power-scoring-helper.cc
  HEADER_FILES
    model/power-scoring.h
    helper/power-scoring-helper.h
  LIBRARIES_TO_LINK
    ${libcore}
    ${libwifi}
    ${libkafka-producer}
)
