#include "MSQueue.h"

namespace pmwcas {

void MSQueueByPMWCas::enq(QueueNode ** privateAddr) {
    QueueNode * newNode = (*privateAddr);
    while(true) {
        QueueNode * last = (QueueNode *)(((CasPtr *)(ptail_))->GetValueProtected());
        if(last == (*ptail_)) {
            eqTargetAddrVec_[0] = (CasPtr *)(&last->next_);
            eqOldValVec_[0] = 0;
            eqNewValVec_[0] = (uint64_t)(newNode);

            eqTargetAddrVec_[1] = (CasPtr *)(ptail_);
            eqOldValVec_[1] = (uint64_t)last;
            eqNewValVec_[1] = (uint64_t)(newNode);

            eqTargetAddrVec_[2] = (CasPtr *)(privateAddr);
            eqOldValVec_[2] = (uint64_t)newNode;
            eqNewValVec_[2] = 0;

            if(casOpWrapper_.mwcas(eqTargetAddrVec_, eqOldValVec_, eqNewValVec_, 3)) {
                return;
            }
        }
    }
}

void MSQueueByPMWCas::deq(QueueNode ** privateAddr) {
    while (true) {
        QueueNode * first = (QueueNode *)(((CasPtr *)(phead_))->GetValueProtected());
        QueueNode * last = (QueueNode *)(((CasPtr *)(ptail_))->GetValueProtected());

        if(first == (*phead_)) {
            if(first == last) {
                //empty
                if(first->next_ == NULL) {
                    return;
                }
            }
            else {
                dqTargetAddrVec_[0] = (CasPtr *)(phead_);
                dqOldValVec_[0] = (uint64_t)first;
                dqNewValVec_[0] = (uint64_t)(first->next_);

                dqTargetAddrVec_[1] = (CasPtr *)(privateAddr);
                dqOldValVec_[1] = (uint64_t)(*privateAddr);
                //should be the next, but to reclaim the node, return the previous node.
                //dqNewValVec_[1] = first.next_;
                dqNewValVec_[1] = (uint64_t)first; 

                if(casOpWrapper_.mwcas(dqTargetAddrVec_, dqOldValVec_, dqNewValVec_, 2)) {
                    return;
                }
            }
        }
    }
}













}



