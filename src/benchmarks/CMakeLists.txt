set(BENCHMARK_HEADERS
  ../util/random_number_generator.h
)

#ADD_PMWCAS_BENCHMARK(mwcas_benchmark)
ADD_PMWCAS_BENCHMARK(mwcas_shm_server)
#ADD_PMWCAS_BENCHMARK(testGProfile)
ADD_PMWCAS_BENCHMARK(doubly_linked_list_benchmark)

add_executable(mwcas_benchmark ${BENCHMARK_HEADERS} mwcas_benchmark.cc fetchStoreStore.cc RecoverMutex.cc 
LinearCheckerLogWriter.cc ${CMAKE_SOURCE_DIR}/${LINUX_ENVIRONMENT_SOURCES})
target_link_libraries(mwcas_benchmark ${PMWCAS_BENCHMARK_LINK_LIBS} ${PMWCAS_APPS_LINK_LIBS})


add_executable(msqueue_benchmark ${BENCHMARK_HEADERS} msqueue_benchmark.cc PMWCasOpWrapper.cc MSQueueByPMWCas.cc 
MSQueueByLog.cc MSQueueByPMCasV2.cc fetchStoreStore.cc MSQueueByOrgCas.cc RecoverCAS.cpp ${CMAKE_SOURCE_DIR}/${LINUX_ENVIRONMENT_SOURCES})
target_link_libraries(msqueue_benchmark ${PMWCAS_BENCHMARK_LINK_LIBS} ${PMWCAS_APPS_LINK_LIBS})

add_executable(recoverCAS_benchmark ${BENCHMARK_HEADERS} recoverCas_benchmark.cc fetchStoreStore.cc RecoverCAS.cpp ${CMAKE_SOURCE_DIR}/${LINUX_ENVIRONMENT_SOURCES})
target_link_libraries(recoverCAS_benchmark ${PMWCAS_BENCHMARK_LINK_LIBS} ${PMWCAS_APPS_LINK_LIBS})

