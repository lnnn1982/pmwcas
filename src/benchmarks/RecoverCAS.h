#pragma once

#include "mwcas_common.h"

namespace pmwcas {

class RCAS {
public:
    RCAS(uint64_t * targetAddr, uint64_t * processInfoStore,
        uint64_t maxThreads)
        : targetAddr_(targetAddr), processInfoStore_(processInfoStore), maxThread_(maxThreads) 
    {
        /*std::cout << "construct targetAddr_:" << std::hex << targetAddr_
            << ", processInfoStore_:" << std::hex << processInfoStore_
            << ", maxThread_:" << std::hex << maxThread_
            << ", this:" << std::hex << this
            << std::endl;*/
    }

    bool Cas(uint64_t oldValue, uint64_t newValue, uint64_t seq, uint64_t processId);
    uint64_t readValue();

    //lower 48 bits
    static const uint64_t valueFlg = (uint64_t)0XFFFFFFFFFFFF;
    static const uint64_t processIdFlg = (uint64_t)0XFF << 48;
    static const uint64_t seqIdFlg = (uint64_t)0XFF << 56;
    static const uint64_t processBoolFlg = ~seqIdFlg;
    
private:
    //seq 8 bit; processId 8 bit; value 48bit
    uint64_t * targetAddr_;
    uint64_t * processInfoStore_;
    uint64_t maxThread_;

};























}














