#define NOMINMAX

#include <unordered_map>
#include <string>
#include "mwcas_benchmark.h"
#include "MSQueue.h"
#include "NodePool.h"

using namespace pmwcas::benchmark;

DEFINE_uint64(node_size, 8388608, "");
DEFINE_uint64(queue_size, 15, "");
DEFINE_uint64(queue_impl_type, 0, "");


namespace pmwcas {

struct MSQueueTestBaseV1 : public BaseMwCas {

    void Setup(size_t thread_count) 
    {
        NVRAM::InitializeClflush();

        uint64_t metaSize = sizeof(DescriptorPool::Metadata);
        uint64_t descriptorSize = getDescriptorSizeSize();
        uint64_t nodeSize = getNodeSize() * FLAGS_node_size;
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

        initDescriptorPool(segment);

        if(!isNewMem_) {
            recoverQueue();
        }
        
        initQueueNodePool(segment, metaSize+descriptorSize);

        queueHead_ = (QueueNode**)((uintptr_t)segment->GetMapAddress() + metaSize+descriptorSize+nodeSize);
        queueTail_ = (QueueNode**)((uintptr_t)segment->GetMapAddress() + metaSize+descriptorSize+nodeSize+queueHeadSize);
        std::cout << "queueHead_:" << queueHead_ << ", head point to:" << (*queueHead_)
                << ", queueTail_:" << queueTail_ << ", tail point to:" << (*queueTail_) << std::endl;
        initQueueHeadTail(segment);

        initOther(segment, metaSize+descriptorSize+nodeSize+queueHeadSize+queueTailSize);
        
        initMSQueue();

        orgQueueSize_ = getQueueSize();
        std::cout << "******************orgQueueSize_:" << orgQueueSize_ << "*****************" << std::endl;

        enqNum_ = 0;
        deqNum_ = 0;
        recoverNum_ = 0;

        for(int i = 0; i < FLAGS_threads; i++) {
            threadIdOpNumMap_[i] = 0;
        }
    }

    virtual uint64_t getDescriptorSizeSize() = 0;
    virtual void initDescriptorPool(SharedMemorySegment* segment) = 0;
    virtual void initMSQueue() = 0;
    virtual void initOther(SharedMemorySegment* segment, uint64_t extraOffset) = 0;
    virtual uint64_t getExtraSize() = 0;

    void initQueueNodePool(SharedMemorySegment* segment, uint64_t nodeOffset) {
        nodePtr_ = (QueueNode*)((uintptr_t)segment->GetMapAddress() + nodeOffset);
        std::cout << "initQueueNodePool nodePtr:" << nodePtr_ << std::endl;

        nodePool_ = (NodePool*)Allocator::Get()->AllocateAligned(
                sizeof(NodePool), kCacheLineSize);
        new(nodePool_) NodePool(
            FLAGS_node_size, FLAGS_threads, nodePtr_, getNodeSize(), getNodeType());
    }

    virtual size_t getNodeSize() = 0;
    virtual int getNodeType() = 0;


    void initQueueHeadTail(SharedMemorySegment* segment) 
    {
        if(*queueHead_ == NULL || *queueTail_ == NULL) {
            QueueNode * begNode = genSentinelNode();
            printOneNode(begNode);
            *queueHead_ = begNode;
            *queueTail_ = begNode;

            NVRAM::Flush(sizeof(QueueNode *), (const void*)queueHead_);
            NVRAM::Flush(sizeof(QueueNode *), (const void*)queueTail_);

            std::cout << "queueHead_:" << queueHead_ << ", head point to:" << (*queueHead_)
                << ", queueTail_:" << queueTail_ << ", tail point to:" << (*queueTail_) << std::endl;

            isNewQueue_ = true; 
        }
        else {
            isNewQueue_ = false; 
        }
    }

    virtual QueueNode * genSentinelNode() = 0;
    virtual void printOneNode(QueueNode * node, const char * info = "") = 0;

