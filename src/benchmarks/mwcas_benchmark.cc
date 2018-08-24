// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#define NOMINMAX
#include <string>

#include "mwcas_benchmark.h"
#include "fetchStoreStore.h"
#include "RecoverMutex.h"

#include "util/core_local.h"
#include "util/random_number_generator.h"

using namespace pmwcas::benchmark;

DEFINE_string(benchmarks,
    "FASAS", "fetch store and store");
DEFINE_uint64(array_size, 100, "size of the word array for mwcas benchmark");
DEFINE_uint64(seed, 1234, "base random number generator seed, the thread index"
    "is added to this number to form the full seed");
DEFINE_uint64(word_count, 2, "number of words in the multi-word compare and"
    " swap");
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
DEFINE_int32(FASAS_BASE_TYPE, 1, "fetch store store base type ");
#ifdef PMEM
DEFINE_uint64(write_delay_ns, 0, "NVRAM write delay (ns)");
DEFINE_bool(emulate_write_bw, false, "Emulate write bandwidth");
DEFINE_bool(clflush, true, "Use CLFLUSH, instead of spinning delays."
  "write_dealy_ns and emulate_write_bw will be ignored.");
#endif

namespace pmwcas {

/// Maximum number of threads that the benchmark driver supports.
const size_t kMaxNumThreads = 64;

/// Dumps args in a format that can be extracted by an experiment script
void DumpArgs() {
  std::cout << "> Args shm_segment " << FLAGS_shm_segment.c_str() << std::endl;
  std::cout << "> Args threads " << FLAGS_threads << std::endl;
  std::cout << "> Args word_count " << FLAGS_word_count << std::endl;
  std::cout << "> Args array_size " << FLAGS_array_size << std::endl;
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

//////////////////////////////////////////////////////////////////////////////////////////////////
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
  }


  uint64_t previous_dump_run_ticks_;
  MwCASMetrics cumulative_stats_;
  std::atomic<uint64_t> total_success_;
};


//////////////////////////////////////////////////////////////////////////////////////////////////
struct MwCas : public BaseMwCas {
  void Setup(size_t thread_count) {
    // Ideally the descriptor pool is sized to the number of threads in the
    // benchmark to reduce need for new allocations, etc.
    descriptor_pool_ = reinterpret_cast<DescriptorPool*>(
                         Allocator::Get()->Allocate(sizeof(DescriptorPool)));
    Descriptor* pool_va = nullptr;
    std::string segname(FLAGS_shm_segment);
    //persistent_ = (segname.size() != 0);
    bool old = false;
#ifdef PMEM
    if(FLAGS_clflush) {
      NVRAM::InitializeClflush();
    } else {
      NVRAM::InitializeSpin(FLAGS_write_delay_ns, FLAGS_emulate_write_bw);
    }

    uint64_t size = sizeof(DescriptorPool::Metadata) +
                    sizeof(Descriptor) * FLAGS_descriptor_pool_size +  // descriptors area
                    sizeof(CasPtr) * FLAGS_array_size;  // data area

    SharedMemorySegment* segment = nullptr;
    auto s = Environment::Get()->NewSharedMemorySegment(segname, size, true,
        &segment);
    RAW_CHECK(s.ok() && segment, "Error creating memory segment");

    // Attach anywhere to extract the base address we used last time
    s = segment->Attach();
    RAW_CHECK(s.ok(), "cannot attach");

    //uintptr_t base_address = *(uintptr_t*)segment->GetMapAddress();
	uintptr_t base_address = *(uintptr_t*)((uintptr_t)segment->GetMapAddress() +
        sizeof(uint64_t));
    if(base_address) {
      std::cout << "base_address:" << std::hex << base_address << std::endl;
      old = true;
	  // An existing pool, with valid descriptors and data
      if(base_address != (uintptr_t)segment->GetMapAddress()) {
        segment->Detach();
        s = segment->Attach((void*)base_address);
        if(!s.ok()) {
          LOG(FATAL) << "Cannot attach to the segment with given base address";
        }
      }
      test_array_ = (CasPtr*)((uintptr_t)segment->GetMapAddress() +
        sizeof(DescriptorPool::Metadata) +
        sizeof(Descriptor) * FLAGS_descriptor_pool_size);
    } else {
      // New pool/data area, store this base address, pass it + meatadata_size
      // as desc pool va
      void* map_address = segment->GetMapAddress();
      DescriptorPool::Metadata *metadata = (DescriptorPool::Metadata*)map_address;
      metadata->descriptor_count = FLAGS_descriptor_pool_size;
      metadata->initial_address = (uintptr_t)map_address;
      test_array_ = (CasPtr*)((uintptr_t)map_address + sizeof(DescriptorPool::Metadata) +
                              sizeof(Descriptor) * FLAGS_descriptor_pool_size);
      LOG(INFO) << "Initialized new descriptor pool and data areas";
    }
    pool_va = (Descriptor*)((uintptr_t)segment->GetMapAddress() +
      sizeof(DescriptorPool::Metadata));

	std::cout << "descriptor begin addr:" << pool_va << ", array begin addr:" << test_array_ << std::endl;
#else
    // Allocate the thread array and initialize to consecutive even numbers
    test_array_ = reinterpret_cast<CasPtr*>(
      Allocator::Get()->Allocate(FLAGS_array_size * sizeof(CasPtr)));

    // Wrap the test array memory in an auto pointer for easy cleanup, keep the
    // raw pointer to avoid indirection during access
    test_array_guard_ = make_unique_ptr_t<CasPtr>(test_array_);

    for(uint32_t i = 0; i < FLAGS_array_size; ++i) {
      test_array_[i] = uint64_t(i * 4);
    }
#endif
    new(descriptor_pool_) DescriptorPool(
      FLAGS_descriptor_pool_size, FLAGS_threads, pool_va, FLAGS_enable_stats);
    // Recovering from an existing descriptor pool wouldn't cause the data area
    // to be re-initialized, rather this provides us the opportunity to do a
    // sanity check: no field should still point to a descriptor after recovery.
    if(old) {
      for(uint32_t i = 0; i < FLAGS_array_size; ++i) {
        //RAW_CHECK(((uint64_t)test_array_[i] & 0x1) == 0, "Wrong value");
		RAW_CHECK(!Descriptor::isDescriptorPtr((uint64_t)test_array_[i]), "Wrong value");
	  }
    }

    // Now we can start from a clean slate (perhaps not necessary)
    for(uint32_t i = 0; i < FLAGS_array_size; ++i) {
      test_array_[i] = uint64_t(i * 4);
    }
  }

