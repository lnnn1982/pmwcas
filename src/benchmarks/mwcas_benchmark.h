// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#pragma once

#define NOMINMAX
#include <inttypes.h>

#include <gtest/gtest.h>
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "glog/raw_logging.h"

#ifdef WIN32
#include "environment/environment_windows.h"
#else
#include "environment/environment_linux.h"
#endif
#include "benchmarks/benchmark.h"
#include "mwcas_common.h"


using namespace pmwcas::benchmark;

DEFINE_uint64(seed, 1234, "base random number generator seed, the thread index"
    "is added to this number to form the full seed");
DEFINE_uint64(threads, 8, "number of threads to use for multi-threaded tests");
DEFINE_uint64(seconds, 30, "default time to run a benchmark");
DEFINE_uint64(metrics_dump_interval, 0, "if greater than 0, the benchmark "
              "driver dumps metrics at this fixed interval (in seconds)");
DEFINE_int32(affinity, 1, "affinity to use in scheduling threads");
DEFINE_uint64(descriptor_pool_size, 1048576, "number of total descriptors");
DEFINE_string(shm_segment, "lnmwcas", "name of the shared memory segment for"
    " descriptors and data (for persistent MwCAS only)");
DEFINE_int32(enable_stats, 1, "whether to enable stats on MwCAS internal"
    " operations");

#ifdef PMEM
DEFINE_uint64(write_delay_ns, 0, "NVRAM write delay (ns)");
DEFINE_bool(emulate_write_bw, false, "Emulate write bandwidth");
DEFINE_bool(clflush, true, "Use CLFLUSH, instead of spinning delays."
  "write_dealy_ns and emulate_write_bw will be ignored.");
#endif

/// Maximum number of threads that the benchmark driver supports.
//const size_t kMaxNumThreads = 64;

/// Dumps args in a format that can be extracted by an experiment script
void DumpArgs() {
  std::cout << "> Args shm_segment " << FLAGS_shm_segment.c_str() << std::endl;
  std::cout << "> Args threads " << FLAGS_threads << std::endl;
  std::cout << "> Args affinity " << FLAGS_affinity << std::endl;
  std::cout << "> Args desrciptor_pool_size " <<
      FLAGS_descriptor_pool_size << std::endl;

#ifdef PMEM
  if(FLAGS_clflush) {
    printf("> Args using clflush\n");
  } else {
    std::cout << "> Args write_delay_ns " << FLAGS_write_delay_ns << std::endl;
    std::cout << "> Args emulate_write_bw " <<
        FLAGS_emulate_write_bw << std::endl;
  }
#endif
}

namespace pmwcas {
typedef MwcTargetField<uint64_t> CasPtr;
typedef FASASTargetField<uint64_t> FASASCasPtr;


struct BaseMwCas : public Benchmark {
  BaseMwCas()
    : Benchmark()
    , previous_dump_run_ticks_()
    , cumulative_stats_ () {
    total_success_ = 0;
  }

  uint64_t GetOperationCount() {
    MwCASMetrics metrics;
    MwCASMetrics::Sum(metrics);
    return metrics.GetUpdateAttemptCount();
  }

  uint64_t GetTotalSuccess() {
    return total_success_.load();
  }

  virtual void Dump(size_t thread_count, uint64_t run_ticks, uint64_t dump_id,
      bool final_dump) {
    MARK_UNREFERENCED(thread_count);
    uint64_t interval_ticks = 0;
    if(final_dump) {
      interval_ticks = run_ticks;
    } else {
      interval_ticks = run_ticks - previous_dump_run_ticks_;
      previous_dump_run_ticks_ = run_ticks;
    }
    Benchmark::Dump(thread_count, run_ticks, dump_id, final_dump);

    MwCASMetrics stats;
    MwCASMetrics::Sum(stats);
    if(!final_dump) {
      stats -= cumulative_stats_;
      cumulative_stats_ += stats;
    }

    stats.Print();

#ifdef WIN32
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    uint64_t ticks_per_second = frequency.QuadPart;
#else
    uint64_t ticks_per_second = 1000000;
#endif
    std::cout << "> Benchmark " << dump_id << " TicksPerSecond " <<
      ticks_per_second << std::endl;
    std::cout << "> Benchmark " << dump_id << " RunTicks " <<
      run_ticks << std::endl;

    double run_seconds = (double)run_ticks / ticks_per_second;
    std::cout << "> Benchmark " << dump_id << " RunSeconds " <<
      run_seconds << std::endl;

    std::cout << "> Benchmark " << dump_id << " IntervalTicks " <<
      interval_ticks << std::endl;
    double interval_seconds = (double)interval_ticks / ticks_per_second;
    std::cout << "> Benchmark " << dump_id << " IntervalSeconds " <<
      interval_seconds << std::endl;

    for(const auto & idOpNum : threadIdOpNumMap_) {
        std::cout << "threadId:" << idOpNum.first << ", opNum:" << idOpNum.second << std::endl;
    }
  }

