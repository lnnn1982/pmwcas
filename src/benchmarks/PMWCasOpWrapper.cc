#include "PMWCasOpWrapper.h"

namespace pmwcas {

thread_local uint64_t PMWCasOpWrapper::epochs_ = 0;

bool PMWCasOpWrapper::mwcas(CasPtr** targetAddrVec,
        uint64_t * oldValVec, 
        uint64_t * newValVec,
        int len)
{   
    epochProtect(descPool_);
        
    Descriptor* descriptor = descPool_->AllocateDescriptor();
    CHECK_NOTNULL(descriptor);

    for(int i = 0; i < len; i++) {
	    descriptor->AddEntry((uint64_t*)(targetAddrVec[i]), oldValVec[i],
			newValVec[i]);
    }
    
    return descriptor->MwCAS();
}


void PMWCasOpWrapper::epochProtect(DescriptorPool* descPool) {
    if(++epochs_ == kEpochThreshold_) {
        descPool->GetEpoch()->Unprotect();
        descPool->GetEpoch()->Protect();
        epochs_ = 0;
    }
}

}