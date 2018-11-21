#include <list>
#include "MSQueue.h"

namespace pmwcas {

/*struct OneRecord {
    QueueNode * newNode_;
    uint64_t * pData_;
    QueueNode * tail_;
    QueueNode * lastNext_;
    QueueNode * first_;
    QueueNode * firstNext_;
    const char * des_;
    size_t thread_index_;
    QueueNode * priEnqNode_;
    QueueNode ** priEnqNodeAddr_;
    QueueNode * priDeqNode_;
    QueueNode ** priDeqNodeAddr_;
    uint64_t * priDeqData_;
    uint64_t ** priDeqDataAddr_;
    Descriptor * rdescriptor_;

    OneRecord(QueueNode * newNode, uint64_t * pData, QueueNode * tail, QueueNode * lastNext,
        QueueNode * first, QueueNode * firstNext,
        const char * des, size_t thread_index, QueueNode * priEnqNode, QueueNode ** priEnqNodeAddr,
        QueueNode * priDeqNode, QueueNode ** priDeqNodeAddr,
        uint64_t * priDeqData, uint64_t ** priDeqDataAddr, Descriptor * rdescriptor) {
        newNode_ = newNode;
        pData_ = pData;
        tail_ = tail;
        lastNext_ = lastNext;
        first_ = first;
        firstNext_ = firstNext;
        des_ = des;
        thread_index_ = thread_index;
        priEnqNode_ = priEnqNode;
        priEnqNodeAddr_ = priEnqNodeAddr;
        priDeqNode_ = priDeqNode;
        priDeqNodeAddr_ = priDeqNodeAddr;
        priDeqData_ = priDeqData;
        priDeqDataAddr_ = priDeqDataAddr;
        rdescriptor_ = rdescriptor;
    }

        void print() const {
            std::cout << "log:" << des_ << ", thread_index_:" << thread_index_ << ", newNode_:"
                << newNode_ << ", pData_:" << pData_ << ", tail_:" << tail_ << ", lastNext_:"
                << lastNext_ << ", first_:" << first_
                << ", firstNext_:" << firstNext_ 
                << ", priEnqNode_:" << priEnqNode_ << ", priEnqNodeAddr_:" << priEnqNodeAddr_
                << ", priDeqNode_:" << priDeqNode_ << ", priDeqNodeAddr_:" << priDeqNodeAddr_
                << ", priDeqData_:" << priDeqData_ << ", priDeqDataAddr_:" << priDeqDataAddr_ 
                << ", rdescriptor_:" << rdescriptor_ << std::endl;
        }
};

typedef std::list<OneRecord> OneRecordList;
OneRecordList recordList[8];
bool isStop = false;

void MSQueue::initRecord() {
    std::cout << "MSQueue::initRecord" << std::endl;
    for(int i = 0; i < 8; i++) {
        OneRecordList oneRecordList;
        recordList[i] = oneRecordList;
    }
}

void checkSleep() {
    if(isStop) {
        sleep(2);
    }
}

void addRecord(OneRecord const & record, size_t thread_index) {
    checkSleep();
    if(recordList[thread_index].size() == 40) {
        recordList[thread_index].pop_front();
        recordList[thread_index].push_back(record);
    }
    else {
        recordList[thread_index].push_back(record);
    }
}


void printOneRecord(size_t thread_index) {
    std::cout << "thread_index:" << thread_index << std::endl;
    for(OneRecord const & record : recordList[thread_index]) {
        record.print();
    }
}

void printRecord() {
    for(int i = 0; i < 8; i++) {
        printOneRecord(i);
    }
}
*/


void MSQueueByPMWCas::enq(QueueNode ** privateAddr) {
    QueueNode * newNode = (*privateAddr);
    RAW_CHECK(nullptr != newNode, "MSQueueByPMWCas::enq node null");

    CasPtr* eqTargetAddrVec_[3];
    uint64_t eqOldValVec_[3];
    uint64_t eqNewValVec_[3];

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

void MSQueueByPMWCas::deq(QueueNode ** privateAddr, size_t thread_index) {                      
    CasPtr* dqTargetAddrVec_[2];
    uint64_t dqOldValVec_[2];
    uint64_t dqNewValVec_[2];
    
    while (true) {
        QueueNode * first = (QueueNode *)(((CasPtr *)(phead_))->GetValueProtected());
        QueueNode * last = (QueueNode *)(((CasPtr *)(ptail_))->GetValueProtected());
        QueueNode * firstNext = (QueueNode *)(((CasPtr *)(&(first->next_)))->GetValueProtected());

        //possible ABA problem, first reclaimed and used as head again. But the node pool has enough nodes and node will not be recycled imediately
        //this condition means the first is not deleted yet, so firstNext cannot be NULL if first and last not equal.
        if(first == (*phead_)) {
            if(first == last) {
                //empty
                if(firstNext == NULL) {
                    return;
                }
            }
            else {
                dqTargetAddrVec_[0] = (CasPtr *)(phead_);
                dqOldValVec_[0] = (uint64_t)first;
                dqNewValVec_[0] = (uint64_t)(firstNext);

                dqTargetAddrVec_[1] = (CasPtr *)(privateAddr);
                //could be modified by other thread. the init value is zero, other thread can put previous descriptor on
                dqOldValVec_[1] = (uint64_t)(((CasPtr *)(privateAddr))->GetValueProtected());
                //should be the next, but to reclaim the node, return the previous node.
                dqNewValVec_[1] = (uint64_t)first;

                //dqTargetAddrVec_[2] = (CasPtr *)(deqDataAddr);
                //could be modified by other thread
                //dqOldValVec_[2] = (uint64_t)(((CasPtr *)(deqDataAddr))->GetValueProtected());
                //dqNewValVec_[2] = (uint64_t)(firstNext->pData_);

                if(casOpWrapper_.mwcas(dqTargetAddrVec_, dqOldValVec_, dqNewValVec_, 2)) {
                    return;
                }

            }
        }
    }
}

}



