#define NOMINMAX

#include <string>
#include "mwcas_benchmark.h"
#include "MSQueue.h"


using namespace pmwcas::benchmark;

DEFINE_uint64(node_size, 1048576, "");
DEFINE_uint64(queue_impl_type, 0, "");
DEFINE_uint64(queue_op_type, 0, "");

namespace pmwcas {

struct QueueNodePool {
    QueueNodePool(/*EpochManager* epoch*/) {
        free_list = nullptr;
        free_list_tail = nullptr;
        /*garbage_list = (GarbageListUnsafe*)Allocator::Get()->Allocate(
                sizeof(GarbageListUnsafe));
        new(garbage_list) GarbageListUnsafe;
        auto s = garbage_list->Initialize(epoch);
        RAW_CHECK(s.ok(), "QueueNodePool garbage list initialization failure");*/
    }

  QueueNode *free_list;
  QueueNode *free_list_tail;
  //GarbageListUnsafe* garbage_list;
};

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
        uint64_t queueHeadSize = sizeof(QueueNode **)+56; //make it to cacheline size
        uint64_t queueTailSize = sizeof(QueueNode **)+56;
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

        queueHead_ = (QueueNode**)((uintptr_t)segment->GetMapAddress() + metaSize+descriptorSize+nodeSize);
        queueTail_ = (QueueNode**)((uintptr_t)segment->GetMapAddress() + metaSize+descriptorSize+nodeSize+queueHeadSize);
        std::cout << "queueHead_:" << queueHead_ << ", head point to:" << (*queueHead_)
                << ", queueTail_:" << queueTail_ << ", tail point to:" << (*queueTail_) << std::endl;
        initQueueNodePool(segment, metaSize+descriptorSize);

        initQueueHeadTail(segment);

        initOther(segment, metaSize+descriptorSize+nodeSize+queueHeadSize+queueTailSize);
        
        initMSQueue();

        orgQueueSize_ = getQueueSize();
        std::cout << "orgQueueSize_:" << orgQueueSize_ << std::endl;

