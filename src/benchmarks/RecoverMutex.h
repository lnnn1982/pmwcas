#pragma once

#include "mwcas_common.h"
#include "fetchStoreStore.h"


namespace pmwcas {

struct QNode {
    //volatile char padding[64];
    
    QNode * volatile prev;
    volatile char padding[56];
    QNode * volatile next;
    volatile char padding1[56];
    volatile uint64_t linked;
    volatile char padding2[56];

    QNode() : prev(this), next(NULL), linked(0) {}
};

class RecoverMutex {
public:
    RecoverMutex(QNode ** tailPtr) : tailPtr_(tailPtr) {}
    virtual void lock() = 0;
    virtual void unlock() = 0;

    void setMyNode(QNode * myNode) {
        myNode_ = myNode;
    }

    QNode * getMyNode() {
        return myNode_;
    }

    QNode ** getTail() {
        return tailPtr_;
    }

    virtual void FASAS(uint64_t * targetNodeAddr, uint64_t * storeNodeAddr,
        uint64_t value) = 0;
    virtual bool DCAS(uint64_t * wordAddr1, uint64_t * wordAddr2, uint64_t oldValue1,
        uint64_t oldValue2, uint64_t newValue1, uint64_t newValue2) = 0;

protected:
    QNode ** tailPtr_;
    static thread_local QNode * myNode_;
    static FetchStoreStore exec_;
};

class RecoverMutexUsingOrgMwcas : public RecoverMutex {
public:
    RecoverMutexUsingOrgMwcas(DescriptorPool* descPool, QNode ** tailPtr) : RecoverMutex(tailPtr),
        descPool_(descPool) {}
    virtual void lock();
    virtual void unlock();
    virtual void FASAS(uint64_t * targetNodeAddr, uint64_t * storeNodeAddr,
        uint64_t value);
    virtual bool DCAS(uint64_t * wordAddr1, uint64_t * wordAddr2, uint64_t oldValue1,
        uint64_t oldValue2, uint64_t newValue1, uint64_t newValue2);

private:
    DescriptorPool* descPool_;

};

class RecoverMutexNew : public RecoverMutex {
public:
    RecoverMutexNew(FASASDescriptorPool* fasasDescPool, QNode ** tailPtr) : RecoverMutex(tailPtr),  
        fasasDescPool_(fasasDescPool) {}
    virtual void lock();
    virtual void unlock();
    virtual void FASAS(uint64_t * targetNodeAddr, uint64_t * storeNodeAddr,
        uint64_t value);
    virtual bool DCAS(uint64_t * wordAddr1, uint64_t * wordAddr2, uint64_t oldValue1,
        uint64_t oldValue2, uint64_t newValue1, uint64_t newValue2);


private:
    FASASDescriptorPool* fasasDescPool_;

};




}















