#include "MSQueue.h"

namespace pmwcas {

void MSQueueByOrgCas::enq(OrgCasNode ** privateAddr) {
    OrgCasNode * newNode = (*privateAddr);
    RAW_CHECK(nullptr != newNode, "MSQueueByOrgCas::enq node null");

    while(true) {
        QueueNode * last = (*ptail_);
        QueueNode * next = last->next_;
        if(last == (*ptail_)) {
            if(next == NULL) {
                if(CompareExchange64((QueueNode **)(&(last->next_)), (QueueNode *)newNode, (QueueNode *)next) == next) {
                    NVRAM::Flush(sizeof(newNode), (const void*)(&(last->next_)));
                    *privateAddr = NULL;
                    //recover not need to know the acutuall operation.
                    //NVRAM::Flush(sizeof(OrgCasNode *), (const void*)(privateAddr));
                    CompareExchange64(ptail_, (QueueNode *)newNode, last);
                    return;
                }
            }
            else {
                NVRAM::Flush(sizeof(newNode), (const void*)(&(last->next_)));
                CompareExchange64(ptail_, next, last);
            }
        } 
    }
}


bool MSQueueByOrgCas::deq(OrgCasNode ** privateAddr, size_t thread_index) {                      
    while (true) {
        OrgCasNode * first = (OrgCasNode *)(*phead_);
        OrgCasNode * last = (OrgCasNode *)(*ptail_);
        OrgCasNode * next = (OrgCasNode *)first->next_;

        if(first == (*phead_)) {
            if(first == last) {
                //empty
                if(next == NULL) {
                    return false;
                }
                else {
                    NVRAM::Flush(sizeof(OrgCasNode *), (const void*)(&(last->next_)));
                    CompareExchange64(ptail_, (QueueNode *)next, (QueueNode *)last);
                }
            }
            else {
                if(first->del_thread_index_ != -1) {
                    continue;
                }
                
                *privateAddr = first;
                NVRAM::Flush(sizeof(OrgCasNode *), (const void*)(privateAddr));
            
                if(CompareExchange64(&first->del_thread_index_, thread_index, (size_t)-1) == -1) {
                    NVRAM::Flush(sizeof(first->del_thread_index_), (const void*)(&first->del_thread_index_));
                    CompareExchange64(phead_, (QueueNode*)next, (QueueNode*)first);
                    //LOG(ERROR) << "deq suc thread_index " << thread_index;
                    return true;
                }
                else {
                    CompareExchange64(phead_, (QueueNode*)next, (QueueNode*)first);
                }
            }
        }
    }
}












}