        enqNum_ = 0;
        deqNum_ = 0;
    }

    void initQueueNodePool(SharedMemorySegment* segment, uint64_t nodeOffset) {
        nodePoolTbl_ = (QueueNodePool*)Allocator::Get()->AllocateAligned(
                sizeof(QueueNodePool)*FLAGS_threads, kCacheLineSize);
        RAW_CHECK(nullptr != nodePoolTbl_, "initQueueNodePool out of memory");

        for(uint32_t i = 0; i < FLAGS_threads; ++i) {
            new(&nodePoolTbl_[i]) QueueNodePool(/*descPool_->GetEpoch()*/);
        }

        nodePtr_ = (QueueNode*)((uintptr_t)segment->GetMapAddress() + nodeOffset);
        std::cout << "initQueueNodePool nodePtr:" << nodePtr_ << std::endl;
        if(queueHead_ == NULL || queueTail_ == NULL) {
            std::cout << "tail or head is null. clear all node" << std::endl;
            memset(nodePtr_, 0, sizeof(QueueNode) * FLAGS_node_size);
        }

        uint32_t thread = 0;
        uint32_t noodThread = FLAGS_node_size / FLAGS_threads;
        uint32_t busyNodeCnt = 0;
        uint32_t freeNodeCnt = 0;
        for(uint32_t i = 0; i < FLAGS_node_size; ++i) {
            QueueNode * pNode = nodePtr_ + i;
            if(pNode->isBusy_ == 1) {
                busyNodeCnt++;
                continue;
            }

            QueueNodePool * pool = nodePoolTbl_ + thread;
            pNode->poolNext_ = pool->free_list;
            pool->free_list = pNode;
            freeNodeCnt++;

            if(pool->free_list_tail == NULL) {
                pool->free_list_tail = pNode;
            }

            if((i + 1) % noodThread == 0) {
                std::cout << "initQueueNodePool node count:" << std::dec << freeNodeCnt << ", thread:" << thread << std::endl;
                thread++;
            }
        }

        std::cout << "freeNodeCnt:" << std::dec << freeNodeCnt << ", busyNodeCnt:" << std::dec << busyNodeCnt << std::endl;
    }

    void printNodePoolNum() {
        std::cout << "printNodePoolNum:";
        
        uint32_t busyNodeCnt = 0;
        uint32_t freeNodeCnt = 0;
        for(uint32_t i = 0; i < FLAGS_node_size; ++i) {
            QueueNode * pNode = nodePtr_ + i;
            if(pNode->isBusy_ == 1) {
                busyNodeCnt++;
            }
            else {
                freeNodeCnt++;
            }
        }

        std::cout << "For all nodes, busyNodeCnt:" << busyNodeCnt << ", freeNodeCnt:" << freeNodeCnt << std::endl;

        for(uint32_t i = 0; i < FLAGS_threads; ++i) {
            QueueNodePool* nodePool = nodePoolTbl_ + i;

            QueueNode * curNode = nodePool->free_list;
            uint32_t len = 0;
            while(curNode) {
                len++;
                curNode = curNode->poolNext_;
            }

            std::cout << "threadNum:" << i << ", node pool len:" << len << std::endl;
        }
    }

    QueueNode * allocateNode(int threadNum) {
        QueueNodePool * nodePool = nodePoolTbl_ + threadNum;
        QueueNode * node = nodePool->free_list;
        RAW_CHECK(nullptr != node, "allocateNode no free node");
        /*while(!node) {
            nodePool->garbage_list->GetEpoch()->BumpCurrentEpoch();
            nodePool->garbage_list->Scavenge();
            node = nodePool->free_list;
        }*/
        nodePool->free_list = node->poolNext_;
        if(nodePool->free_list_tail == node) {
            nodePool->free_list_tail = NULL;
        }

        node->poolNext_ = NULL;
        return node;
    }

    void reclaimNode(QueueNode * node, int threadNum) {
        node->poolNext_ = NULL;

        QueueNodePool * nodePool = nodePoolTbl_ + threadNum;
        if(nodePool->free_list_tail != NULL) {
            nodePool->free_list_tail->poolNext_ = node;
            nodePool->free_list_tail = node;
        }
        else {
            nodePool->free_list_tail = node;
            nodePool->free_list = node;
        }

        //if reclaim in the head, then always use the same nodes
        /*node->poolNext_ = nodePool->free_list;
        nodePool->free_list = node;*/
    }

    void initQueueHeadTail(SharedMemorySegment* segment) 
    {
        if(*queueHead_ == NULL || *queueTail_ == NULL) {
            
            QueueNode * begNode = allocateNode(0);
            std::cout << "queue empty. Give a sentinel node:" << begNode << std::endl;
            begNode->isBusy_ = 1;
            begNode->next_ = NULL;
            begNode->pData_ = NULL;

            NVRAM::Flush(sizeof(QueueNode), (const void*)begNode);
            
            *queueHead_ = begNode;
            *queueTail_ = begNode;

            NVRAM::Flush(sizeof(QueueNode *), (const void*)queueHead_);
            NVRAM::Flush(sizeof(QueueNode *), (const void*)queueTail_);

            std::cout << "queueHead_:" << queueHead_ << ", head point to:" << (*queueHead_)
                << ", queueTail_:" << queueTail_ << ", tail point to:" << (*queueTail_) << std::endl;
        }
    }

    virtual uint64_t getExtraSize() = 0;
    virtual void initMSQueue() = 0;
    virtual void initOther(SharedMemorySegment* segment, uint64_t extraOffset) = 0;
    
    void Main(size_t thread_index) {       
        auto s = MwCASMetrics::ThreadInitialize();
	    RAW_CHECK(s.ok(), "Error initializing thread");

        WaitForStart();

        descPool_->GetEpoch()->Protect();

        recover(thread_index);
        
        uint64_t n_success = 0;
        uint64_t n_enq = 0;
        uint64_t n_deq = 0;
        while(!IsShutdown()) {
            enqueue(thread_index, (uint64_t *)n_enq++);
            enqueue(thread_index, (uint64_t *)n_enq++);

            if(dequeue(thread_index)) n_deq++;
            if(dequeue(thread_index)) n_deq++;

            n_success += 4;
        }
        
        descPool_->GetEpoch()->Unprotect();
        (threadIdOpNumMap_[thread_index]) = n_success;

        total_success_.fetch_add(threadIdOpNumMap_[thread_index], std::memory_order_seq_cst);
        enqNum_.fetch_add(n_enq, std::memory_order_seq_cst);
        deqNum_.fetch_add(n_deq, std::memory_order_seq_cst);

        LOG(ERROR) << thread_index << ", n_success: " << threadIdOpNumMap_[thread_index]
	            << ", n_enq:" << n_enq << ", n_deq:" << n_deq << std::endl;
    }

    uint64_t getQueueSize() {
        uint64_t size = 0; 
        QueueNode * curNode = (*queueHead_);
        while(curNode) {
            size++;
            curNode = curNode->next_;
        }

        return size;
    }

    void Teardown() {
        std::cout << "total_success_:" << total_success_.load() << ", enqNum_:" << enqNum_.load()
            << ", deqNum_:" << deqNum_.load() << ", orgQueueSize_:" << orgQueueSize_ << std::endl;

        std::cout << "queueHead_:" << queueHead_ << ", head point to:" << (*queueHead_)
                << ", queueTail_:" << queueTail_ << ", tail point to:" << (*queueTail_) << std::endl;

        printExtraInfo();
        printNodePoolNum();

        uint64_t newQueueSize = getQueueSize();
        std::cout << "newQueueSize:" << newQueueSize << std::endl;

        RAW_CHECK(newQueueSize == orgQueueSize_ + enqNum_.load() - deqNum_.load(), 
            "queue size not right");
    }

    virtual void recover(size_t thread_index) = 0;
    virtual void enqueue(size_t thread_index, uint64_t * pData = NULL) = 0;
    virtual bool dequeue(size_t thread_index) = 0;
    virtual void printExtraInfo() = 0;

    

    QueueNode ** queueHead_;
    QueueNode ** queueTail_;
    DescriptorPool* descPool_;

    QueueNodePool* nodePoolTbl_;
    QueueNode * nodePtr_;

    std::atomic<uint64_t> enqNum_;
    std::atomic<uint64_t> deqNum_;

    std::atomic<uint64_t> orgQueueSize_;

};

