cmake_minimum_required (VERSION 3.2.2)

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
  message(FATAL_ERROR "In-source builds are not allowed.")
endif("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")

enable_testing()

include(ExternalProject)
project(pmwcas)

if(WIN32)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zi /nologo /W3 /WX /EHsc /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TP /errorReport:queue")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /FC /d2Zi+ /wd4018 /wd4100 /wd4101 /wd4127 /wd4189 /wd4200 /wd4244 /wd4267 /wd4296 /wd4305 /wd4307 /wd4309 /wd4512 /wd4701 /wd4702 /wd4800 /wd4804 /wd4996 /wd4005 /wd4091 /wd4722")

  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Od /RTC1 /Gm /MDd")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Oi /Gm- /Gy /MD")

  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG /OPT:REF /OPT:NOICF /INCREMENTAL:NO")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /DEBUG /OPT:REF /OPT:NOICF /INCREMENTAL:NO")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wno-error")

  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

  #set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lgtest -lglog -lgflags -lpthread -lrt -lnuma -pg")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lgtest -lglog -lgflags -lpthread -lrt -lnuma -lpmemobj -lpmem")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -lgtest -lglog -lgflags -lpthread -lrt -lnuma -lpmemobj -lpmem")
endif()

#Always set _DEBUG compiler directive when compiling bits regardless of target OS
set_directory_properties(PROPERTIES COMPILE_DEFINITIONS_DEBUG "_DEBUG")

add_definitions()

# Set PMEM by default to build persistent version
option(WITH_PMEM "Support persistence" ON)
if(WITH_PMEM)
  add_definitions(-DPMEM)
endif()

#add_definitions(-pg)

# Turn off RTM by default
option(WITH_RTM "Use RTM for installing descriptors" OFF)
if(WITH_RTM)
  if(WITH_PMEM)
    message(FATAL_ERROR "Cannot use RTM with persistence.")
  endif()
  add_definitions(-DRTM)
endif()