  ///////////////////////////////////////////////////////////////////////////////

  SharedMemorySegment* initSharedMemSegment(std::string const & segmentName, uint64_t size) {
    SharedMemorySegment* segment = nullptr;
    auto s = Environment::Get()->NewSharedMemorySegment(segmentName, size, true,
        &segment);
    RAW_CHECK(s.ok() && segment, "Error creating memory segment");

    // Attach anywhere to extract the base address we used last time
    s = segment->Attach();
    RAW_CHECK(s.ok(), "cannot attach");

	uintptr_t base_address = *(uintptr_t*)((uintptr_t)segment->GetMapAddress() +
        sizeof(uint64_t));
    if(base_address) {
      std::cout << "base_address:" << std::hex << base_address << std::endl;
      isNewMem_ = false;
    
	  // An existing pool, with valid descriptors and data
      if(base_address != (uintptr_t)segment->GetMapAddress()) {
        segment->Detach();
        s = segment->Attach((void*)base_address);
        if(!s.ok()) {
          LOG(FATAL) << "Cannot attach to the segment with given base address";
        }

        RAW_CHECK((uintptr_t)segment->GetMapAddress() == base_address, "base address and map address not match");
      }
    } 
    else {
      // New pool/data area, store this base address, pass it + meatadata_size
      // as desc pool va
      isNewMem_ = true;
      void* map_address = segment->GetMapAddress();
      DescriptorPool::Metadata *metadata = (DescriptorPool::Metadata*)map_address;
      metadata->descriptor_count = FLAGS_descriptor_pool_size;
      metadata->initial_address = (uintptr_t)map_address;

      std::cout << "Initialized new descriptor pool and data areas" << std::endl;
    }

    std::cout << "initSharedMemSegment size:" << std::dec << size << ", isNewMem_:" << isNewMem_ << std::endl;

    DescriptorPool::Metadata *metadata = (DescriptorPool::Metadata*)(segment->GetMapAddress());
    std::cout << "descriptor_count::" << std::dec << metadata->descriptor_count << ", initial_address:"
        << std::hex << metadata->initial_address << std::endl;

    return segment;
  }

  virtual DescriptorPool* getDescPool() {
    return NULL;
  }

  void initMWCasDescriptorPool(SharedMemorySegment* segment, DescriptorPool** descPool)
  {
    Descriptor * poolDesc = (Descriptor*)((uintptr_t)segment->GetMapAddress() +
      sizeof(DescriptorPool::Metadata));
	std::cout << "descriptor addr:" << poolDesc << std::endl;

    // Ideally the descriptor pool is sized to the number of threads in the
    // benchmark to reduce need for new allocations, etc.
    *descPool = reinterpret_cast<DescriptorPool *>(
                         Allocator::Get()->Allocate(sizeof(DescriptorPool)));
    new(*descPool) DescriptorPool(
      FLAGS_descriptor_pool_size, FLAGS_threads, poolDesc, FLAGS_enable_stats);
  }


  void initFASSDescriptorPool(SharedMemorySegment* segment, FASASDescriptorPool** descPool)
  {
    FASASDescriptor * poolDesc = (FASASDescriptor*)((uintptr_t)segment->GetMapAddress() +
      sizeof(DescriptorPool::Metadata));
	std::cout << "fasas descriptor addr:" << poolDesc << std::endl;

    // Ideally the descriptor pool is sized to the number of threads in the
    // benchmark to reduce need for new allocations, etc.
    *descPool = reinterpret_cast<FASASDescriptorPool *>(
                         Allocator::Get()->Allocate(sizeof(FASASDescriptorPool)));
    new(*descPool) FASASDescriptorPool(
      FLAGS_descriptor_pool_size, FLAGS_threads, poolDesc, FLAGS_enable_stats);
  }



  

///////////////////////////////////////////////////////////////////////////////

  
  uint64_t previous_dump_run_ticks_;
  MwCASMetrics cumulative_stats_;
  std::atomic<uint64_t> total_success_;
  std::unordered_map<size_t, uint64_t> threadIdOpNumMap_;


///////////////////////////////////////////////////////////////////////////////
  bool isNewMem_;
};

}  // namespace pmwcas
