// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <condition_variable>
#include <mutex>

#ifdef WIN32
#include <conio.h>
#endif

#include "mwcas_benchmark.h"

using namespace pmwcas::benchmark;

DEFINE_uint64(array_size, 5000, "size of the word array for mwcas benchmark");
DEFINE_uint64(descriptor_pool_size, 1048576, "number of total descriptors");
DEFINE_string(shm_segment, "lnmwcas", "name of the shared memory segment for"
  " descriptors and data (for persistent MwCAS only)");
DEFINE_uint64(threads, 8, "number of threads to use for multi-threaded tests");
DEFINE_string(benchmarks, "FASAS", "fetch store and store");
DEFINE_int32(FASAS_BASE_TYPE, 1, "fetch store store base type ");

using namespace pmwcas;

//test gprof
void testPrint() {
    for(int i =0; i< 100000*10000; i++) {
    	float a = (float)100000/3444;
    }
}

// Start a process to create a shared memory segment and sleep
int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  google::ParseCommandLineFlags(&argc, &argv, true);

  FLAGS_log_dir = "./";
  
  LOG(INFO) << "Array size: " << FLAGS_array_size;
  LOG(INFO) << "Descriptor pool size: " << FLAGS_descriptor_pool_size;
  LOG(INFO) << "Segment name: " << FLAGS_shm_segment;

#ifdef WIN32
  pmwcas::InitLibrary(pmwcas::DefaultAllocator::Create,
      pmwcas::DefaultAllocator::Destroy, pmwcas::WindowsEnvironment::Create,
      pmwcas::WindowsEnvironment::Destroy);
#else
  pmwcas::InitLibrary(pmwcas::DefaultAllocator::Create,
      pmwcas::DefaultAllocator::Destroy, pmwcas::LinuxEnvironment::Create,
      pmwcas::LinuxEnvironment::Destroy);
#endif

  std::string benchmark(FLAGS_benchmarks);
  uint64_t size = 0;
  if(benchmark == "mwcas") {
    size = sizeof(DescriptorPool::Metadata) +
                  sizeof(Descriptor) * FLAGS_descriptor_pool_size +  // descriptors area
                  sizeof(CasPtr) * FLAGS_array_size;  // data area     
    std::cout << "size:" << size << ", Metadata:" << sizeof(DescriptorPool::Metadata)
	  << ", Descriptor:" << sizeof(Descriptor)
	  << ", CasPtr:" << sizeof(CasPtr) << std::endl;
  }
  else {
    if(FLAGS_FASAS_BASE_TYPE == 0) {
      size = sizeof(DescriptorPool::Metadata) +
                    sizeof(Descriptor) * FLAGS_descriptor_pool_size +  // descriptors area
                    //sizeof(CasPtr) * FLAGS_array_size + // share data area
                    256 * FLAGS_array_size + // share data area
                    sizeof(CasPtr) * FLAGS_threads;  // private data area
      std::cout << "size:" << size << ", Metadata:" << sizeof(DescriptorPool::Metadata)
	    << ", Descriptor:" << sizeof(Descriptor)
	    << ", CasPtr:" << sizeof(CasPtr) << std::endl;
    }
    else {
      size = sizeof(DescriptorPool::Metadata) +
                    sizeof(FASASDescriptor) * FLAGS_descriptor_pool_size +  // descriptors area
                    //sizeof(FASASCasPtr) * FLAGS_array_size + // share data area
                    256 * FLAGS_array_size + // share data area
                    sizeof(FASASCasPtr) * FLAGS_threads;  // private data area
      std::cout << "size:" << size << ", Metadata:" << sizeof(DescriptorPool::Metadata)
	    << ", FASASDescriptor:" << sizeof(FASASDescriptor)
	    << ", FASASCasPtr:" << sizeof(FASASCasPtr) << std::endl;
    }
  }

  SharedMemorySegment* segment = nullptr;
  auto s = Environment::Get()->NewSharedMemorySegment(FLAGS_shm_segment, size,
      false, &segment);
  RAW_CHECK(s.ok() && segment, "Error creating memory segment");
  s = segment->Attach();
  std::cout << "s:" << s.ToString() << std::endl;

  RAW_CHECK(s.ok(), "cannot attach");
  memset(segment->GetMapAddress(), 0, size);
  segment->Detach();

  //testPrint();

 /* test nan if necessary
  std::mutex mutex;
  std::unique_lock<std::mutex> lock(mutex);
  std::condition_variable cv;

  std::cout << "Created shared memory segment" << std::endl;
  cv.wait(lock);
*/

  std::cout << "exit" << std::endl;
  return 0;
}
