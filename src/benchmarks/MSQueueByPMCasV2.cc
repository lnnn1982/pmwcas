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

void MSQueueByPMWCasV3::enq(QueueNode ** privateAddr, size_t thread_index) {
    QueueNode * newNode = (*privateAddr);
    RAW_CHECK(nullptr != newNode, "MSQueueByPMWCasV3::enq node null");

    while(true) {
        QueueNode * last = (*ptail_);
        QueueNode * next = (QueueNode *)(fetchSS_.read((uint64_t *)(&(last->next_)), last->memIndex_,
            thread_index, descPool_));
     
        if(last == (*ptail_)) {
            if(next == NULL) {
                if(fetchSS_.dcas((uint64_t *)(&(last->next_)), (uint64_t *)(privateAddr),
                    0, (uint64_t)newNode, 0, last->memIndex_, (uint16_t)thread_index, descPool_)) {
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

void MSQueueByPMWCasV3::deq(QueueNode ** privateAddr, size_t thread_index) {                      
    while (true) {
        QueueNode * first = (QueueNode *)(fetchSS_.read((uint64_t *)(phead_), 0,
            thread_index, descPool_));
        QueueNode * last = (*ptail_);
        QueueNode * firstNext = (QueueNode *)(fetchSS_.read((uint64_t *)(&(first->next_)), first->memIndex_,
            thread_index, descPool_));

        if((uint64_t)first == ((uint64_t)(*phead_) & OptmizedFASASDescriptor::ActualValueFlg)) {
            //std::cout << "aaaaaaaaaaaaaaaaaaa" << std::endl;
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
                if(fetchSS_.dcas((uint64_t *)(phead_), (uint64_t *)(privateAddr),
                     (uint64_t)first, (uint64_t)(firstNext), (uint64_t)first, 
                     0, (uint16_t)thread_index, descPool_))
                {
                    return;
                }
            }
        }
    }
}

























}













