#include "RecoverCAS.h"

namespace pmwcas {

bool RCAS::Cas(uint64_t oldValue, uint64_t newValue, uint64_t seq, uint64_t processId)
{
    uint64_t existTargetValue = (*targetAddr_);
    uint64_t existValue = existTargetValue & valueFlg;
    uint64_t existProcessId = (uint64_t)(existTargetValue & processIdFlg) >> 48;
    uint64_t existSeqId = (uint64_t)(existTargetValue & seqIdFlg)>>56;
    
    if(existProcessId >= maxThread_) {
        std::cout << "existTargetValue:" << std::hex << existTargetValue
            << ", existProcessId:" << std::hex <<existProcessId
            << ", existSeqId:" << std::hex <<existSeqId
            << ", existValue:" << std::hex <<existValue
            << ", targetAddr_:" << std::hex << targetAddr_
            << ", processInfoStore_:" << std::hex << processInfoStore_
            << ", maxThread_:" << std::hex << maxThread_
            << ", this:" << std::hex << this
            << ", oldValue:" << std::hex << oldValue
            << ", newValue:" << std::hex << newValue
            << ", processId:" << std::hex << processId
            << std::endl;
        RAW_CHECK(existProcessId >= 0 && existProcessId < maxThread_, 
            "existProcessId out of bound");
    }

    if(existValue != oldValue) return false;

    //need this. because last suc thread may fail without flushing the addr
    NVRAM::Flush(sizeof(uint64_t), (const void*)(targetAddr_));
    
    uint64_t otherProcessNewInfo = (existSeqId << 56) | 1;
    uint64_t otherProcessOldInfo = (existSeqId << 56) | 0;

    uint64_t * otherProcessStore = processInfoStore_ + existProcessId;

    CompareExchange64(otherProcessStore, otherProcessNewInfo, otherProcessOldInfo);
    //need do flush every time. If only the successful one did flush, then if it crashed,
    //other thread will never do the flush again.
    NVRAM::Flush(sizeof(uint64_t), (const void*)(otherProcessStore));

    uint64_t * processStore = processInfoStore_ + processId;
    *processStore = (seq << 56) | 0;
    NVRAM::Flush(sizeof(uint64_t), (const void*)(processStore));

    uint64_t newTargetValue = (seq<<56) | (processId<<48) | newValue;
    if(CompareExchange64(targetAddr_, newTargetValue, existTargetValue)
        == existTargetValue)
    {
        NVRAM::Flush(sizeof(uint64_t), (const void*)(targetAddr_));
        return true;
    }
    else {
        NVRAM::Flush(sizeof(uint64_t), (const void*)(targetAddr_));
        return false;
    }

}

//need to flush because the reader may use this value. If it is not flushed,
//when recovering, it maybe back to the original value.
uint64_t RCAS::readValue() {
    uint64_t existTargetValue = (*targetAddr_);
    NVRAM::Flush(sizeof(uint64_t), (const void*)(targetAddr_));
    return existTargetValue & valueFlg;
}















}