    void Main(size_t thread_index) {              
        auto s = MwCASMetrics::ThreadInitialize();
	    RAW_CHECK(s.ok(), "Error initializing thread");

        descriptorEpochProtect();
        nodePool_->GetEpoch()->Protect();
        logEntryEpochProtect();

        uint64_t revTime = 0;
        if(isNewQueue_) {
            for(int i = 0; i < (FLAGS_queue_size/FLAGS_threads)+1; i++) {
                enqueue(thread_index);
            }
        }
        else {
            uint64_t revBefore = Environment::Get()->NowMicros();
            recover(thread_index);
            revTime = Environment::Get()->NowMicros() - revBefore;
        }

        descriptorEpochUnProtect();
        nodePool_->GetEpoch()->Unprotect();
        logEntryEpochUnProtect();

        recoverNum_.fetch_add(1, std::memory_order_seq_cst);
        while(recoverNum_.load() < FLAGS_threads);
        orgQueueSize_ = getQueueSize();

        LOG(ERROR) << "**************************thread " << thread_index << " recover time:" 
            << revTime << " micro seconds ***************************" << std::endl;

        WaitForStart();
        const uint64_t kNodeEpochThreshold = 100;
        uint64_t nodeEpochs = 0;

        descriptorEpochProtect();
        nodePool_->GetEpoch()->Protect();
        logEntryEpochProtect();
        
        
        uint64_t n_success = 0;
        uint64_t n_enq = 0;
        uint64_t n_deq = 0;
        while(!IsShutdown()) 
        {
            if(++nodeEpochs == kNodeEpochThreshold) {
                nodePool_->GetEpoch()->Unprotect();
                logEntryEpochUnProtect();

                nodePool_->GetEpoch()->Protect();
                logEntryEpochProtect();
                nodeEpochs = 0;
            }

            enqueue(thread_index, (uint64_t *)n_enq);
            n_enq++;
            enqueue(thread_index, (uint64_t *)n_enq);
            n_enq++;

            if(dequeue(thread_index)) n_deq++;
            if(dequeue(thread_index)) n_deq++;

            n_success += 4;
        }

        descriptorEpochUnProtect();
        nodePool_->GetEpoch()->Unprotect();
        logEntryEpochUnProtect();

        (threadIdOpNumMap_[thread_index]) = n_success;

        total_success_.fetch_add(threadIdOpNumMap_[thread_index], std::memory_order_seq_cst);
        enqNum_.fetch_add(n_enq, std::memory_order_seq_cst);
        deqNum_.fetch_add(n_deq, std::memory_order_seq_cst);

        LOG(INFO) << thread_index << ", n_success: " << threadIdOpNumMap_[thread_index]
	            << ", n_enq:" << n_enq << ", n_deq:" << n_deq << std::endl;
        LOG(INFO) << thread_index << ", total_success_: " << total_success_
	            << ", enqNum_:" << enqNum_ << ", deqNum_:" << deqNum_ << std::endl;
        
    }

    void logEntryEpochProtect() {
        if(getLogEntryNodePool() != NULL) {
            getLogEntryNodePool()->GetEpoch()->Protect();
        }
    }

    void logEntryEpochUnProtect() {
        if(getLogEntryNodePool() != NULL) {
            getLogEntryNodePool()->GetEpoch()->Unprotect();
        }
    }


    void descriptorEpochProtect() {
        if(getDescPool() != NULL) {
            getDescPool()->GetEpoch()->Protect();
        }
    }
 
    void descriptorEpochUnProtect() {
        if(getDescPool() != NULL) {
            getDescPool()->GetEpoch()->Unprotect();
        }
    }

    virtual BaseDescriptorPool* getDescPool() = 0;
    virtual NodePool* getLogEntryNodePool() = 0;

    uint64_t getQueueSize() {
        uint64_t size = 0; 
        QueueNode * curNode = (QueueNode *)((uint64_t)(*queueHead_) 
            & (OptmizedFASASDescriptor::ActualValueFlg));
        while(curNode) {
            size++;
            curNode = (QueueNode *)((uint64_t)(curNode->next_) & (OptmizedFASASDescriptor::ActualValueFlg));
        }

        return size;
    }

