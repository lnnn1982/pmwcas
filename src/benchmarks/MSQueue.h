#pragma once

#include "mwcas_common.h"
#include "PMWCasOpWrapper.h"

namespace pmwcas {

class alignas(kCacheLineSize) QueueNode {
public:
    uint64_t * pData_;
    QueueNode * volatile next_;
    QueueNode * poolNext_;
    uint64_t isBusy_;
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
    void deq(QueueNode ** privateAddr, uint64_t ** deqDataAddr, size_t thread_index=0);


private:
    PMWCasOpWrapper casOpWrapper_;

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

