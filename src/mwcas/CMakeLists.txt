set(MWCAS_HEADERS
  metrics.h
  mwcas.h
  FASASDescriptor.h
)

set(MWCAS_SOURCES
  mwcas.cc
  FASASDescriptor.cc
)

add_library(mwcas STATIC ${MWCAS_SOURCES} ${MWCAS_HEADERS})
target_link_libraries(mwcas ${GFLAGS_LIB} ${GLOG_LIB})

ADD_PMWCAS_TEST(mwcas_tests)