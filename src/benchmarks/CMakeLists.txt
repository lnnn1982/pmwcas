set(BENCHMARK_HEADERS
  ../util/random_number_generator.h
)

#ADD_PMWCAS_BENCHMARK(mwcas_benchmark)
ADD_PMWCAS_BENCHMARK(mwcas_shm_server)
ADD_PMWCAS_BENCHMARK(doubly_linked_list_benchmark)

add_executable(mwcas_benchmark ${BENCHMARK_HEADERS} mwcas_benchmark.cc fetchStoreStore.cc RecoverMutex.cc ${CMAKE_SOURCE_DIR}/${LINUX_ENVIRONMENT_SOURCES})
target_link_libraries(mwcas_benchmark ${PMWCAS_BENCHMARK_LINK_LIBS} ${PMWCAS_APPS_LINK_LIBS})