    void Teardown() {
        std::cout << "total_success_:" << total_success_.load() << ", enqNum_:" << enqNum_.load()
            << ", deqNum_:" << deqNum_.load() << ", orgQueueSize_:" << orgQueueSize_ << std::endl;

        std::cout << "queueHead_:" << queueHead_ << ", head point to:" << (*queueHead_)
                << ", queueTail_:" << queueTail_ << ", tail point to:" << (*queueTail_) << std::endl;

        printExtraInfo();
        nodePool_->printNodePoolNum(getNodeSize());

        uint64_t newQueueSize = getQueueSize();
        std::cout << "newQueueSize:" << newQueueSize << std::endl;

        RAW_CHECK(newQueueSize == orgQueueSize_ + enqNum_.load() - deqNum_.load(), 
            "queue size not right");
    }


    virtual void recoverQueue() {}
    
    virtual void recover(size_t thread_index) = 0;
    virtual void enqueue(size_t thread_index, uint64_t * pData = NULL) = 0;
    virtual bool dequeue(size_t thread_index) = 0;
    virtual void printExtraInfo() = 0;
    



    QueueNode ** queueHead_;
    QueueNode ** queueTail_;
    
    QueueNode * nodePtr_;
    NodePool * nodePool_;

    std::atomic<uint64_t> enqNum_;
    std::atomic<uint64_t> deqNum_;
    std::atomic<uint64_t> recoverNum_;

    bool isNewQueue_;
    std::atomic<uint64_t> orgQueueSize_;

};

//////////////////////////////////////////////////////////////////////////////////////////

struct MSQueuePVTest : public MSQueueTestBaseV1 {

    uint64_t getExtraSize() {
        return 64*2* FLAGS_threads;
    }

    void initOther(SharedMemorySegment* segment, uint64_t extraOffset) {
        threadEnqAddr_ = (QueueNode**)((uintptr_t)segment->GetMapAddress() + extraOffset);
        threadDeqAddr_ = (QueueNode**)((uintptr_t)segment->GetMapAddress() + extraOffset +  
                64 * FLAGS_threads);
        printExtraInfo();
    }

    void printExtraInfo() {
        for(int i = 0; i < FLAGS_threads; i++) {
            //should be 8 not 64
            QueueNode ** threadEnqAddr = threadEnqAddr_+i*8;
            QueueNode * enqNode = (*threadEnqAddr);
            std::cout << "threadEnqAddr. i:" << i << ", addr:" << threadEnqAddr 
                << ", enqNode:" << (enqNode) << std::endl;
            if(enqNode != NULL) {
                printOneNode(enqNode);
            }
                
            QueueNode ** threadDeqAddr = threadDeqAddr_+i*8;
            QueueNode * deqNode = (*threadDeqAddr);
            std::cout << "threadDeqAddr. i:" << i << ", addr:" << threadDeqAddr 
                << ", deqNode:" << (deqNode) << std::endl;
            if(deqNode != NULL) {
                printOneNode(deqNode);
            }
        }
    }

    virtual NodePool* getLogEntryNodePool() {
        return NULL;
    }


    QueueNode ** threadEnqAddr_;
    QueueNode ** threadDeqAddr_;

};

///////////////////////////////////////////////////////////////////////////////////////////


struct MSQueueByOrgCasTestV1 : public MSQueuePVTest {
    uint64_t getDescriptorSizeSize() {
        return 0;
    }

    void initDescriptorPool(SharedMemorySegment* segment) {
    }

    virtual size_t getNodeSize() {
        return sizeof(OrgCasNode);
    }

    virtual int getNodeType() {
        return 0;
    }

    virtual BaseDescriptorPool* getDescPool() {
        return NULL;
    }

    void initMSQueue() {
        msQueue_ = reinterpret_cast<MSQueueByOrgCas*>(Allocator::Get()->Allocate(
            sizeof(MSQueueByOrgCas)));
        new(msQueue_) MSQueueByOrgCas(queueHead_, queueTail_);
    }

    OrgCasNode * genSentinelNode() {
        OrgCasNode * begNode = (OrgCasNode *)nodePool_->AllocateNode(0);
        std::cout << "org queue empty. Give a sentinel node:" << begNode << std::endl;
        //begNode->isBusy_ = 1;
        begNode->next_ = NULL;
        begNode->pData_ = NULL;
        begNode->del_thread_index_ = -1;
        
        NVRAM::Flush(sizeof(OrgCasNode), (const void*)begNode);
        return begNode;
    }

