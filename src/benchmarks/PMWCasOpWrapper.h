#pragma once

#include "mwcas_common.h"


namespace pmwcas {

class PMWCasOpWrapper {
public:
	PMWCasOpWrapper(DescriptorPool* descPool) : descPool_(descPool) {}
    
	bool mwcas(CasPtr** targetAddrVec,
        uint64_t * oldValVec, 
        uint64_t * newValVec,
        int len);
   


private:
    void epochProtect(DescriptorPool* descPool);
        
    static thread_local uint64_t epochs_;
    const uint64_t kEpochThreshold_ = 100;

    DescriptorPool* descPool_;
};


}