  void Teardown() {
    if(FLAGS_array_size > 100) {
      return;
    }
    // Check the array for correctness
    unique_ptr_t<int64_t> found = alloc_unique<int64_t>(
      sizeof(int64_t) * FLAGS_array_size);

    for(uint32_t i = 0; i < FLAGS_array_size; i++) {
      found.get()[i] = 0;
    }

    for(uint32_t i = 0; i < FLAGS_array_size; i++) {
      uint32_t idx =
        uint32_t((uint64_t(test_array_[i]) % (4 * FLAGS_array_size)) / 4);
      LOG(INFO) << "idx=" << idx << ", pos=" << i << ", val=" <<
        (uint64_t)test_array_[i];

      if(!(idx >= 0 && idx < FLAGS_array_size)) {
        LOG(INFO) << "Invalid: pos=" << i << "val=" << uint64_t(test_array_[i]);
        continue;
      }
      found.get()[idx]++;
    }

    uint32_t missing = 0;
    uint32_t duplicated = 0;
    for(uint32_t i = 0; i < FLAGS_array_size; i++) {
      if(found.get()[i] == 0) missing++;
      if(found.get()[i] > 1) duplicated++;
    }

    CHECK(0 == missing && 0 == duplicated) <<
      "Failed final sanity test, missing: " << missing << " duplicated: " <<
      duplicated;
  }

  void Main(size_t thread_index) {
    CasPtr* address[10] = {0,0,0,0,0,0,0,0,0,0};
    CasPtr value[10] = {0,0,0,0,0,0,0,0,0,0};
    RandomNumberGenerator rng(FLAGS_seed + thread_index, 0, FLAGS_array_size);
    auto s = MwCASMetrics::ThreadInitialize();
    RAW_CHECK(s.ok(), "Error initializing thread");
    WaitForStart();
    const uint64_t kEpochThreshold = 100;
    uint64_t epochs = 0;

    descriptor_pool_->GetEpoch()->Protect();
	
    uint64_t n_success = 0;
    while(!IsShutdown()) {
      if(++epochs == kEpochThreshold) {
        descriptor_pool_->GetEpoch()->Unprotect();
        descriptor_pool_->GetEpoch()->Protect();
        epochs = 0;
      }

      // Pick a random word each time
      for(uint32_t i = 0; i < FLAGS_word_count; ++i) {
      retry:
        uint64_t idx = rng.Generate(FLAGS_array_size);
        for(uint32_t j = 0; j < i; ++j) {
          if(address[j] == reinterpret_cast<CasPtr*>(&test_array_[idx])) {
            goto retry;
          }
        }
        address[i] = reinterpret_cast<CasPtr*>(&test_array_[idx]);
        value[i] = test_array_[idx].GetValueProtected();
        CHECK(value[i] % (4 * FLAGS_array_size) >= 0 &&
            (value[i] % (4 * FLAGS_array_size)) / 4 < FLAGS_array_size);
      }

      Descriptor* descriptor = descriptor_pool_->AllocateDescriptor();
      CHECK_NOTNULL(descriptor);
      for(uint64_t i = 0; i < FLAGS_word_count; i++) {
        descriptor->AddEntry((uint64_t*)(address[i]), uint64_t(value[i]),
            uint64_t(value[FLAGS_word_count - 1 - i] + 4 * FLAGS_array_size));
      }
      bool status = false;
      status = descriptor->MwCAS();
      n_success += (status == true);
    }
    descriptor_pool_->GetEpoch()->Unprotect();
    auto n = total_success_.fetch_add(n_success, std::memory_order_seq_cst);
    LOG(INFO) << "Thread " << thread_index << " n_success: " <<
        n_success << ", " << n << ", total_success_:" << total_success_;
  }