# Install gtest as en external project
set(GTEST_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/gtest")
set(GTEST_LOCATION "${GTEST_PREFIX}/src/GTestExternal-build")
set(GTEST_INCLUDES "${GTEST_PREFIX}/src/GTestExternal/googletest/include")
set(gtest_force_shared_crt ON CACHE BOOL "Force gtest to use dynamic standard library" )
include_directories(${GTEST_INCLUDES})

# external project download and build
ExternalProject_Add(GTestExternal
  URL ${CMAKE_CURRENT_SOURCE_DIR}/third-party/googletest
  PREFIX "${GTEST_PREFIX}"

  # cmake arguments
  CMAKE_ARGS -Dgtest_force_shared_crt=ON

  # Disable install step
  INSTALL_COMMAND ""

  # Wrap download, configure and build steps in a script to log output
  LOG_DOWNLOAD ON
  LOG_CONFIGURE ON
  LOG_BUILD ON
)

if (MSVC)
    set(GTEST_IMPORTED_LOCATION
        IMPORTED_LOCATION_DEBUG           "${GTEST_LOCATION}/googlemock/gtest/Debug/${CMAKE_STATIC_LIBRARY_PREFIX}gtestd${CMAKE_STATIC_LIBRARY_SUFFIX}"
        IMPORTED_LOCATION_RELEASE         "${GTEST_LOCATION}/googlemock/gtest/Release/${CMAKE_STATIC_LIBRARY_PREFIX}gtest${CMAKE_STATIC_LIBRARY_SUFFIX}"
        )
    set(GTESTMAIN_IMPORTED_LOCATION
        IMPORTED_LOCATION_DEBUG           "${GTEST_LOCATION}/googlemock/gtest/Debug/${CMAKE_STATIC_LIBRARY_PREFIX}gtest_maind${CMAKE_STATIC_LIBRARY_SUFFIX}"
        IMPORTED_LOCATION_RELEASE         "${GTEST_LOCATION}/googlemock/gtest/Release/${CMAKE_STATIC_LIBRARY_PREFIX}gtest_main${CMAKE_STATIC_LIBRARY_SUFFIX}"
        )
else()
    set(GTEST_IMPORTED_LOCATION
        IMPORTED_LOCATION                 "${GTEST_LOCATION}/googlemock/gtest")
    set(GTESTMAIN_IMPORTED_LOCATION
        IMPORTED_LOCATION                 "${GTEST_LOCATION}/googlemock/gtest_main")
  link_directories(${GTEST_LOCATION})
  link_directories(${GTEST_LOCATION}/googlemock/gtest)
endif()

# the gtest include directory exists only after it is built
file(MAKE_DIRECTORY ${GTEST_INCLUDES})

# define imported library GTest
add_library(GTest IMPORTED STATIC GLOBAL)
set_target_properties(GTest PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES     "${GTEST_INCLUDES}"
    IMPORTED_LINK_INTERFACE_LIBRARIES "${CMAKE_THREAD_LIBS_INIT}"
    ${GTEST_IMPORTED_LOCATION}
)

# define imported library GTestMain
add_library(GTestMain IMPORTED STATIC GLOBAL)
set_target_properties(GTestMain PROPERTIES
    IMPORTED_LINK_INTERFACE_LIBRARIES GTest
    ${GTESTMAIN_IMPORTED_LOCATION}
    )

# make GTest depend on GTestExternal
add_dependencies(GTest GTestExternal)

###############################

# variables to help keep track of gflags paths
set(GFLAGS_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/gflags")
set(GFLAGS_LOCATION "${GFLAGS_PREFIX}/src/GFlagsExternal-build")
set(GFLAGS_INCLUDES "${GFLAGS_PREFIX}/src/GFlagsExternal-build/include")

# external project download and build (no install for gtest)
ExternalProject_Add(GFlagsExternal
  URL ${CMAKE_CURRENT_SOURCE_DIR}/third-party/gflags-2.1.2/gflags
  PREFIX "${GFLAGS_PREFIX}"

  
  # Disable install step
  INSTALL_COMMAND ""

  # Wrap download, configure and build steps in a script to log output
  LOG_DOWNLOAD ON
  LOG_CONFIGURE ON
  LOG_BUILD ON
)

# variables defining the import location properties for the generated gtest and
# gtestmain libraries
if (MSVC)
  set(GFLAGS_IMPORTED_LOCATION
    IMPORTED_LOCATION_DEBUG           "${GFLAGS_LOCATION}/lib/Debug/${CMAKE_STATIC_LIBRARY_PREFIX}gflags${CMAKE_STATIC_LIBRARY_SUFFIX}"
    IMPORTED_LOCATION_RELEASE         "${GFLAGS_LOCATION}/lib/Release/${CMAKE_STATIC_LIBRARY_PREFIX}gflags${CMAKE_STATIC_LIBRARY_SUFFIX}"
      )
else()
  set(GFLAGS_IMPORTED_LOCATION
    IMPORTED_LOCATION                 "${GFLAGS_LOCATION}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}gflags${CMAKE_STATIC_LIBRARY_SUFFIX}")
  link_directories(${GFLAGS_LOCATION}/lib)
endif()

# the gtest include directory exists only after it is build, but it is used/needed
# for the set_target_properties call below, so make it to avoid an error
file(MAKE_DIRECTORY ${GFLAGS_INCLUDES})

# define imported library GFlags
add_library(GFlags IMPORTED STATIC GLOBAL)
set_target_properties(GFlags PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES     "${GFLAGS_INCLUDES}"
    IMPORTED_LINK_INTERFACE_LIBRARIES "${CMAKE_THREAD_LIBS_INIT}"
    ${GFLAGS_IMPORTED_LOCATION}
)
include_directories(${GFLAGS_INCLUDES})
add_dependencies(GFlags GFlagsExternal)

###############################

# variables to help keep track of glog paths
set(GLOG_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/glog")
set(GLOG_LOCATION "${GLOG_PREFIX}/src/GLogExternal-build")
if(WIN32)
  set(GLOG_INCLUDES "${GLOG_PREFIX}/src/GLogExternal/src/windows")
  file(MAKE_DIRECTORY ${GLOG_INCLUDES})
else()
  set(APPEND GLOG_INCLUDES "${GLOG_PREFIX}/src/GLogExternal/src")
  file(MAKE_DIRECTORY "${GLOG_PREFIX}/src/GLogExternal/src")
  include_directories("${GLOG_PREFIX}/src/GLogExternal/src")

  set(APPEND GLOG_INCLUDES "${GLOG_PREFIX}/src/GLogExternal-build")
  file(MAKE_DIRECTORY "${GLOG_PREFIX}/src/GLogExternal-build")
  include_directories("${GLOG_PREFIX}/src/GLogExternal-build")
endif()

# external project download and build (no install for gtest)
ExternalProject_Add(GLogExternal
  URL ${CMAKE_CURRENT_SOURCE_DIR}/third-party/glog-0.3.4
  PREFIX "${GLOG_PREFIX}"
  DEPENDS GFlagsExternal
  # cmake arguments
  CMAKE_ARGS -DCMAKE_PREFIX_PATH=${GFLAGS_LOCATION}

  # Disable install step
  INSTALL_COMMAND ""

  # Wrap download, configure and build steps in a script to log output
  LOG_DOWNLOAD ON
  LOG_CONFIGURE ON
  LOG_BUILD ON
)

if (MSVC)
  set(GLOG_IMPORTED_LOCATION
    IMPORTED_LOCATION_DEBUG           "${GLOG_LOCATION}/Debug/${CMAKE_STATIC_LIBRARY_PREFIX}glog${CMAKE_STATIC_LIBRARY_SUFFIX}"
    IMPORTED_LOCATION_RELEASE         "${GLOG_LOCATION}/Release/${CMAKE_STATIC_LIBRARY_PREFIX}glog${CMAKE_STATIC_LIBRARY_SUFFIX}"
      )
else()
  set(GLOG_IMPORTED_LOCATION
    IMPORTED_LOCATION                 "${GLOG_LOCATION}/${CMAKE_STATIC_LIBRARY_PREFIX}glog${CMAKE_STATIC_LIBRARY_SUFFIX}")
  link_directories(${GLOG_LOCATION})
endif()

# the glog include directory exists only after it is build, but it is used/needed
# for the set_target_properties call below, so make it to avoid an error
#file(MAKE_DIRECTORY ${GLOG_INCLUDES})

# define imported library GFlags
add_library(GLog IMPORTED STATIC GLOBAL)
set_target_properties(GLog PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES     "${GLOG_INCLUDES}"
  IMPORTED_LINK_INTERFACE_LIBRARIES "${CMAKE_THREAD_LIBS_INIT}"
  ${GLOG_IMPORTED_LOCATION}
)
add_dependencies(GLog GLogExternal)

set(GFLAGS_LIB GFlags)
set(GLOG_LIB GLog)
set(GTEST_LIB GTest)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/src)

# Set the directory targets when build in libs and binaries
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Set the list of libraries that make up the pmwcas stack
set (PMWCAS_LINK_LIBS
  environment
  util
  common
  mwcas
)

# Set the list of libraries that use pmwcas
set (PMWCAS_APPS_LINK_LIBS
  double-linked-list
)

if(WIN32)
  set(WINDOWS_ENVIRONMENT_SOURCES
    src/environment/environment_windows.cc
  )
else()
  set(LINUX_ENVIRONMENT_SOURCES
    src/environment/environment_linux.cc
  )
endif()

# Set the link libraries to for test compilation
set (PMWCAS_TEST_LINK_LIBS ${PMWCAS_LINK_LIBS} ${GTEST_LIB})
if(WIN32)
  set (PMWCAS_TEST_LINK_LIBS ${PMWCAS_LINK_LIBS} ${GTEST_LIB})
  # shlwapi is needed for gflags when compiling on windows
  set (PMWCAS_TEST_LINK_LIBS ${PMWCAS_TEST_LINK_LIBS} shlwapi)
else()
  set (PMWCAS_TEST_LINK_LIBS ${PMWCAS_LINK_LIBS})
  set (PMWCAS_TEST_LINK_LIBS ${PMWCAS_TEST_LINK_LIBS} "-lgtest -lglog -lgflags -lpthread -lrt -lnuma -lpmemobj -lpmem")
endif()

# Set the link libraries to for benchmark binary compilation
set (PMWCAS_BENCHMARK_LINK_LIBS ${PMWCAS_LINK_LIBS} ${GFLAGS_LIB} ${GLOG_LIB})
if(WIN32)
  # shlwapi is needed for gflags when compiling on windows
  set (PMWCAS_BENCHMARK_LINK_LIBS ${PMWCAS_BENCHMARK_LINK_LIBS} shlwapi)
else()
  set (PMWCAS_BENCHMARK_LINK_LIBS ${PMWCAS_BENCHMARK_LINK_LIBS} "-lgtest -lglog -lgflags -lpthread -lrt -lnuma -lpmemobj -lpmem")
endif()

#Function to automate building test binaries with appropriate environment sources
FUNCTION(ADD_PMWCAS_TEST TEST_NAME)
  if(WIN32)
    add_executable(${TEST_NAME} ${TEST_NAME}.cc ${CMAKE_SOURCE_DIR}/${WINDOWS_ENVIRONMENT_SOURCES})
  else()
    add_executable(${TEST_NAME} ${TEST_NAME}.cc ${CMAKE_SOURCE_DIR}/${LINUX_ENVIRONMENT_SOURCES})
  endif()

  target_link_libraries(${TEST_NAME} ${PMWCAS_TEST_LINK_LIBS} ${PMWCAS_APPS_LINK_LIBS})
  target_include_directories(${TEST_NAME} PUBLIC ${gtest_SOURCE_DIR} ${gtest_SOURCE_DIR}/include) 
  add_test(${TEST_NAME} ${CMAKE_BINARY_DIR}/${TEST_NAME})
ENDFUNCTION()

#Function to automate building benchmark binaries with appropriate environment sources
FUNCTION(ADD_PMWCAS_BENCHMARK BENCHMARK_NAME)
  if(WIN32)
    add_executable(${BENCHMARK_NAME} ${BENCHMARK_HEADERS} ${BENCHMARK_NAME}.cc ${CMAKE_SOURCE_DIR}/${WINDOWS_ENVIRONMENT_SOURCES})
  else()
    add_executable(${BENCHMARK_NAME} ${BENCHMARK_HEADERS} ${BENCHMARK_NAME}.cc ${CMAKE_SOURCE_DIR}/${LINUX_ENVIRONMENT_SOURCES})
  endif()

  target_link_libraries(${BENCHMARK_NAME} ${PMWCAS_BENCHMARK_LINK_LIBS} ${PMWCAS_APPS_LINK_LIBS})
ENDFUNCTION()

# Build each subdirectory
add_subdirectory(src/util)
add_subdirectory(src/environment)
add_subdirectory(src/common)
add_subdirectory(src/benchmarks)
add_subdirectory(src/mwcas)
add_subdirectory(src/double-linked-list)
