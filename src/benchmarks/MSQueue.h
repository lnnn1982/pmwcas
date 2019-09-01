#pragma once

#include "mwcas_common.h"
#include "PMWCasOpWrapper.h"
#include "fetchStoreStore.h"

namespace pmwcas {

class alignas(kCacheLineSize) QueueNode {
public:
    uint64_t * pData_;
    QueueNode * volatile next_;
    QueueNode * poolNext_;
    uint64_t isBusy_;
    uint64_t memIndex_;
};

class MSQueue {     
public:
    static void initRecord();
protected:
    MSQueue(QueueNode ** phead, QueueNode ** ptail) : phead_(phead), ptail_(ptail) {
    }


    QueueNode ** phead_;
    QueueNode ** ptail_;


};

class MSQueueByPMWCas : public MSQueue{
public:
    MSQueueByPMWCas(QueueNode ** phead, QueueNode ** ptail, DescriptorPool* descPool) 
        : MSQueue(phead, ptail), casOpWrapper_(descPool) {
    }

    void enq(QueueNode ** privateAddr);
    void deq(QueueNode ** privateAddr, size_t thread_index=0);


private:
    PMWCasOpWrapper casOpWrapper_;

};

class MSQueueByPMWCasV2 : public MSQueue{
public:
    MSQueueByPMWCasV2(QueueNode ** phead, QueueNode ** ptail, FASASDescriptorPool* descPool) 
        : MSQueue(phead, ptail), fetchSS_(), descPool_(descPool) {
    }

    void enq(QueueNode ** privateAddr);
    void deq(QueueNode ** privateAddr);


private:
    FetchStoreStore fetchSS_;
    FASASDescriptorPool * descPool_;

};


class MSQueueByPMWCasV3 : public MSQueue{
public:
    MSQueueByPMWCasV3(QueueNode ** phead, QueueNode ** ptail, OptmizedFASASDescriptorPool * descPool) 
        : MSQueue(phead, ptail), fetchSS_(), descPool_(descPool) {
    }

    void enq(QueueNode ** privateAddr, size_t thread_index);
    void deq(QueueNode ** privateAddr, size_t thread_index);


private:
    FetchStoreStore fetchSS_;
    OptmizedFASASDescriptorPool * descPool_;

};




class alignas(kCacheLineSize) OrgCasNode : public QueueNode {
public:
    size_t del_thread_index_;
};



class MSQueueByOrgCas : public MSQueue{
public:
    MSQueueByOrgCas(QueueNode ** phead, QueueNode ** ptail)
        : MSQueue(phead, ptail), isRecoverFinish_(false){
    }

    void enq(OrgCasNode ** privateAddr);
    bool deq(OrgCasNode ** privateAddr, size_t thread_index);

    //void recover(OrgCasNode ** threadEnqAddr, size_t threadCnt, size_t thread_index);
    void recover(std::unordered_map<OrgCasNode *, OrgCasNode **> const & enqNodeMap, 
        std::unordered_map<OrgCasNode *, OrgCasNode **> const & deqNodeMap,
        size_t thread_index);
    
private:
    /*void  checkEnqNode(OrgCasNode ** threadEnqAddr, size_t threadCnt,
            OrgCasNode * node) ;*/
    void checkEnqNode(std::unordered_map<OrgCasNode *, OrgCasNode **> const & enqNodeMap,
            OrgCasNode * node, size_t thread_index) ;
    void checkEnqNodeFromDeqMap(std::unordered_map<OrgCasNode *, OrgCasNode **> const & enqNodeMap,
            std::unordered_map<OrgCasNode *, OrgCasNode **> const & deqNodeMap);
    
    volatile bool isRecoverFinish_;

};




class LogQueueNode;

struct alignas(kCacheLineSize) LogEntry {
    uint64_t operationNum_;
    uint32_t operation_;
    uint32_t status_;
    LogQueueNode * volatile node_;
    LogEntry * poolNext_;
    
    LogEntry(uint64_t operationNum, uint32_t operation, uint32_t status, LogQueueNode * node)
        : operationNum_(operationNum), operation_(operation), status_(status), node_(node) {}
};

class alignas(kCacheLineSize) LogQueueNode : public QueueNode {
public:
    LogEntry * logInsert_;
    LogEntry * logRemove_;
};


class LogQueue : public MSQueue{
public:
    LogQueue(QueueNode ** phead, QueueNode ** ptail) 
        : MSQueue(phead, ptail) {
    }

    void enq(LogEntry * logEntry);
    bool deq(LogEntry * logEntry);
};

}