  CasPtr* test_array_;
  //bool persistent_;
  unique_ptr_t<CasPtr> test_array_guard_;
  DescriptorPool* descriptor_pool_;

};

//////////////////////////////////////////////////////////////////////////////////////////////////
struct BaseFASASTest : public BaseMwCas {
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

  void Main(size_t thread_index) {
    auto s = MwCASMetrics::ThreadInitialize();
	RAW_CHECK(s.ok(), "Error initializing thread");

	RandomNumberGenerator rng(FLAGS_seed + thread_index, 0, FLAGS_array_size);
    DescriptorPool* descPool = getDescPool();

    WaitForStart();

    FetchStoreStore fetchStoreStore;

    const uint64_t kEpochThreshold = 100;
	uint64_t epochs = 0;
	descPool->GetEpoch()->Protect();
		
	uint64_t n_success = 0;
	uint64_t newValue = 0;
    while(!IsShutdown()) {     
	  if(++epochs == kEpochThreshold) {
	    descPool->GetEpoch()->Unprotect();
	    descPool->GetEpoch()->Protect();
	    epochs = 0;
      }

      uint64_t targetIdx = rng.Generate(FLAGS_array_size);
      doFASAS(targetIdx, thread_index, newValue, fetchStoreStore);
      newValue += 4;
	  n_success += 1;
    }

    descPool->GetEpoch()->Unprotect();
		
	auto n = total_success_.fetch_add(n_success, std::memory_order_seq_cst);
	LOG(INFO) << "Thread " << thread_index << " n_success: " <<
	            n_success << ", " << n << ", total_success_:" << total_success_;
  }

  virtual DescriptorPool* getDescPool() = 0;
  virtual void doFASAS(uint64_t targetIdx, size_t thread_index,
    uint64_t newValue, FetchStoreStore & fetchStoreStore) = 0;

  bool isNewMem_;
    
};

 
//////////////////////////////////////////////////////////////////////////////////////////////////
struct FASASTestByOrgPMwCas : public BaseFASASTest {
  void Setup(size_t thread_count) {
    if(FLAGS_clflush) {
      NVRAM::InitializeClflush();
    } else {
      NVRAM::InitializeSpin(FLAGS_write_delay_ns, FLAGS_emulate_write_bw);
    }

    std::string segname(FLAGS_shm_segment);
    uint64_t size = sizeof(DescriptorPool::Metadata) +
                    sizeof(Descriptor) * FLAGS_descriptor_pool_size +  // descriptors area
                    sizeof(CasPtr) * FLAGS_array_size + // share data area
                    sizeof(CasPtr) * FLAGS_threads;  // private data area
    SharedMemorySegment* segment = initSharedMemSegment(segname, size);

    shareArrayPtr_ = (CasPtr*)((uintptr_t)segment->GetMapAddress() +
        sizeof(DescriptorPool::Metadata) +
        sizeof(Descriptor) * FLAGS_descriptor_pool_size);
    privateArrayPtr_ = (CasPtr*)((uintptr_t)segment->GetMapAddress() +
        sizeof(DescriptorPool::Metadata) +
        sizeof(Descriptor) * FLAGS_descriptor_pool_size +
        sizeof(CasPtr) * FLAGS_array_size);
	std::cout << "share array begin addr:" << shareArrayPtr_ 
        << ", private array begin addr:" << privateArrayPtr_ << std::endl;

    initDescriptorPool(segment);

    for(uint32_t i = 0; i < FLAGS_array_size; ++i) {
      if(Descriptor::isDescriptorPtr((uint64_t)shareArrayPtr_[i])) {
        std::cout << "share varible is descriptor ptr:" << (uint64_t)shareArrayPtr_[i] << 
             ", i:" << i << ", addr:" << std::hex << (&(shareArrayPtr_[i])) << std::endl;
      }
      RAW_CHECK(!Descriptor::isDescriptorPtr((uint64_t)shareArrayPtr_[i]), "Wrong value");
    }

    for(uint32_t i = 0; i < FLAGS_threads; ++i) {
      if(Descriptor::isDescriptorPtr((uint64_t)privateArrayPtr_[i])) {
        std::cout << "private varible is descriptor ptr:" << (uint64_t)privateArrayPtr_[i] << 
             ", i:" << i << ", addr:" << std::hex << (&(privateArrayPtr_[i])) << std::endl;
      }
      RAW_CHECK(!Descriptor::isDescriptorPtr((uint64_t)privateArrayPtr_[i]), "Wrong value");
    }

    for(uint32_t i = 0; i < FLAGS_array_size; ++i) {
      shareArrayPtr_[i] = uint64_t(0);
    }

    for(uint32_t i = 0; i < FLAGS_threads; ++i) {
      privateArrayPtr_[i] = uint64_t(0);
    }
  }
  