    void printOneNode(QueueNode * nodeOrg, const char * info = "") {
        OrgCasNode * node = (OrgCasNode *)(nodeOrg);
        std::cout << "printOneNode node:" << node << ", pData_:" << node->pData_ << ", next_:" << node->next_
            << ", poolNext_:" << node->poolNext_ 
            //<< ", isBusy_:" << node->isBusy_ 
            << ", del_thread_index_:" << node->del_thread_index_ << std::endl;
    }

    void recover(size_t thread_index) {
    }

    virtual void recoverEnq(size_t thread_index) {
    }

    virtual void recoverDeq(size_t thread_index) {
    }

    void enqueue(size_t thread_index, uint64_t * pData = NULL) {
        OrgCasNode * newNode = (OrgCasNode *)nodePool_->AllocateNode(thread_index);

        OrgCasNode ** threadEnqAddr = (OrgCasNode **)(threadEnqAddr_+thread_index*8);
        *threadEnqAddr = newNode;

        if(FLAGS_queue_impl_type ==0 || FLAGS_queue_impl_type == 1) {
            NVRAM::Flush(sizeof(OrgCasNode *), (const void*)threadEnqAddr);
        }

        newNode->next_ = NULL;
        newNode->pData_ = pData;
        //newNode->isBusy_= 1;
        newNode->del_thread_index_ = -1;
        //printOneNode(newNode);
        NVRAM::Flush(sizeof(OrgCasNode), (const void*)newNode);

        msQueue_->enq(threadEnqAddr, FLAGS_queue_impl_type);
    }
    
    bool dequeue(size_t thread_index) {
        OrgCasNode ** threadDeqAddr = (OrgCasNode **)(threadDeqAddr_+thread_index*8);
        *threadDeqAddr = NULL;

        if(FLAGS_queue_impl_type ==0 || FLAGS_queue_impl_type == 1) {
            NVRAM::Flush(sizeof(OrgCasNode *), (const void*)threadDeqAddr);
        }
         
        if(!msQueue_->deq(threadDeqAddr, thread_index, FLAGS_queue_impl_type)) {
            return false;
        }
        
        OrgCasNode * deqNode = (*threadDeqAddr);
        RAW_CHECK(nullptr != deqNode, "MSQueueByOrgCas deqNode null");

        //deqNode->isBusy_= 0;
        //NVRAM::Flush(sizeof(OrgCasNode), (const void*)deqNode);

        nodePool_->ReleaseNode(thread_index, deqNode);
        
        *threadDeqAddr = NULL;
        

        return true;
    }


    MSQueueByOrgCas * msQueue_;

};


///////////////////////////////////////////////////////////////////////////////////////////
struct MSQueueByFASAS : public MSQueuePVTest {
    virtual size_t getNodeSize() {
        return sizeof(QueueNode);
    }

    virtual int getNodeType() {
        return 1;
    }

    QueueNode * genSentinelNode() {
        QueueNode * begNode = (QueueNode *)nodePool_->AllocateNode(0);
        std::cout << "queue empty. Give a sentinel node:" << begNode << std::endl;
        //begNode->isBusy_ = 1;
        begNode->next_ = NULL;
        begNode->pData_ = NULL;

        NVRAM::Flush(sizeof(QueueNode), (const void*)begNode);
        return begNode;
    }

    void enqueue(size_t thread_index, uint64_t * pData = NULL) {
        QueueNode * newNode = (QueueNode *)nodePool_->AllocateNode(thread_index);

        QueueNode ** threadEnqAddr = threadEnqAddr_+thread_index*8;
        *threadEnqAddr = newNode;
        NVRAM::Flush(sizeof(QueueNode *), (const void*)threadEnqAddr);

        newNode->next_ = NULL;
        newNode->pData_ = pData;
        //newNode->isBusy_= 1;
        //printOneNode(newNode);
        NVRAM::Flush(sizeof(QueueNode), (const void*)newNode);

        rawEnque(threadEnqAddr, thread_index);
    }
    
    virtual void rawEnque(QueueNode ** threadEnqAddr, size_t thread_index) = 0;

