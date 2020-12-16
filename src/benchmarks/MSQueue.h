#pragma once

#include "mwcas_common.h"
#include "PMWCasOpWrapper.h"
#include "fetchStoreStore.h"
#include "RecoverCAS.h"

namespace pmwcas {

class PoolNode {
public:
    PoolNode * poolNext_;
    uint64_t isBusy_;

    virtual void clear() = 0;
    
    virtual void initialize() = 0;
    
    virtual void initiCommon(uint32_t i)  = 0;
    
};

class alignas(kCacheLineSize) QueueNode : public PoolNode {
public:
    uint64_t * pData_;
    QueueNode * volatile next_;
    uint64_t memIndex_;

    virtual void clear() {
        pData_ = NULL;
        next_ = NULL;
    }

    virtual void initiCommon(uint32_t i) {
        memIndex_ = i+1;
    }

    virtual void initialize() {
        next_ = NULL;
        pData_ = NULL;
    }
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


class MSQueueByRecoverCAS : public MSQueue{
public:
    MSQueueByRecoverCAS(QueueNode ** phead, QueueNode ** ptail, RCAS * casOpArray) 
        : MSQueue(phead, ptail), casOpArray_(casOpArray) {
    }

    void enq(QueueNode ** privateAddr, size_t thread_index);
    void deq(QueueNode ** privateAddr, size_t thread_index);


private:
   RCAS * casOpArray_;

};

/*class MSDetectableQueue : public MSQueue{
public:
    MSDetectableQueue(QueueNode ** phead, QueueNode ** ptail);

    void prepareEnq(QueueNode * node)
    void enq();
    void recoverEnq(uint64_t * effect);

    void prepareDeq()
    QueueNode * deq();
    void recoverDeq(uint64_t * effect);
};*/





class alignas(kCacheLineSize) OrgCasNode : public QueueNode {
public:
    size_t del_thread_index_;

    virtual void clear() {
        QueueNode::clear();
        del_thread_index_ = -1;
    }

    virtual void initiCommon(uint32_t i) {
    }

    virtual void initialize() {
        QueueNode::initialize();
        del_thread_index_ = -1;
    }
};



class MSQueueByOrgCas : public MSQueue{
public:
    MSQueueByOrgCas(QueueNode ** phead, QueueNode ** ptail)
        : MSQueue(phead, ptail)
        //, isRecoverFinish_(false)
    {
    }

    void enq(OrgCasNode ** privateAddr, int detectType = 0);
    bool deq(OrgCasNode ** privateAddr, size_t thread_index, int detectType = 0);

    //void recover(OrgCasNode ** threadEnqAddr, size_t threadCnt, size_t thread_index);
    void recover(std::unordered_map<OrgCasNode *, OrgCasNode **> const & enqNodeMap, 
        std::unordered_map<OrgCasNode *, OrgCasNode **> const & deqNodeMap,
        size_t thread_index);

    void recover();
    
private:
    /*void  checkEnqNode(OrgCasNode ** threadEnqAddr, size_t threadCnt,
            OrgCasNode * node) ;*/
    void checkEnqNode(std::unordered_map<OrgCasNode *, OrgCasNode **> const & enqNodeMap,
            OrgCasNode * node, size_t thread_index) ;
    void checkEnqNodeFromDeqMap(std::unordered_map<OrgCasNode *, OrgCasNode **> const & enqNodeMap,
            std::unordered_map<OrgCasNode *, OrgCasNode **> const & deqNodeMap);
    
    //volatile bool isRecoverFinish_;

};




class LogQueueNode;

struct alignas(kCacheLineSize) LogEntry : public PoolNode {
    uint64_t operationNum_;
    uint32_t operation_;
    uint32_t status_;
    LogQueueNode * volatile node_;

    virtual void clear() {
        node_ = NULL;
    }

    virtual void initiCommon(uint32_t i) {
    }

    virtual void initialize() {
        node_ = NULL;
    }

    
};

class alignas(kCacheLineSize) LogQueueNode : public QueueNode {
public:
    LogEntry * logInsert_;
    LogEntry * logRemove_;

    virtual void clear() {
        QueueNode::clear();
        logInsert_ = NULL;
        logRemove_ = NULL;
    }

    virtual void initiCommon(uint32_t i) {
    }

    virtual void initialize() {
        QueueNode::initialize();
        logInsert_ = NULL;
        logRemove_ = NULL;
    }
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

