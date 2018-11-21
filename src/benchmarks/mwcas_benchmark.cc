// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#define NOMINMAX
#include <string>
#include <unordered_map>

#include "mwcas_benchmark.h"
#include "fetchStoreStore.h"
#include "RecoverMutex.h"

#include "util/core_local.h"
#include "util/random_number_generator.h"

using namespace pmwcas::benchmark;

DEFINE_string(benchmarks, "FASAS", "fetch store and store");
DEFINE_uint64(array_size, 100, "size of the word array for mwcas benchmark");
DEFINE_uint64(word_count, 2, "number of words in the multi-word compare and"
    " swap");
DEFINE_int32(FASAS_BASE_TYPE, 1, "fetch store store base type ");


namespace pmwcas {

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
	  /*if(++epochs == kEpochThreshold) {
	    descPool->GetEpoch()->Unprotect();
	    descPool->GetEpoch()->Protect();
	    epochs = 0;
      }*/

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


  virtual void doFASAS(uint64_t targetIdx, size_t thread_index,
    uint64_t newValue, FetchStoreStore & fetchStoreStore) = 0;

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
struct RecoverMutexTestBase : public BaseMwCas {
  void Setup(size_t thread_count) {
    if(FLAGS_clflush) {
      NVRAM::InitializeClflush();
    } else {
      NVRAM::InitializeSpin(FLAGS_write_delay_ns, FLAGS_emulate_write_bw);
    }

#ifdef FETCH_WAIT
    std::cout << "using fetch wait" << std::endl;
#endif

    uint64_t metaSize = sizeof(DescriptorPool::Metadata);
    uint64_t descriptorSize = getDescriptorSizeSize();
    //every tail is 8 bytes, so adding 56 bytes to make it cache line size
    //array size of tail ptr
    uint64_t qnodePtrSize = (sizeof(QNode *)+56) * FLAGS_array_size;
    uint64_t qnodeSize = sizeof(QNode) * FLAGS_threads;
    uint64_t size = metaSize + descriptorSize + qnodePtrSize + qnodeSize;
    std::cout << "RecoverMutexTestBase initSharedMemSegment size:" << std::dec << size 
        << ", meta size:" << std::dec << metaSize 
        << ", descriptorSize:" << std::dec << descriptorSize 
        << ", qnodePtrSize:" << std::dec << qnodePtrSize 
        << ", qnodeSize:" << std::dec << qnodeSize << std::endl;

    std::string segname(FLAGS_shm_segment);
    SharedMemorySegment* segment = initSharedMemSegment(segname, size);
    initDescriptorPool(segment);
    initNodePtr(segment, metaSize + descriptorSize + qnodePtrSize);

    QNode ** tailPtr = (QNode**)((uintptr_t)segment->GetMapAddress() + metaSize + descriptorSize);
    std::cout << "tailPtr:" << tailPtr << std::endl;
    initMutexPtr(tailPtr);

    setInitialValue();
    
    for(int i = 0; i < FLAGS_threads; i++) {
        threadIdOpNumMap_[i] = 0;
    }

    getCurrentMhz();
  }

  void getCurrentMhz() {
    uint64_t start = __rdtsc();
    sleep(1);
    uint64_t end = __rdtsc();

    cpu_freq_GHz_ = (double)(end - start)/1000000000;

    std::cout << "cpu_freq_GHz_:" << cpu_freq_GHz_ << std::endl;

    for(int i = 0; i < FLAGS_threads; i++) {
        threadIdRtdscMap_[i];
    }
  }

  virtual uint64_t getDescriptorSizeSize() = 0;
  virtual void initDescriptorPool(SharedMemorySegment* segment) = 0;
  virtual void initMutexPtr(QNode ** tailPtr) = 0;

  void setInitialValue() {
    initialValue_ = 7;
    std::cout << "initialValue_:" << initialValue_ << std::endl;

    changeValuePtr_ = reinterpret_cast<size_t *>(
      Allocator::Get()->Allocate(FLAGS_array_size * sizeof(size_t)));
    for(int i = 0; i < FLAGS_array_size; i++) {
        *(changeValuePtr_+i) = 7;
    }
  }

  void initNodePtr(SharedMemorySegment* segment, uint64_t size) {
    nodePtr_ = (QNode*)((uintptr_t)segment->GetMapAddress() + size);
    printNode();
  }
  
  void printNode() {
    for(uint32_t i = 0; i < FLAGS_threads; i++) {
        QNode * node = nodePtr_ + i;
        std::cout << "i:" << i << ", nodePtr:" << node << ", node prevPtr:"<< (&node->prev)
            << ", node prev:" << node->prev << ", node nextPtr:"<< (&node->next)
            << ", next:" << node->next << ", linkPtr:" << (&node->linked)
            << ", link:" << node->linked << std::endl;
    }
  }