struct MSQueueTPMWCasest : public MSQueueTestBase {
    uint64_t getExtraSize() {
        return sizeof(QueueNode **) * FLAGS_threads + sizeof(QueueNode **) * FLAGS_threads
            + sizeof(uint64_t **) * FLAGS_threads;
    }

    void initMSQueue() {
        msQueue_ = reinterpret_cast<MSQueueByPMWCas*>(Allocator::Get()->Allocate(
            sizeof(MSQueueByPMWCas)));
        new(msQueue_) MSQueueByPMWCas(queueHead_, queueTail_, descPool_);
    }

    void initOther(SharedMemorySegment* segment, uint64_t extraOffset) {
        threadEnqAddr_ = (QueueNode**)((uintptr_t)segment->GetMapAddress() + extraOffset);
        threadDeqAddr_ = (QueueNode**)((uintptr_t)segment->GetMapAddress() + extraOffset +  
                sizeof(QueueNode **) * FLAGS_threads);
        deqDataAddr_ = (uint64_t **)((uintptr_t)segment->GetMapAddress() + extraOffset +  
                sizeof(QueueNode **) * FLAGS_threads * 2);
                
        printExtraInfo();
    }

    void printExtraInfo() {
        for(int i = 0; i < FLAGS_threads; i++) {
            QueueNode ** threadEnqAddr = threadEnqAddr_+i;
            QueueNode * enqNode = (*threadEnqAddr);
            std::cout << "threadEnqAddr. i:" << i << ", addr:" << threadEnqAddr 
                << ", enqNode:" << (enqNode) << std::endl;
            if(enqNode != NULL) {
                printOneNode(enqNode);
            }
                
            QueueNode ** threadDeqAddr = threadDeqAddr_+i;
            QueueNode * deqNode = (*threadDeqAddr);
            std::cout << "threadDeqAddr. i:" << i << ", addr:" << threadDeqAddr 
                << ", deqNode:" << (deqNode) << std::endl;
            if(deqNode != NULL) {
                printOneNode(deqNode);
            }

            uint64_t ** deqDataAddr = deqDataAddr_+i;
            uint64_t * deqDataPtr = (*deqDataAddr);
            std::cout << "deqDataAddr. i:" << i << ", addr:" << deqDataAddr 
                << ", deqDataPtr:" << (deqDataPtr) << std::endl;
        }
    }

