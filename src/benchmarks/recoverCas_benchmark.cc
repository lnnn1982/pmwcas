#define NOMINMAX

#include <unordered_map>
#include <string>
#include "util/random_number_generator.h"
#include "mwcas_benchmark.h"
#include "fetchStoreStore.h"
#include "RecoverCAS.h"


using namespace pmwcas::benchmark;

DEFINE_uint64(array_size, 100, "size of the word array for mwcas benchmark");
DEFINE_int32(RCAS_TYPE, 0, "recover cas type");

namespace pmwcas {

struct RecoverCasTestBase : public BaseMwCas {
    void Setup(size_t thread_count) {
        NVRAM::InitializeClflush();

        uint64_t metaSize = sizeof(DescriptorPool::Metadata);
        uint64_t descriptorSize = getDescriptorSize();
        uint64_t arraySize = getTestArraySize();
        uint64_t extraSize = getExtraSize();

        uint64_t size = metaSize + descriptorSize + arraySize + extraSize;
        std::cout << "RecoverCasTestBase initSharedMemSegment size:" << std::dec << size 
            << ", meta size:" << std::dec << metaSize 
            << ", descriptorSize:" << std::dec << descriptorSize 
            << ", size of FASASDescriptor:" << std::dec << sizeof(FASASDescriptor)
            << ", size of BaseDescriptor:" << std::dec << sizeof(BaseDescriptor)
            << ", size of BaseWordDescriptor:" << std::dec << sizeof(BaseDescriptor::BaseWordDescriptor)
            << ", size of Descriptor:" << std::dec << sizeof(Descriptor)
            << ", size of OptimizedFASASDescriptor:" << std::dec << sizeof(OptmizedFASASDescriptor)
            << ", arraySize:" << std::dec << arraySize 
            << ", extraSize:" << std::dec << extraSize << std::endl;

        std::string segname(FLAGS_shm_segment);
        SharedMemorySegment* segment = initSharedMemSegment(segname, size);

        initDescriptorPool(segment);
        initTestArray(segment, metaSize+descriptorSize);
        initExtra(segment, metaSize+descriptorSize+arraySize);
        
        for(int i = 0; i < FLAGS_threads; i++) {
            threadIdOpNumMap_[i] = 0;
        }
    }

    void initTestArray(SharedMemorySegment* segment, uint64_t extraOffset) {
        test_array_ = (uint64_t*)((uintptr_t)segment->GetMapAddress() + extraOffset);
        std::cout << "test_array_:" << test_array_ << std::endl;

        for(uint32_t i = 0; i < FLAGS_array_size; ++i) {
            test_array_[i] = (i * 4);
        }

        //printTestArray();
    }

    void printTestArray() {
        for(uint32_t i = 0; i < FLAGS_array_size; ++i) {
            std::cout << "i:" << i << ", testArray:" << 
                std::dec << test_array_[i] << std::endl;
        }
    }

    uint64_t getTestArraySize() {
        return sizeof(FASASCasPtr) * FLAGS_array_size; ;
    }

    void Teardown() {
        checkAfterTest();
    }

    virtual void checkAfterTest() {
        //printTestArray();
        // Check the array for correctness
        unique_ptr_t<int64_t> found = alloc_unique<int64_t>(
            sizeof(int64_t) * FLAGS_array_size);

        for(uint32_t i = 0; i < FLAGS_array_size; i++) {
            found.get()[i] = 0;
        }

        for(uint32_t i = 0; i < FLAGS_array_size; i++) {
            uint64_t value = getTestArrayValue(i);
            //std::cout << "i:" << i << ", value:" << std::dec << value << std::endl;
        
            uint32_t idx =
                uint32_t((value % (4 * FLAGS_array_size)) / 4);
            CHECK(idx >= 0 && idx < FLAGS_array_size) <<
              "idx value error: " << idx;

            //std::cout << "i:" << i << ", idx:" << std::dec << idx << std::endl;
            
            found.get()[idx]++;
        }

        uint32_t missing = 0;
        uint32_t duplicated = 0;
        for(uint32_t i = 0; i < FLAGS_array_size; i++) {
            if(found.get()[i] == 0)  {
                missing++;
                std::cout << "missing:" << i << std::endl;
            }
            if(found.get()[i] > 1) {
                duplicated++;
                std::cout << "duplicated:" << i << std::endl;
            }
        }

        CHECK(0 == missing && 0 == duplicated) <<
            "Failed final sanity test, missing: " << missing << " duplicated: " <<
            duplicated;
    }

