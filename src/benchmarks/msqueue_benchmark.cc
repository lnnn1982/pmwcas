#define NOMINMAX

#include <string>
#include "mwcas_benchmark.h"
#include "MSQueue.h"


using namespace pmwcas::benchmark;

DEFINE_uint64(node_size, 1048576, "");
DEFINE_uint64(queue_impl_type, 0, "");
DEFINE_uint64(queue_op_type, 0, "");

namespace pmwcas {

struct MSQueueTestBase : public BaseMwCas {
    void Setup(size_t thread_count) {
        if(FLAGS_clflush) {
          NVRAM::InitializeClflush();
        } else {
          NVRAM::InitializeSpin(FLAGS_write_delay_ns, FLAGS_emulate_write_bw);
        }

        uint64_t metaSize = sizeof(DescriptorPool::Metadata);
        uint64_t descriptorSize = sizeof(Descriptor) * FLAGS_descriptor_pool_size;
        uint64_t nodeSize = (sizeof(QueueNode)) * FLAGS_node_size;
        uint64_t queueHeadSize = sizeof(QueueNode **);
        uint64_t queueTailSize = sizeof(QueueNode **);
        uint64_t extraSize = getExtraSize();
        uint64_t size = metaSize + descriptorSize + nodeSize + queueHeadSize + 
            queueTailSize + extraSize;
        std::cout << "MSQueueTestBase initSharedMemSegment size:" << std::dec << size 
            << ", meta size:" << std::dec << metaSize 
            << ", descriptorSize:" << std::dec << descriptorSize 
            << ", nodeSize:" << std::dec << nodeSize 
            << ", queueHeadSize:" << std::dec << queueHeadSize
            << ", queueTailSize:" << std::dec << queueTailSize
            << ", extraSize:" << std::dec << extraSize << std::endl;

        std::string segname(FLAGS_shm_segment);
        SharedMemorySegment* segment = initSharedMemSegment(segname, size);

        initMWCasDescriptorPool(segment, &descPool_);

        initQueueHeadTail(segment, metaSize+descriptorSize+nodeSize, 
            metaSize+descriptorSize+nodeSize+queueHeadSize,
            metaSize+descriptorSize);

        initOther(segment, metaSize+descriptorSize+nodeSize+queueHeadSize+queueTailSize);
        
        initMSQueue();


    }

    void initQueueHeadTail(SharedMemorySegment* segment, uint64_t headOffset,
        uint64_t tailOffset, uint64_t nodeOffset) 
    {
        queueHead_ = (QueueNode**)((uintptr_t)segment->GetMapAddress() + headOffset);
        queueTail_ = (QueueNode**)((uintptr_t)segment->GetMapAddress() + tailOffset);

        if(*queueHead_ == NULL && *queueTail_ == NULL) {
            std::cout << "queue empty. Give a sentinel node" << std::endl;
            QueueNode * begNode = (QueueNode*)((uintptr_t)segment->GetMapAddress() + nodeOffset);
            *queueHead_ = begNode;
            *queueTail_ = begNode;
        }

        std::cout << "queueHead_:" << queueHead_ << ", head point to:" << (*queueHead_)
            << ", queueTail_:" << queueTail_ << ", tail point to:" << (*queueTail_) << std::endl;
    }

    virtual uint64_t getExtraSize() = 0;
    virtual void initMSQueue() = 0;
    virtual void initOther(SharedMemorySegment* segment, uint64_t extraOffset) = 0;



    

    void Main(size_t thread_index) {















    }




    void Teardown() {




















    }



    QueueNode ** queueHead_;
    QueueNode ** queueTail_;
    DescriptorPool* descPool_;

    
};

struct MSQueueTPMWCasest : public MSQueueTestBase {
    uint64_t getExtraSize() {
        return sizeof(QueueNode **) * FLAGS_threads;
    }

    void initMSQueue() {
        msQueue_ = reinterpret_cast<MSQueueByPMWCas*>(Allocator::Get()->Allocate(
            sizeof(MSQueueByPMWCas)));
        new(msQueue_) MSQueueByPMWCas(queueHead_, queueTail_, descPool_);
    }

    void initOther(SharedMemorySegment* segment, uint64_t extraOffset) {
        threadAddr_ = (QueueNode**)((uintptr_t)segment->GetMapAddress() + extraOffset);
        for(int i = 0; i < FLAGS_threads; i++) {
            QueueNode ** threadAddr = threadAddr_+i;
            std::cout << "threadAddr. i:" << i << ", addr:" << threadAddr << ", content:" << (*threadAddr) << std::endl;
        }
    }

    MSQueueByPMWCas * msQueue_;
    QueueNode ** threadAddr_;
};



}

//////////////////////////////////////////////////////////////////////////////////////////////////
using namespace pmwcas;

void doTest(MSQueueTestBase & test) {
    std::cout << "Starting MSQueue benchmark..." << std::endl;
    test.Run(FLAGS_threads, FLAGS_seconds,
        static_cast<AffinityPattern>(FLAGS_affinity),
        FLAGS_metrics_dump_interval);
    printf("multiple cas: opCnt:%.0f, sec:%.3f,  %.2f ops/sec\n", (double)test.GetOperationCount(), 
  	  test.GetRunSeconds(),  (double)test.GetOperationCount() / test.GetRunSeconds());

    double opPerSec = (double)test.GetTotalSuccess() / test.GetRunSeconds();
    if(FLAGS_queue_op_type == 0) {
        printf("enq 50 percent and deq 50 percent: totalSuc:%.0f, sec:%.3f, %.2f successful ops/sec\n",
  	        (double)test.GetTotalSuccess(), test.GetRunSeconds(), opPerSec);
    }
    else if(FLAGS_queue_op_type == 1) {
        printf("enq 90 percent and deq 10 percent: totalSuc:%.0f, sec:%.3f, %.2f successful ops/sec\n",
  	         (double)test.GetTotalSuccess(), test.GetRunSeconds(), opPerSec);
    }
    else if(FLAGS_queue_op_type == 2) {
        printf("enq 10 percent and deq 90 percent: totalSuc:%.0f, sec:%.3f, %.2f successful ops/sec\n",
  	         (double)test.GetTotalSuccess(), test.GetRunSeconds(), opPerSec);
    }
}

void runBenchmark() {
    DumpArgs();
    std::cout << "> Args node_size " << FLAGS_node_size << std::endl;
    std::cout << "> Args queue_impl_type " << FLAGS_queue_impl_type << std::endl;
    std::cout << "> Args queue_op_type " << FLAGS_queue_op_type << std::endl;

    MSQueueTPMWCasest test;
    doTest(test);

    



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

