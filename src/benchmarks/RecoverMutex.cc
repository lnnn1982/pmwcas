#include "RecoverMutex.h"

namespace pmwcas {

thread_local QNode * RecoverMutex::myNode_ = NULL;
FetchStoreStore RecoverMutex::exec_;

void RecoverMutex::lock() {
    QNode * prev = myNode_->prev;
    if(prev == myNode_) {
        myNode_->next = NULL;
        myNode_->linked = false;
        FASAS((uint64_t *)(&tail_), (uint64_t *)(&(myNode_->prev)), (uint64_t)(myNode_));
        prev = myNode_->prev;
    }

    if(prev != NULL) {
        DCAS((uint64_t *)(&prev->next), (uint64_t *)(&myNode_->linked), (uint64_t)NULL, 
            (uint64_t)(false), (uint64_t)myNode_, (uint64_t)(true));
        while(myNode_->prev != NULL);
    }
}

void RecoverMutex::unlock() {
    if (!DCAS((uint64_t *)(&tail_), (uint64_t *)(&myNode_->prev), 
        (uint64_t)myNode_, (uint64_t)NULL, (uint64_t)NULL, (uint64_t)myNode_))
    {
        while(myNode_->next == NULL);
        FASAS((uint64_t *)(&(myNode_->next->prev)), (uint64_t *)(&(myNode_->prev)), 
            (uint64_t)(NULL));
    }
}

void RecoverMutexUsingOrgMwcas::FASAS(uint64_t * targetNodeAddr, uint64_t * storeNodeAddr,
    uint64_t value) 
{
    exec_.processByOrgMwcas((CasPtr *)targetNodeAddr, (CasPtr *)storeNodeAddr, 
        value, descPool_);
}
    
bool RecoverMutexUsingOrgMwcas::DCAS(uint64_t * wordAddr1, uint64_t * wordAddr2, 
    uint64_t oldValue1, uint64_t oldValue2, uint64_t newValue1, uint64_t newValue2)
{
    return exec_.dcasByOrgMwcas((CasPtr *)wordAddr1, (CasPtr *)wordAddr2, oldValue1,
        oldValue2, newValue1, newValue2, descPool_);
}

void RecoverMutexNew::FASAS(uint64_t * targetNodeAddr, uint64_t * storeNodeAddr,
    uint64_t value) 
{
    exec_.process((FASASCasPtr *)targetNodeAddr, (FASASCasPtr *)storeNodeAddr, 
        value, fasasDescPool_);
}
    
bool RecoverMutexNew::DCAS(uint64_t * wordAddr1, uint64_t * wordAddr2, 
    uint64_t oldValue1, uint64_t oldValue2, uint64_t newValue1, uint64_t newValue2)
{
    return exec_.dcasByMwcas((FASASCasPtr *)wordAddr1, (FASASCasPtr *)wordAddr1,
        oldValue1, oldValue2, newValue1, newValue2, fasasDescPool_);
}





}


