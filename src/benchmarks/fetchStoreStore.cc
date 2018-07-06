#include "fetchStoreStore.h"

using namespace pmwcas::benchmark;

namespace pmwcas {


void FetchStoreStore::process(CasPtr* targetAddr, CasPtr* storeAddr, 
	   uint64_t newval)
{
    /*if(targetAddr == storeAddr) {
	    LOG(ERROR) << "target == store addr";
	}*/
	
    while(1) {
		uint64_t targetValue = targetAddr->GetValueProtected();
		uint64_t storeValue = storeAddr->GetValueProtected();
		
		Descriptor* descriptor = descriptor_pool_->AllocateDescriptor();
		CHECK_NOTNULL(descriptor);
		
		descriptor->AddEntry((uint64_t*)(targetAddr), targetValue,
			newval);
		descriptor->AddEntry((uint64_t*)(storeAddr), storeValue,
			targetValue);
	
		if(descriptor->MwCAS()) {
			break;
		}
	}
}




















}