    bool dequeue(size_t thread_index) {
        QueueNode ** threadDeqAddr = threadDeqAddr_+thread_index*8;
        *threadDeqAddr = NULL;
        NVRAM::Flush(sizeof(QueueNode *), (const void*)threadDeqAddr);

        rawDequeue(threadDeqAddr, thread_index);

        QueueNode * deqNode = (*threadDeqAddr);
        if(deqNode != NULL) {
            //deqNode->isBusy_= 0;
            //NVRAM::Flush(sizeof(QueueNode), (const void*)deqNode);

            nodePool_->ReleaseNode(thread_index, deqNode);

            *threadDeqAddr = NULL;
            return true;
        }

        return false;
    }

    virtual void rawDequeue(QueueNode ** threadDeqAddr, size_t thread_index) = 0;

    void printOneNode(QueueNode * node, const char * info = "") {
        std::cout << "printOneNode node:" << node << ", pData_:" << node->pData_ 
            << ", next_:" << node->next_ << ", poolNext_:" << node->poolNext_ 
            //<< ", isBusy_:" << node->isBusy_ 
            << std::endl;
    }
};

///////////////////////////////////////////////////////////////////////////////////////////

struct MSQueuePMWCasV2TestV1 : public MSQueueByFASAS {
    uint64_t getDescriptorSizeSize() {
        return sizeof(FASASDescriptor) * FLAGS_descriptor_pool_size;
    }

    void initDescriptorPool(SharedMemorySegment* segment) {
        initFASSDescriptorPool(segment, &fasasDescPool_);
    }

    virtual BaseDescriptorPool* getDescPool() {
        return fasasDescPool_;
    }

    void initMSQueue() {
        msQueue_ = reinterpret_cast<MSQueueByPMWCasV2*>(Allocator::Get()->Allocate(
            sizeof(MSQueueByPMWCasV2)));
        new(msQueue_) MSQueueByPMWCasV2(queueHead_, queueTail_, fasasDescPool_);
    }

    void recover(size_t thread_index) {
    }

    virtual void recoverEnq(size_t thread_index) {
    }

    virtual void recoverDeq(size_t thread_index) {
    }


    virtual void rawEnque(QueueNode ** threadEnqAddr, size_t thread_index) {
        msQueue_->enq(threadEnqAddr);
    }

    virtual void rawDequeue(QueueNode ** threadDeqAddr, size_t thread_index) {
        msQueue_->deq(threadDeqAddr);
    }

    FASASDescriptorPool* fasasDescPool_;

    MSQueueByPMWCasV2 * msQueue_;


};


//////////////////////////////////////////////////////////////////////////////////////////////////

struct MSQueuePMWCasV3TestV1 : public MSQueueByFASAS {
    uint64_t getDescriptorSizeSize() {
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

    virtual BaseDescriptorPool* getDescPool() {
       return nullptr;
    }

    void initMSQueue() {
        msQueue_ = reinterpret_cast<MSQueueByPMWCasV3*>(Allocator::Get()->Allocate(
            sizeof(MSQueueByPMWCasV3)));
        new(msQueue_) MSQueueByPMWCasV3(queueHead_, queueTail_, fasasDescPool_);
    }

    void recover(size_t thread_index) {
    }

    virtual void recoverEnq(size_t thread_index) {
    }

    virtual void recoverDeq(size_t thread_index) {
    }


    virtual void rawEnque(QueueNode ** threadEnqAddr, size_t thread_index) {
        msQueue_->enq(threadEnqAddr, thread_index);
    }

    virtual void rawDequeue(QueueNode ** threadDeqAddr, size_t thread_index) {
        msQueue_->deq(threadDeqAddr, thread_index);
    }

    OptmizedFASASDescriptorPool * fasasDescPool_;

    MSQueueByPMWCasV3 * msQueue_;


};


//////////////////////////////////////////////////////////////////////////////////////////////////
struct MSLogQueueTestV1 : public MSQueueTestBaseV1 {
    uint64_t getExtraSize() {
        return sizeof(LogEntry) * FLAGS_node_size + 64*(FLAGS_threads);
    }

    virtual size_t getNodeSize() {
        return sizeof(LogQueueNode);
    }

    virtual int getNodeType() {
        return 2;
    }

    void initMSQueue() {
        msQueue_ = reinterpret_cast<LogQueue*>(Allocator::Get()->Allocate(
            sizeof(LogQueue)));
        new(msQueue_) LogQueue(queueHead_, queueTail_);
    }