    void Main(size_t thread_index) {
        auto s = MwCASMetrics::ThreadInitialize();
	    RAW_CHECK(s.ok(), "Error initializing thread");

        RandomNumberGenerator rng(FLAGS_seed + thread_index, 0, FLAGS_array_size);
        WaitForStart();
        
        if(getDescPool() != NULL) {
            getDescPool()->GetEpoch()->Protect();
        }

        uint64_t n_success = 0;
        while(!IsShutdown()) {
            uint64_t targetIdx = rng.Generate(FLAGS_array_size);
            bool status = doRecoverCAS(thread_index, targetIdx);
            n_success += (status == true);
        }

        if(getDescPool() != NULL) {
            getDescPool()->GetEpoch()->Unprotect();
        }

        (threadIdOpNumMap_[thread_index]) = n_success;
        auto n = total_success_.fetch_add(threadIdOpNumMap_[thread_index], std::memory_order_seq_cst);
	    //LOG(INFO) << "Thread " << thread_index << " n_success: " <<
	            //threadIdOpNumMap_[thread_index] << ", " << n << ", total_success_:" << total_success_;
    }

    virtual uint64_t getDescriptorSize() = 0;
    virtual void initDescriptorPool(SharedMemorySegment* segment) = 0;

    virtual uint64_t getExtraSize() = 0;
    virtual void initExtra(SharedMemorySegment* segment, uint64_t extraOffset) = 0;
    
    virtual bool doRecoverCAS(uint64_t thread_index, uint64_t targetIdx) = 0;

    virtual uint64_t getTestArrayValue(uint32_t arrayIdx) = 0;


    FetchStoreStore fetchStoreStore_;
    uint64_t * test_array_;
};

struct RecoverCasByFASAS : public RecoverCasTestBase {

    uint64_t getDescriptorSize() {
        return sizeof(FASASDescriptor) * FLAGS_descriptor_pool_size;
    }

    void initDescriptorPool(SharedMemorySegment* segment) {
        initFASSDescriptorPool(segment, &fasasDescPool_);
    }

    virtual uint64_t getExtraSize() {
        return sizeof(uint64_t) * FLAGS_threads;
    }
    
    virtual void initExtra(SharedMemorySegment* segment, uint64_t extraOffset) {
        privatePtr_ = (uint64_t*)((uintptr_t)segment->GetMapAddress() 
            + extraOffset);
        std::cout << "privatePtr_:" << privatePtr_ << std::endl;
    }

    virtual BaseDescriptorPool* getDescPool() {
        return fasasDescPool_;
    }
    
    virtual bool doRecoverCAS(uint64_t thread_index, uint64_t targetIdx) 
    {
        FASASCasPtr* privateAddr = (FASASCasPtr*)(privatePtr_+targetIdx);
        
        FASASCasPtr* shareAddr = (FASASCasPtr*)test_array_+targetIdx;
        uint64_t oldVal = shareAddr->getValueProtectedOfSharedVar();
        uint64_t newVal = oldVal + 4 * FLAGS_array_size;
        
        bool ret = fetchStoreStore_.recoverCas(shareAddr, privateAddr,
            oldVal, newVal, fasasDescPool_);
        if(ret) {
            RAW_CHECK(*(privatePtr_+targetIdx) == 1, "privateValue is wrong");
        }

        return ret;
    }


    virtual uint64_t getTestArrayValue(uint32_t arrayIdx) {
        FASASCasPtr* ptr = (FASASCasPtr*)(test_array_+arrayIdx);
        return ptr->getValueProtectedOfSharedVar();
    }

    FASASDescriptorPool* fasasDescPool_;
    uint64_t * privatePtr_;
    
};

struct RecoverCasByOptimizedFASAS : public RecoverCasTestBase {

    uint64_t getDescriptorSize() {
        return sizeof(OptmizedFASASDescriptor) * FLAGS_descriptor_pool_size;
    }

    void initDescriptorPool(SharedMemorySegment* segment) {
        OptmizedFASASDescriptor * poolDesc = (OptmizedFASASDescriptor*)((
            uintptr_t)segment->GetMapAddress() +
            sizeof(DescriptorPool::Metadata));
	    std::cout << "fasas descriptor addr:" << poolDesc << std::endl;

        fasasDescPool_ = reinterpret_cast<OptmizedFASASDescriptorPool *>(
                         Allocator::Get()->Allocate(sizeof(OptmizedFASASDescriptorPool)));
        new(fasasDescPool_) OptmizedFASASDescriptorPool(
            FLAGS_descriptor_pool_size, FLAGS_threads, poolDesc, nullptr, FLAGS_enable_stats);
    }

    virtual uint64_t getExtraSize() {
        return sizeof(uint64_t) * FLAGS_threads;
    }
    
    virtual void initExtra(SharedMemorySegment* segment, uint64_t extraOffset) {
        privatePtr_ = (uint64_t*)((uintptr_t)segment->GetMapAddress() 
            + extraOffset);
        std::cout << "privatePtr_:" << privatePtr_ << std::endl;
    }

    virtual BaseDescriptorPool* getDescPool() {
        return NULL;
    }
    