  void initDescriptorPool(SharedMemorySegment* segment)
  {
    Descriptor * poolDesc = (Descriptor*)((uintptr_t)segment->GetMapAddress() +
      sizeof(DescriptorPool::Metadata));
	std::cout << "descriptor addr:" << poolDesc << std::endl;

    // Ideally the descriptor pool is sized to the number of threads in the
    // benchmark to reduce need for new allocations, etc.
    descPool_ = reinterpret_cast<DescriptorPool *>(
                         Allocator::Get()->Allocate(sizeof(DescriptorPool)));
    new(descPool_) DescriptorPool(
      FLAGS_descriptor_pool_size, FLAGS_threads, poolDesc, FLAGS_enable_stats);
  }

  virtual DescriptorPool* getDescPool() {
    return descPool_;
  }

  virtual void doFASAS(uint64_t targetIdx, size_t thread_index,
          uint64_t newValue, FetchStoreStore & fetchStoreStore) 
  {
    CasPtr* targetAddress = reinterpret_cast<CasPtr*>(&(shareArrayPtr_[targetIdx])); 
	CasPtr* storeAddress = reinterpret_cast<CasPtr*>(&privateArrayPtr_[thread_index]);
	fetchStoreStore.processByOrgMwcas(targetAddress, storeAddress, newValue, descPool_);
  }
	
  void Teardown() {
    for(uint32_t i = 0; i < FLAGS_array_size; i++) {
      RAW_CHECK(Descriptor::IsCleanPtr((uint64_t)shareArrayPtr_[i]), "share variable Wrong value");
      LOG(INFO) << "pos=" << i << ", val=" << (uint64_t)shareArrayPtr_[i];
      RAW_CHECK((uint64_t)shareArrayPtr_[i] % 4 == 0, "share value not multi of 4");
    }

    for(uint32_t i = 0; i < FLAGS_threads; i++) {
      LOG(INFO) << "private value:" << (uint64_t)privateArrayPtr_[i];
      RAW_CHECK(Descriptor::IsCleanPtr((uint64_t)privateArrayPtr_[i]), "private variable Wrong value");
      RAW_CHECK((uint64_t)privateArrayPtr_[i] % 4 == 0, "private value not multi of 4");
    }
  }

  CasPtr* shareArrayPtr_;
  CasPtr* privateArrayPtr_;

  DescriptorPool* descPool_;
} ;