    void initOther(SharedMemorySegment* segment, uint64_t extraOffset) {
        initLogEntryPool(segment, extraOffset);

        threadLogEntry_ = (LogEntry **)((uintptr_t)segment->GetMapAddress() 
            + extraOffset + sizeof(LogEntry) * FLAGS_node_size);
        std::cout << "threadLogEntry_:" << threadLogEntry_ << std::endl;
        printExtraInfo();
    }

    void initLogEntryPool(SharedMemorySegment* segment, uint64_t logEntryOffset) {
        logEntryPtr_ = (LogEntry *)((uintptr_t)segment->GetMapAddress() + logEntryOffset);
        std::cout << "initLogEntryPool logEntryPtr_:" << logEntryPtr_ << std::endl;


        logEntryPool_ = (NodePool*)Allocator::Get()->AllocateAligned(
                sizeof(NodePool), kCacheLineSize);
        new(logEntryPool_) NodePool(
            FLAGS_node_size, FLAGS_threads, logEntryPtr_, sizeof(LogEntry), 3);
    }

    void printExtraInfo() {
        for(int i = 0; i < FLAGS_threads; i++) {
            LogEntry ** logEntry = threadLogEntry_ + i*8;
            std::cout << "i:" << i << ", logEntry:" << logEntry << ", point to :"
                << (*logEntry) << std::endl;
        }
    }

    virtual void printOneNode(QueueNode * rnode, const char * info = "") {
        LogQueueNode * node = (LogQueueNode *)(rnode);
        LOG(ERROR) << info << " LogQueueNode:" << node << ", pData_:" << node->pData_ << ", next_:" << node->next_
            << ", poolNext_:" << node->poolNext_ 
            //<< ", isBusy_:" << node->isBusy_ 
            << ", logInsert_:" << node->logInsert_ << ", logRemove_:" << node->logRemove_ << std::endl;
    }

    void recover(size_t thread_index) {
    }

    LogQueueNode * genSentinelNode() {
        LogQueueNode * begNode = (LogQueueNode *)nodePool_->AllocateNode(0);
        std::cout << "log queue empty. Give a sentinel node:" << begNode << std::endl;
        
        //begNode->isBusy_ = 1;
        begNode->next_ = NULL;
        begNode->pData_ = NULL;
        begNode->logInsert_ = NULL;
        begNode->logRemove_ = NULL;
        
        NVRAM::Flush(sizeof(LogQueueNode), (const void*)begNode);
        return begNode;
    }

    void enqueue(size_t thread_index, uint64_t * pData = NULL) {
        LogEntry * plog = (LogEntry *)logEntryPool_->AllocateNode(thread_index);

        LogQueueNode * newNode = (LogQueueNode *)nodePool_->AllocateNode(thread_index);
        plog->node_ = newNode;
        plog->status_ = 0;
        plog->operation_ = 0;
        plog->operationNum_ = 0;
        //plog->isBusy_ = 1;
        NVRAM::Flush(sizeof(LogEntry), (const void*)plog);

        LogEntry ** threadLogEntry = threadLogEntry_ + thread_index*8;
        *threadLogEntry = plog;
        NVRAM::Flush(sizeof(LogEntry *), (const void*)threadLogEntry);

       // newNode->isBusy_= 1;
        newNode->next_ = NULL;
        newNode->pData_ = pData;
        newNode->logInsert_ = plog;
        newNode->logRemove_ = NULL;
        
        //printOneNode(newNode);
        NVRAM::Flush(sizeof(LogQueueNode), (const void*)newNode);

        msQueue_->enq(plog);

        *threadLogEntry = NULL;
        NVRAM::Flush(sizeof(LogEntry *), (const void*)threadLogEntry);

        //plog->isBusy_= 0;
        //NVRAM::Flush(sizeof(LogEntry), (const void*)plog);
        logEntryPool_->ReleaseNode(thread_index, plog);
        
    }
 