    virtual bool doRecoverCAS(uint64_t thread_index, uint64_t targetIdx) 
    {
        uint64_t * shareAddr = test_array_+targetIdx;
        uint64_t oldVal = fetchStoreStore_.read(shareAddr,
            (uint32_t)targetIdx, (uint16_t)thread_index, fasasDescPool_);
        uint64_t newVal = oldVal + 4 * FLAGS_array_size;

        uint64_t * privatePtr = privatePtr_+thread_index;
        *privatePtr = 0;
        bool ret = fetchStoreStore_.recoverCas(shareAddr, privatePtr,
            oldVal, newVal, (uint32_t)targetIdx, (uint16_t)thread_index, fasasDescPool_);    
        if(ret) {
            RAW_CHECK(*privatePtr == 1, "privateValue is wrong");
        }

        return ret;
    }


    virtual uint64_t getTestArrayValue(uint32_t arrayIdx) {
        return fetchStoreStore_.read(test_array_+arrayIdx,
            (uint32_t)arrayIdx, 0, fasasDescPool_);
    }

    OptmizedFASASDescriptorPool * fasasDescPool_;
    uint64_t * privatePtr_;
    
};

struct RecoverCasBySeq : public RecoverCasTestBase {
    uint64_t getDescriptorSize() {
        return 0;
    }

    void initDescriptorPool(SharedMemorySegment* segment) {
    }

    virtual BaseDescriptorPool* getDescPool() {
        return nullptr;
    }

    uint64_t getExtraSize() {
        return sizeof(uint64_t) * FLAGS_array_size * FLAGS_threads;
    }

    void initExtra(SharedMemorySegment* segment, uint64_t extraOffset) {
        processInfoArray_ = (uint64_t*)((uintptr_t)segment->GetMapAddress() 
            + extraOffset);
        std::cout << "processInfoArray_:" << processInfoArray_ << std::endl;

        casOpArray_ = reinterpret_cast<RCAS*>(Allocator::Get()->Allocate(
            sizeof(RCAS)*FLAGS_array_size));
        for(uint32_t i = 0; i < FLAGS_array_size; i++) {
            new(casOpArray_+i) RCAS(test_array_+i, 
                processInfoArray_+i*FLAGS_threads, FLAGS_threads);
        }
        std::cout << "casOpArray_:" << casOpArray_ << std::endl;
    }

    virtual bool doRecoverCAS(uint64_t thread_index, uint64_t targetIdx) 
    {
        RCAS * casOp = casOpArray_+ targetIdx;
        uint64_t oldVal = casOp->readValue();
        uint64_t newVal = (oldVal + 4 * FLAGS_array_size)&(RCAS::valueFlg);
        //uint64_t newVal = 100;

        uint64_t seq = 4;
        bool ret = casOp->Cas(oldVal, (uint64_t)newVal, seq, thread_index);
        return ret;
    }

    virtual uint64_t getTestArrayValue(uint32_t arrayIdx) {
        return (casOpArray_+arrayIdx)->readValue();;
    }
    
    uint64_t * processInfoArray_;
    RCAS * casOpArray_;
};


}

//////////////////////////////////////////////////////////////////////////////////////////////////
using namespace pmwcas;

void doTest(RecoverCasTestBase & test) {
    std::cout << "Starting Recover CAS benchmark..." << std::endl;
    test.Run(FLAGS_threads, FLAGS_seconds,
        static_cast<AffinityPattern>(FLAGS_affinity),
        FLAGS_metrics_dump_interval);
    printf("multiple cas: opCnt:%.0f, sec:%.3f,  %.2f ops/sec\n", (double)test.GetOperationCount(), 
  	  test.GetRunSeconds(),  (double)test.GetOperationCount() / test.GetRunSeconds());
  printf("recover cas: totalSuc:%.0f, sec:%.3f, %.2f successful updates/sec\n",
  	(double)test.GetTotalSuccess(),
  	 test.GetRunSeconds(),
     (double)test.GetTotalSuccess() / test.GetRunSeconds());
 
}

void runBenchmark() {
    DumpArgs();
    std::cout << "> Args RCAS_TYPE " << FLAGS_RCAS_TYPE << std::endl;
    std::cout << "> Args array_size " << FLAGS_array_size << std::endl;

    if(FLAGS_RCAS_TYPE == 0) {
        std::cout << "************Recover CAS using FASAS***************" << std::endl;
        RecoverCasByFASAS test;
        doTest(test);
    }
    else if (FLAGS_RCAS_TYPE == 1){
        std::cout << "************Recover CAS using optimised FASAS***************" << std::endl;
        RecoverCasByOptimizedFASAS test;
        doTest(test);

    }
    else if (FLAGS_RCAS_TYPE == 2){
        std::cout << "************Recover CAS using seq***************" << std::endl;
        RecoverCasBySeq test;
        doTest(test);

    }
}

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    google::ParseCommandLineFlags(&argc, &argv, true);
  
    FLAGS_log_dir = "./";
  
#ifdef WIN32
    return;
#else
    pmwcas::InitLibrary(pmwcas::TlsAllocator::Create,
                      pmwcas::TlsAllocator::Destroy,
                      pmwcas::LinuxEnvironment::Create,
                      pmwcas::LinuxEnvironment::Destroy);
#endif
    runBenchmark();
    return 0;
}