//////////////////////////////////////////////////////////////////////////////////////////////////
struct FASASTest : public BaseFASASTest {
  void Setup(size_t thread_count) {
    if(FLAGS_clflush) {
      NVRAM::InitializeClflush();
    } else {
      NVRAM::InitializeSpin(FLAGS_write_delay_ns, FLAGS_emulate_write_bw);
    }

    std::string segname(FLAGS_shm_segment);
    uint64_t size = sizeof(DescriptorPool::Metadata) +
                    sizeof(FASASDescriptor) * FLAGS_descriptor_pool_size +  // descriptors area
                    sizeof(FASASCasPtr) * FLAGS_array_size + // share data area
                    sizeof(FASASCasPtr) * FLAGS_threads;  // private data area
    SharedMemorySegment* segment = initSharedMemSegment(segname, size);

    shareArrayPtr_ = (FASASCasPtr*)((uintptr_t)segment->GetMapAddress() +
        sizeof(DescriptorPool::Metadata) +
        sizeof(FASASDescriptor) * FLAGS_descriptor_pool_size);
    privateArrayPtr_ = (FASASCasPtr*)((uintptr_t)segment->GetMapAddress() +
        sizeof(DescriptorPool::Metadata) +
        sizeof(FASASDescriptor) * FLAGS_descriptor_pool_size +
        sizeof(FASASCasPtr) * FLAGS_array_size);
	std::cout << "share array begin addr:" << shareArrayPtr_ 
        << ", private array begin addr:" << privateArrayPtr_ << std::endl;

    initDescriptorPool(segment);

    // Recovering from an existing descriptor pool wouldn't cause the data area
    // to be re-initialized, rather this provides us the opportunity to do a
    // sanity check: no field should still point to a descriptor after recovery.
    for(uint32_t i = 0; i < FLAGS_array_size; ++i) {
      if(Descriptor::isDescriptorPtr((uint64_t)shareArrayPtr_[i])) {
        std::cout << "share varible is descriptor ptr:" << std::hex << (uint64_t)shareArrayPtr_[i] << 
            ", i:" << i << ", addr:" << std::hex << (&(shareArrayPtr_[i])) << std::endl;
      }
      RAW_CHECK(!Descriptor::isDescriptorPtr((uint64_t)shareArrayPtr_[i]), "Wrong value");
    }

    for(uint32_t i = 0; i < FLAGS_threads; ++i) {
      if(Descriptor::isDescriptorPtr((uint64_t)privateArrayPtr_[i])) {
        std::cout << "private varible is descriptor ptr:" << std::hex << (uint64_t)privateArrayPtr_[i] <<
            ", i:" << i << ", addr:" << std::hex << (&(privateArrayPtr_[i])) << std::endl;
      }
      RAW_CHECK(!Descriptor::isDescriptorPtr((uint64_t)privateArrayPtr_[i]), "Wrong value");
    }
    
    // Now we can start from a clean slate (perhaps not necessary)
    for(uint32_t i = 0; i < FLAGS_array_size; ++i) {
      shareArrayPtr_[i] = uint64_t(0);
    }

    for(uint32_t i = 0; i < FLAGS_threads; ++i) {
      privateArrayPtr_[i] = uint64_t(0);
    }
  }

  void initDescriptorPool(SharedMemorySegment* segment)
  {
    FASASDescriptor * poolDesc = (FASASDescriptor*)((uintptr_t)segment->GetMapAddress() +
      sizeof(DescriptorPool::Metadata));
	std::cout << "descriptor addr:" << poolDesc << std::endl;

    // Ideally the descriptor pool is sized to the number of threads in the
    // benchmark to reduce need for new allocations, etc.
    fasasDescPool_ = reinterpret_cast<FASASDescriptorPool *>(
                         Allocator::Get()->Allocate(sizeof(FASASDescriptorPool)));
    new(fasasDescPool_) FASASDescriptorPool(
      FLAGS_descriptor_pool_size, FLAGS_threads, poolDesc, FLAGS_enable_stats);
  }

  virtual void doFASAS(uint64_t targetIdx, size_t thread_index,
          uint64_t newValue, FetchStoreStore & fetchStoreStore) 
  {
    FASASCasPtr* targetAddress = reinterpret_cast<FASASCasPtr*>(&(shareArrayPtr_[targetIdx])); 
	FASASCasPtr* storeAddress = reinterpret_cast<FASASCasPtr*>(&(privateArrayPtr_[thread_index]));
    if(FLAGS_FASAS_BASE_TYPE == 1) {
     fetchStoreStore.process(targetAddress, storeAddress, newValue, fasasDescPool_);
    }
    else {
      fetchStoreStore.processByMwcas(targetAddress, storeAddress, newValue, fasasDescPool_);
    }
  }

  virtual DescriptorPool* getDescPool() {
    return fasasDescPool_;
  }       
	
  void Teardown() {
    for(uint32_t i = 0; i < FLAGS_array_size; i++) {
      RAW_CHECK(Descriptor::IsCleanPtr((uint64_t)shareArrayPtr_[i]), "share variable Wrong value");
      LOG(INFO) << "pos=" << i << ", val=" << (uint64_t)shareArrayPtr_[i];
      RAW_CHECK((uint64_t)shareArrayPtr_[i] % 4 == 0, "share value not multi of 4");
    }

    for(uint32_t i = 0; i < FLAGS_threads; i++) {
      LOG(INFO) << "private value:" << (uint64_t)privateArrayPtr_[i];
      RAW_CHECK(Descriptor::IsCleanPtr((uint64_t)privateArrayPtr_[i]), "private variable Wrong value");
      RAW_CHECK((uint64_t)privateArrayPtr_[i] % 4 == 0, "private value not multi of 4");
    }
  }

	
  FASASCasPtr* shareArrayPtr_;
  FASASCasPtr* privateArrayPtr_;

  FASASDescriptorPool* fasasDescPool_;
} ;

//////////////////////////////////////////////////////////////////////////////////////////////////
struct RecoverMutexTestBase : public BaseFASASTest {