    bool dequeue(size_t thread_index) {
        LogEntry * plog = (LogEntry *)logEntryPool_->AllocateNode(thread_index);
        plog->node_ = NULL;
        plog->status_ = 0;
        plog->operation_ = 1;
        plog->operationNum_ = 0;
        //plog->isBusy_ = 1;
        NVRAM::Flush(sizeof(LogEntry), (const void*)plog);
        
        LogEntry ** threadLogEntry = threadLogEntry_ + thread_index*8;
        *threadLogEntry = plog;
        NVRAM::Flush(sizeof(LogEntry *), (const void*)threadLogEntry);

        bool flg = msQueue_->deq(plog);
        
        LogQueueNode * deqNode = plog->node_;
        if(deqNode != NULL) {
            //deqNode->isBusy_= 0;
            //NVRAM::Flush(sizeof(LogQueueNode), (const void*)deqNode);
            nodePool_->ReleaseNode(thread_index, deqNode);
        }

        *threadLogEntry = NULL;
        NVRAM::Flush(sizeof(LogEntry *), (const void*)threadLogEntry);

        //plog->isBusy_= 0;
        //NVRAM::Flush(sizeof(LogEntry), (const void*)plog);
        logEntryPool_->ReleaseNode(thread_index, plog);
        return flg;
    }

    uint64_t getDescriptorSizeSize() {
        return 0;
    }

    virtual void initDescriptorPool(SharedMemorySegment* segment){
    }


    virtual NodePool* getLogEntryNodePool() {
        return NULL;
    }

    virtual BaseDescriptorPool* getDescPool() {
        return NULL;
    }

    

    LogQueue * msQueue_;
    
    LogEntry ** threadLogEntry_;
    LogEntry * logEntryPtr_;
    NodePool * logEntryPool_;

};

}



//////////////////////////////////////////////////////////////////////////////////////////////////
using namespace pmwcas;

void doTest(MSQueueTestBaseV1 & test) {
    std::cout << "Starting MSQueue benchmark..." << std::endl;
    test.Run(FLAGS_threads, FLAGS_seconds,
        static_cast<AffinityPattern>(FLAGS_affinity),
        FLAGS_metrics_dump_interval);
    printf("multiple cas: opCnt:%.0f, sec:%.3f,  %.2f ops/sec\n", (double)test.GetOperationCount(), 
  	  test.GetRunSeconds(),  (double)test.GetOperationCount() / test.GetRunSeconds());

    double opPerSec = (double)test.GetTotalSuccess() / test.GetRunSeconds();
    printf("totalSuc:%.0f, sec:%.3f, %.2f successful ops/sec\n",
  	        (double)test.GetTotalSuccess(), test.GetRunSeconds(), opPerSec);
}

void runBenchmark() {
    DumpArgs();
    std::cout << "> Args node_size " << FLAGS_node_size << std::endl;
    std::cout << "> Args queue_impl_type " << FLAGS_queue_impl_type << std::endl;

    if(FLAGS_queue_impl_type == 0 || FLAGS_queue_impl_type == 1
        || FLAGS_queue_impl_type == 2) {
        std::cout << "************MSQueueByOrgCasTestV1 test***************" << std::endl;
        MSQueueByOrgCasTestV1 test;
        doTest(test);
    }
    else if(FLAGS_queue_impl_type == 3) {
        std::cout << "************MSQueuePMWCasV2TestV1 test***************" << std::endl;
        MSQueuePMWCasV2TestV1 test;
        doTest(test);
    }
    else if(FLAGS_queue_impl_type == 4) {
        std::cout << "************MSQueuePMWCasV3TestV1 test***************" << std::endl;
        MSQueuePMWCasV3TestV1 test;
        doTest(test);
    }
    else if(FLAGS_queue_impl_type == 5) {
        std::cout << "************MSLogQueueTestV1 test***************" << std::endl;
        MSLogQueueTestV1 test;
        doTest(test);
    }

}

int main(int argc, char* argv[]) {
    std::cout << "main is being executed now" << std::endl;
    google::InitGoogleLogging(argv[0]);
    google::ParseCommandLineFlags(&argc, &argv, true);
  
    FLAGS_log_dir = "./";
    pmwcas::InitLibrary(pmwcas::TlsAllocator::Create,
                      pmwcas::TlsAllocator::Destroy,
                      pmwcas::LinuxEnvironment::Create,
                      pmwcas::LinuxEnvironment::Destroy);
    runBenchmark();
    return 0;
}



























