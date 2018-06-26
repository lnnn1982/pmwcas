// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <condition_variable>
#include <mutex>

#ifdef WIN32
#include <conio.h>
#endif

#include "mwcas_benchmark.h"

using namespace pmwcas::benchmark;

DEFINE_uint64(array_size, 100, "size of the word array for mwcas benchmark");
DEFINE_uint64(descriptor_pool_size, 262144, "number of total descriptors");
DEFINE_string(shm_segment, "lnmwcas", "name of the shared memory segment for"
  " descriptors and data (for persistent MwCAS only)");

using namespace pmwcas;

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

  uint64_t size = sizeof(DescriptorPool::Metadata) +
                  sizeof(Descriptor) * FLAGS_descriptor_pool_size +  // descriptors area
                  sizeof(CasPtr) * FLAGS_array_size;  // data area

  std::cout << "size:" << size << ", Metadata:" << sizeof(DescriptorPool::Metadata)
	  << ", Descriptor:" << sizeof(Descriptor)
	  << ", CasPtr:" << sizeof(CasPtr) << std::endl;
  SharedMemorySegment* segment = nullptr;
  auto s = Environment::Get()->NewSharedMemorySegment(FLAGS_shm_segment, size,
      false, &segment);
  RAW_CHECK(s.ok() && segment, "Error creating memory segment");
  s = segment->Attach();
  std::cout << "s:" << s.ToString() << std::endl;

  RAW_CHECK(s.ok(), "cannot attach");
  memset(segment->GetMapAddress(), 0, size);
  segment->Detach();

 /* test nan if necessary
  std::mutex mutex;
  std::unique_lock<std::mutex> lock(mutex);
  std::condition_variable cv;

  std::cout << "Created shared memory segment" << std::endl;
  cv.wait(lock);
*/

  return 0;
}