  void Main(size_t thread_index) {
    auto s = MwCASMetrics::ThreadInitialize();
	RAW_CHECK(s.ok(), "Error initializing thread");

    QNode * node = nodePtr_ + thread_index;
    if(isNewMem_) {
        new(node) QNode();
    }
    LOG(ERROR) << "thread_index:" << thread_index << ", nodePtr:" << node << ", node prev:" 
        << node->prev << ", next:"
        << node->next << ", link:" << node->linked;
    mutexPtr_->setMyNode(node);

    DescriptorPool* descPool = getDescPool();
   
    WaitForStart();

    const uint64_t kEpochThreshold = 50;
	uint64_t epochs = 0;
	descPool->GetEpoch()->Protect();
		
	uint64_t n_success = 0;
    while(!IsShutdown()) {     
	  if(++epochs == kEpochThreshold) {
	    descPool->GetEpoch()->Unprotect();
	    descPool->GetEpoch()->Protect();
	    epochs = 0;
      }

      mutexPtr_->lock();
      changeValue_ = thread_index;
      mutexPtr_->unlock();
      
	  n_success += 1;
    }

    descPool->GetEpoch()->Unprotect();
		
	auto n = total_success_.fetch_add(n_success, std::memory_order_seq_cst);
	LOG(INFO) << "Thread " << thread_index << " n_success: " <<
	            n_success << ", " << n << ", total_success_:" << total_success_;
  }
	
  void Teardown() {
    std::cout << "tail:" << *(mutexPtr_->getTail()) << std::endl;
    std::cout << "changeValue_:" << changeValue_ << std::endl;

    for(uint32_t i = 0; i < FLAGS_threads; i++) {
        QNode * node = nodePtr_ + i;
        std::cout << "i:" << i << ", nodePtr:" << node << ", node prev:"
            << node->prev << ", next:" << node->next << ", link:" << node->linked << std::endl;

        //if(node->prev == NULL) {
            //RAW_CHECK((uint32_t)changeValue_ == i, "changeValue_ not right");
        //}
    }
  }

  virtual void doFASAS(uint64_t targetIdx, size_t thread_index,
          uint64_t newValue, FetchStoreStore & fetchStoreStore) {  
    throw "not support";
  }

  RecoverMutex* mutexPtr_; 
  QNode* nodePtr_;
  size_t changeValue_;
  
} ;

struct RecoverByOrgPMwCas : public RecoverMutexTestBase {

 void Setup(size_t thread_count) {
    if(FLAGS_clflush) {
      NVRAM::InitializeClflush();
    } else {
      NVRAM::InitializeSpin(FLAGS_write_delay_ns, FLAGS_emulate_write_bw);
    }

    std::string segname(FLAGS_shm_segment);
    uint64_t metaSize = sizeof(DescriptorPool::Metadata);
    uint64_t descriptorSize = sizeof(Descriptor) * FLAGS_descriptor_pool_size;
    uint64_t qnodePtrSize = sizeof(QNode *) * 1;
    uint64_t qnodeSize = sizeof(QNode) * FLAGS_threads;
    uint64_t size = metaSize + descriptorSize + qnodePtrSize + qnodeSize;

    std::cout << "RecoverByOrgPMwCas initSharedMemSegment size:" << std::dec << size 
        << ", meta size:" << std::dec << metaSize << ", descriptorSize:" << std::dec << descriptorSize 
        << ", qnodePtrSize:" << std::dec << qnodePtrSize << ", qnodeSize:" << std::dec << qnodeSize << std::endl;
    SharedMemorySegment* segment = initSharedMemSegment(segname, size);
    
    QNode ** tailPtr = (QNode**)((uintptr_t)segment->GetMapAddress() + metaSize + descriptorSize);
    nodePtr_ = (QNode*)((uintptr_t)segment->GetMapAddress() +
        metaSize + descriptorSize + qnodePtrSize );
	std::cout << "tailPtr:" << tailPtr << ", tail:" << (*tailPtr) << ", nodePtr:" << nodePtr_ << std::endl;
    for(uint32_t i = 0; i < FLAGS_threads; i++) {
        QNode * node = nodePtr_ + i;
        std::cout << "i:" << i << ", nodePtr:" << node << ", node prevPtr:"<< (&node->prev)
            << ", node prev:" << node->prev << ", node nextPtr:"<< (&node->next)
            << ", next:" << node->next << ", linkPtr:" << (&node->linked)
            << ", link:" << node->linked << std::endl;
    }

    initDescriptorPool(segment);

    RecoverMutexUsingOrgMwcas * mutexPtr = reinterpret_cast<RecoverMutexUsingOrgMwcas*>(
      Allocator::Get()->Allocate(sizeof(RecoverMutexUsingOrgMwcas)));
    new(mutexPtr) RecoverMutexUsingOrgMwcas(descPool_, tailPtr);
    mutexPtrGuard_ = make_unique_ptr_t<RecoverMutexUsingOrgMwcas>(mutexPtr);
    mutexPtr_ = mutexPtr;
    std::cout << "mutexPtr_:" << mutexPtr_ << std::endl;
  }
  