  void Main(size_t thread_index) {
    auto s = MwCASMetrics::ThreadInitialize();
	RAW_CHECK(s.ok(), "Error initializing thread");

    RandomNumberGenerator rng(FLAGS_seed + thread_index, 0, FLAGS_array_size);

    QNode * node = nodePtr_ + thread_index;
    if(isNewMem_) {
        new(node) QNode();
    }
    /*LOG(ERROR) << "thread_index:" << thread_index << ", nodePtr:" 
        << node << ", node prev:" << node->prev << ", next:"
        << node->next << ", link:" << node->linked;*/

    for(int i = 0; i < FLAGS_array_size; i++) {
        getRecoverMutex(i)->setMyNode(node);
    }
    
    DescriptorPool* descPool = getDescPool();
   
    WaitForStart();

	//uint64_t epochs = 0;
	descPool->GetEpoch()->Protect();
	uint64_t n_success = 0;
	uint64_t targetIdx = 0;
    while(!IsShutdown()) {
      if(FLAGS_array_size != 1) {
        targetIdx = rng.Generate(FLAGS_array_size);
      }

      getRecoverMutex(targetIdx)->lock();
      (*(changeValuePtr_+targetIdx))++;
      (*(changeValuePtr_+targetIdx))--;
      getRecoverMutex(targetIdx)->unlock();
      
	  n_success += 1;
      //(threadIdOpNumMap_[thread_index]) = n_success;

      //effect performance
      //threadIdRtdscMap_[thread_index].push_back(__rdtsc());
    }
    descPool->GetEpoch()->Unprotect();
    (threadIdOpNumMap_[thread_index]) = n_success;
		
	auto n = total_success_.fetch_add(threadIdOpNumMap_[thread_index], std::memory_order_seq_cst);
	//LOG(INFO) << "Thread " << thread_index << " n_success: " <<
	            //threadIdOpNumMap_[thread_index] << ", " << n << ", total_success_:" << total_success_;
  }

  virtual RecoverMutex * getRecoverMutex(int i) = 0;
  
  void Teardown() {      
    printNode();
    for(int i = 0; i < FLAGS_array_size; i++) {
        if(*(changeValuePtr_+i) != initialValue_) {
            std::cout << "wrong change value. value:" << (*(changeValuePtr_+i))
                << ", i:" << i << std::endl;
        }

        RAW_CHECK(*(changeValuePtr_+i) == initialValue_, "changeValue_ not right");
    }

    recordLantency();
  }

  void recordLantency() {
    uint64_t count = 1;
    for(int i = 0; i < FLAGS_threads; i++) {
        std::vector<uint64_t> const & oneThreadVec = threadIdRtdscMap_[i];
        std::cout << "i:" << i << ", rtdsc size:" << oneThreadVec.size() << std::endl;
        uint64_t prevRtdsc = 0;
        for(uint64_t oneRtdsc : oneThreadVec) {
           if(prevRtdsc != 0) {
               double nanoSecs = (oneRtdsc - prevRtdsc)/cpu_freq_GHz_;
               LOG(INFO) << count++ << "," << nanoSecs;               
           }

           prevRtdsc = oneRtdsc;
        }
    }
  }

  virtual void doFASAS(uint64_t targetIdx, size_t thread_index,
          uint64_t newValue, FetchStoreStore & fetchStoreStore) {  
    throw "not support";
  }


  QNode* nodePtr_;
  size_t * changeValuePtr_;
  size_t initialValue_;
  double cpu_freq_GHz_;
  std::unordered_map<size_t, std::vector<uint64_t> > threadIdRtdscMap_;
  
} ;

struct RecoverByOrgPMwCas : public RecoverMutexTestBase {

 void initMutexPtr(QNode ** tailPtr) {
    mutexPtr_ = reinterpret_cast<RecoverMutexUsingOrgMwcas*>(
      Allocator::Get()->Allocate(sizeof(RecoverMutexUsingOrgMwcas)*FLAGS_array_size));
    for(int i = 0; i < FLAGS_array_size; i++) {
      //8 is for cache padding; original size is 8 bytes, adding 56 bytes
      new(mutexPtr_+i) RecoverMutexUsingOrgMwcas(descPool_, tailPtr+i*8);
    }

    std::cout << "mutexPtr_:" << mutexPtr_ << std::endl;
  }

  uint64_t getDescriptorSizeSize() {
    return sizeof(Descriptor) * FLAGS_descriptor_pool_size;
  }

  RecoverMutex * getRecoverMutex(int i) {
    return mutexPtr_+i;
  }

  void initDescriptorPool(SharedMemorySegment* segment)
  {
    initMWCasDescriptorPool(segment, &descPool_);
  }

  virtual DescriptorPool* getDescPool() {
    return descPool_;
  }

  DescriptorPool* descPool_;
  RecoverMutexUsingOrgMwcas* mutexPtr_;
};

struct RecoverNew : public RecoverMutexTestBase {
 void initMutexPtr(QNode ** tailPtr) {
    mutexPtr_ = reinterpret_cast<RecoverMutexNew*>(
      Allocator::Get()->Allocate(sizeof(RecoverMutexNew)*FLAGS_array_size));
    for(int i = 0; i < FLAGS_array_size; i++) {
      new(mutexPtr_+i) RecoverMutexNew(fasasDescPool_, tailPtr+i*8);
    }
    std::cout << "mutexPtr_:" << mutexPtr_ << std::endl;
  }
 
  uint64_t getDescriptorSizeSize() {
    return sizeof(FASASDescriptor) * FLAGS_descriptor_pool_size;
  }

  RecoverMutex * getRecoverMutex(int i) {
    return mutexPtr_+i;
  }

  void initDescriptorPool(SharedMemorySegment* segment)
  {
    initFASSDescriptorPool(segment, &fasasDescPool_);
  }

  virtual DescriptorPool* getDescPool() {
    return fasasDescPool_;
  }
  
  unique_ptr_t<RecoverMutexNew> mutexPtrGuard_;
  FASASDescriptorPool* fasasDescPool_;
  RecoverMutexNew* mutexPtr_;
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
  std::cout << "> Args word_count " << FLAGS_word_count << std::endl;
  std::cout << "> Args array_size " << FLAGS_array_size << std::endl;
  
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
