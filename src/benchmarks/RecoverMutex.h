#pragma once

#include "mwcas_benchmark.h"
#include "fetchStoreStore.h"


namespace pmwcas {

struct QNode {
    QNode * prev;
    QNode * next;
    bool linked;

    QNode() : prev(this), next(NULL), linked(false) {}
};

class RecoverMutex {
public:
    RecoverMutex(QNode * tail) : tail_(tail) {}
    void lock();
    void unlock();

    void setMyNode(QNode * myNode) {
        myNode_ = myNode;
    }

    QNode * getTail() {
        return tail_;
    }

    virtual void FASAS(uint64_t * targetNodeAddr, uint64_t * storeNodeAddr,
        uint64_t value) = 0;
    virtual bool DCAS(uint64_t * wordAddr1, uint64_t * wordAddr2, uint64_t oldValue1,
        uint64_t oldValue2, uint64_t newValue1, uint64_t newValue2) = 0;

protected:
    QNode * tail_;
    static thread_local QNode * myNode_;
    static FetchStoreStore exec_;
};

class RecoverMutexUsingOrgMwcas : public RecoverMutex {
public:
    RecoverMutexUsingOrgMwcas(DescriptorPool* descPool, QNode * tail) : RecoverMutex(tail),
        descPool_(descPool) {}

    virtual void FASAS(uint64_t * targetNodeAddr, uint64_t * storeNodeAddr,
        uint64_t value);
    virtual bool DCAS(uint64_t * wordAddr1, uint64_t * wordAddr2, uint64_t oldValue1,
        uint64_t oldValue2, uint64_t newValue1, uint64_t newValue2);

private:
    DescriptorPool* descPool_;

};

class RecoverMutexNew : public RecoverMutex {
public:
    RecoverMutexNew(FASASDescriptorPool* fasasDescPool, QNode * tail) : RecoverMutex(tail),  
        fasasDescPool_(fasasDescPool) {}

    virtual void FASAS(uint64_t * targetNodeAddr, uint64_t * storeNodeAddr,
        uint64_t value);
    virtual bool DCAS(uint64_t * wordAddr1, uint64_t * wordAddr2, uint64_t oldValue1,
        uint64_t oldValue2, uint64_t newValue1, uint64_t newValue2);


private:
    FASASDescriptorPool* fasasDescPool_;

};




}















