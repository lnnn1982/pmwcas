#include "MSQueue.h"

namespace pmwcas {

void MSQueueByPMWCasV2::enq(QueueNode ** privateAddr) {
    QueueNode * newNode = (*privateAddr);
    RAW_CHECK(nullptr != newNode, "MSQueueByPMWCasV2::enq node null");

    while(true) {
        QueueNode * last = (*ptail_);
        QueueNode * next = (QueueNode *)(((FASASCasPtr *)(&(last->next_)))->getValueProtectedOfSharedVar());
     
        if(last == (*ptail_)) {
            if(next == NULL) {
                if(fetchSS_.dcas((FASASCasPtr *)(&(last->next_)), (FASASCasPtr *)(privateAddr),
                    0, (uint64_t)newNode, (uint64_t)(newNode), 0, descPool_)) {
                    CompareExchange64(ptail_, newNode, last);
                    return;
                }
            }
            else {
                CompareExchange64(ptail_, next, last);
            }
        } 
    }
}

void MSQueueByPMWCasV2::deq(QueueNode ** privateAddr) {                      
    while (true) {
        QueueNode * first = (QueueNode *)(((FASASCasPtr *)(phead_))->getValueProtectedOfSharedVar());
        QueueNode * last = (*ptail_);
        QueueNode * firstNext = (QueueNode *)(((FASASCasPtr *)(&(first->next_)))->getValueProtectedOfSharedVar());

        if(first == (*phead_)) {
            if(first == last) {
                //empty
                if(firstNext == NULL) {
                    return;
                }
                else {
                    CompareExchange64(ptail_, firstNext, last);
                }
            }
            else {
                if(fetchSS_.dcas((FASASCasPtr *)(phead_), (FASASCasPtr *)(privateAddr),
                    (uint64_t)first, 0, (uint64_t)(firstNext), (uint64_t)first, descPool_)) {
                    return;
                }
            }
        }
    }
}


}