  void initDescriptorPool(SharedMemorySegment* segment)
  {
    Descriptor * poolDesc = (Descriptor*)((uintptr_t)segment->GetMapAddress() +
      sizeof(DescriptorPool::Metadata));
	std::cout << "descriptor addr:" << poolDesc << std::endl;

    // Ideally the descriptor pool is sized to the number of threads in the
    // benchmark to reduce need for new allocations, etc.
    descPool_ = reinterpret_cast<DescriptorPool *>(
                         Allocator::Get()->Allocate(sizeof(DescriptorPool)));
    new(descPool_) DescriptorPool(
      FLAGS_descriptor_pool_size, FLAGS_threads, poolDesc, FLAGS_enable_stats);
  }

  virtual DescriptorPool* getDescPool() {
    return descPool_;
  }
  
  unique_ptr_t<RecoverMutexUsingOrgMwcas> mutexPtrGuard_;
  DescriptorPool* descPool_;
};

struct RecoverNew : public RecoverMutexTestBase {

 void Setup(size_t thread_count) {
    if(FLAGS_clflush) {
      NVRAM::InitializeClflush();
    } else {
      NVRAM::InitializeSpin(FLAGS_write_delay_ns, FLAGS_emulate_write_bw);
    }

    std::string segname(FLAGS_shm_segment);
    uint64_t metaSize = sizeof(DescriptorPool::Metadata);
    uint64_t fasasDescriptorSize = sizeof(FASASDescriptor) * FLAGS_descriptor_pool_size;
    uint64_t qnodePtrSize = sizeof(QNode *) * 1;
    uint64_t qnodeSize = sizeof(QNode) * FLAGS_threads;
    uint64_t size = metaSize + fasasDescriptorSize + qnodePtrSize + qnodeSize;

    std::cout << "RecoverNew initSharedMemSegment size:" << std::dec << size 
        << ", meta size:" << std::dec << metaSize << ", fasasDescriptorSize:" << std::dec << fasasDescriptorSize 
        << ", qnodePtrSize:" << std::dec << qnodePtrSize << ", qnodeSize:" << std::dec << qnodeSize << std::endl;
    SharedMemorySegment* segment = initSharedMemSegment(segname, size);

    QNode ** tailPtr = (QNode**)((uintptr_t)segment->GetMapAddress() + metaSize + fasasDescriptorSize);
    nodePtr_ = (QNode*)((uintptr_t)segment->GetMapAddress() +
        metaSize + fasasDescriptorSize + qnodePtrSize );
	std::cout << "tailPtr:" << tailPtr << ", tail:" << (*tailPtr) << ", nodePtr:" << nodePtr_ << std::endl;
    initDescriptorPool(segment);

    RecoverMutexNew * mutexPtr = reinterpret_cast<RecoverMutexNew*>(
      Allocator::Get()->Allocate(sizeof(RecoverMutexNew)));
    new(mutexPtr) RecoverMutexNew(fasasDescPool_, tailPtr);
    mutexPtrGuard_ = make_unique_ptr_t<RecoverMutexNew>(mutexPtr);
    mutexPtr_ = mutexPtr;
    std::cout << "mutexPtr_:" << mutexPtr_ << std::endl;
  }
  
  void initDescriptorPool(SharedMemorySegment* segment)
  {
    FASASDescriptor * poolDesc = (FASASDescriptor*)((uintptr_t)segment->GetMapAddress() +
      sizeof(DescriptorPool::Metadata));
	std::cout << "descriptor addr:" << poolDesc << std::endl;

    // Ideally the descriptor pool is sized to the number of threads in the
    // benchmark to reduce need for new allocations, etc.
    fasasDescPool_ = reinterpret_cast<FASASDescriptorPool *>(
                         Allocator::Get()->Allocate(sizeof(FASASDescriptorPool)));
    new(fasasDescPool_) FASASDescriptorPool(
      FLAGS_descriptor_pool_size, FLAGS_threads, poolDesc, FLAGS_enable_stats);
  }

  virtual DescriptorPool* getDescPool() {
    return fasasDescPool_;
  }
  
  unique_ptr_t<RecoverMutexNew> mutexPtrGuard_;
  FASASDescriptorPool* fasasDescPool_;
};


