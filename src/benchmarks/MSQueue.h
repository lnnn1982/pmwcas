#pragma once

#include "mwcas_common.h"
#include "PMWCasOpWrapper.h"

namespace pmwcas {

class QueueNode {
public:
    uint64_t * pData_;
    QueueNode * next_;

    QueueNode(uint64_t * pData) : pData_(pData), next_(NULL) {}
};

class MSQueue {
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
    void deq(QueueNode ** privateAddr);


private:
    PMWCasOpWrapper casOpWrapper_;
    CasPtr* eqTargetAddrVec_[3];
    uint64_t eqOldValVec_[3];
    uint64_t eqNewValVec_[3];

    CasPtr* dqTargetAddrVec_[2];
    uint64_t dqOldValVec_[2];
    uint64_t dqNewValVec_[2];

};























}




