    void printOneNode(QueueNode * node) {
        std::cout << "enqNode node:" << node << ", pData_:" << node->pData_ << ", next_:" << node->next_
            << ", poolNext_:" << node->poolNext_ << ", isBusy_:" << node->isBusy_;
    }

    void recover(size_t thread_index) {
        recoverEnq(thread_index);
        recoverDeq(thread_index);
    }

    void recoverEnq(size_t thread_index) {
        QueueNode ** threadEnqAddr = threadEnqAddr_+thread_index;
        QueueNode * enqNode = (*threadEnqAddr);
        if(enqNode != NULL) {
            if(enqNode->isBusy_ == 1) {
                msQueue_->enq(threadEnqAddr);
                orgQueueSize_++;
            }
            else {
                *threadEnqAddr = NULL;
                NVRAM::Flush(sizeof(QueueNode *), (const void*)threadEnqAddr);
            }
        }
    }

    void recoverDeq(size_t thread_index) {
        QueueNode ** threadDeqAddr = threadDeqAddr_+thread_index;
        QueueNode * deqNode = (*threadDeqAddr);
        if(deqNode != NULL) {
            if(deqNode->isBusy_ == 1) {
                cleanDeqNode(thread_index, deqNode);
            }

            *threadDeqAddr = NULL;
            NVRAM::Flush(sizeof(QueueNode *), (const void*)threadDeqAddr);
        }

        uint64_t ** deqDataAddr = deqDataAddr_ + thread_index;
        if(*deqDataAddr) {
            *deqDataAddr = NULL;
            NVRAM::Flush(sizeof(uint64_t *), (const void*)deqDataAddr);
        }
    }

    void enqueue(size_t thread_index, uint64_t * pData = NULL) {
        QueueNode * newNode = allocateNode(thread_index);
        QueueNode ** threadEnqAddr = threadEnqAddr_+thread_index;
        *threadEnqAddr = newNode;
        NVRAM::Flush(sizeof(QueueNode *), (const void*)threadEnqAddr);

        newNode->next_ = NULL;
        newNode->pData_ = pData;
        newNode->isBusy_= 1;
        //printOneNode(newNode);
        NVRAM::Flush(sizeof(QueueNode), (const void*)newNode);

        msQueue_->enq(threadEnqAddr);
    }
 
    bool dequeue(size_t thread_index) {
        QueueNode ** threadDeqAddr = threadDeqAddr_+thread_index;
        uint64_t ** deqDataAddr = deqDataAddr_+thread_index;
        msQueue_->deq(threadDeqAddr, deqDataAddr);

        QueueNode * deqNode = (*threadDeqAddr);
        if(deqNode != NULL) {
            //another thread may also deq, then the realDeqNode maybe reclaimed with epoch
            //QueueNode * realDeqNode = deqNode->next_;

            //do something about the deq Data
            
            cleanDeqNode(thread_index, deqNode);
            
            *threadDeqAddr = NULL;
            NVRAM::Flush(sizeof(QueueNode *), (const void*)threadDeqAddr);

            *deqDataAddr = NULL;
            NVRAM::Flush(sizeof(uint64_t *), (const void*)deqDataAddr);

            return true;
        }

        return false;
    }

    void cleanDeqNode(size_t thread_index, QueueNode * deqNode) {
        deqNode->next_ = NULL;
        deqNode->pData_ = NULL;
        deqNode->isBusy_= 0;

        NVRAM::Flush(sizeof(QueueNode), (const void*)deqNode);
        reclaimNode(deqNode, thread_index);
    }

    MSQueueByPMWCas * msQueue_;
    QueueNode ** threadEnqAddr_;
    QueueNode ** threadDeqAddr_;
    uint64_t ** deqDataAddr_;
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