//////////////////////////////////////////////////////////////////////////////////////////////////
}// namespace pmwcas

using namespace pmwcas;

void doRunRecoverMutex(RecoverMutexTestBase & test) {
  std::cout << "Starting mutex recover benchmark..." << std::endl;
  test.Run(FLAGS_threads, FLAGS_seconds,
      static_cast<AffinityPattern>(FLAGS_affinity),
      FLAGS_metrics_dump_interval);

  printf("multiple cas: opCnt:%.0f, sec:%.3f,  %.2f ops/sec\n", (double)test.GetOperationCount(), 
  	  test.GetRunSeconds(),
      (double)test.GetOperationCount() / test.GetRunSeconds());
  printf("lock and unlock: totalSuc:%.0f, sec:%.3f, %.2f successful ops/sec\n",
  	(double)test.GetTotalSuccess(),
  	 test.GetRunSeconds(),
     (double)test.GetTotalSuccess() / test.GetRunSeconds());
}

Status runRecoverMutexTest() {
  if(FLAGS_FASAS_BASE_TYPE == 0) {
    RecoverByOrgPMwCas test;
    doRunRecoverMutex(test);
  }
  else {
    RecoverNew test;
    doRunRecoverMutex(test);
  }
  
  return Status::OK();
}

void doRunFetchStoreAndStore(BaseMwCas & test) {
  std::cout << "Starting fetch store benchmark..." << std::endl;
  test.Run(FLAGS_threads, FLAGS_seconds,
      static_cast<AffinityPattern>(FLAGS_affinity),
      FLAGS_metrics_dump_interval);

  printf("multiple cas: opCnt:%.0f, sec:%.3f,  %.2f ops/sec\n", (double)test.GetOperationCount(), 
  	  test.GetRunSeconds(),
      (double)test.GetOperationCount() / test.GetRunSeconds());
  printf("fetch store: totalSuc:%.0f, sec:%.3f, %.2f successful updates/sec\n",
  	(double)test.GetTotalSuccess(),
  	 test.GetRunSeconds(),
     (double)test.GetTotalSuccess() / test.GetRunSeconds());
}

Status RunMwCas() {
  MwCas test{};
  std::cout << "Starting benchmark..." << std::endl;
  test.Run(FLAGS_threads, FLAGS_seconds,
      static_cast<AffinityPattern>(FLAGS_affinity),
      FLAGS_metrics_dump_interval);

  printf("mwcas: opCnt:%.0f, sec:%.3f,  %.2f ops/sec\n", (double)test.GetOperationCount(), 
  	  test.GetRunSeconds(),
      (double)test.GetOperationCount() / test.GetRunSeconds());
  printf("mwcas: totalSuc:%.0f, sec:%.3f, %.2f successful updates/sec\n",(double)test.GetTotalSuccess(),
  	  test.GetRunSeconds(),
      (double)test.GetTotalSuccess() / test.GetRunSeconds());
  return Status::OK();
}

Status runFetchStoreAndStore() {
  if(FLAGS_FASAS_BASE_TYPE == 0) {
    FASASTestByOrgPMwCas test;
    doRunFetchStoreAndStore(test);
  }
  else {
    FASASTest test;
    doRunFetchStoreAndStore(test);
  }
  
  return Status::OK();
}

void RunBenchmark() {
  std::string benchmark_name{};
  std::stringstream benchmark_stream(FLAGS_benchmarks);
  DumpArgs();

  while(std::getline(benchmark_stream, benchmark_name, ',')) {
    Status s{};
    if("mwcas" == benchmark_name) {
      s = RunMwCas();
    } 
	else if("FASAS" == benchmark_name) {
        s = runFetchStoreAndStore();
	}
    else if("RME" == benchmark_name) {
        s = runRecoverMutexTest();
	}
	else {
      fprintf(stderr, "unknown benchmark name: %s\n", benchmark_name.c_str());
    }

    LOG_IF(FATAL, !s.ok()) << "Benchmark failed. " << s.ToString();
  }
}

int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  google::ParseCommandLineFlags(&argc, &argv, true);
  
  FLAGS_log_dir = "./";
  
#ifdef WIN32
  pmwcas::InitLibrary(pmwcas::DefaultAllocator::Create,
                      pmwcas::DefaultAllocator::Destroy,
                      pmwcas::WindowsEnvironment::Create,
                      pmwcas::WindowsEnvironment::Destroy);
#else
  pmwcas::InitLibrary(pmwcas::TlsAllocator::Create,
                      pmwcas::TlsAllocator::Destroy,
                      pmwcas::LinuxEnvironment::Create,
                      pmwcas::LinuxEnvironment::Destroy);
#endif
  RunBenchmark();
  return 0;
}
