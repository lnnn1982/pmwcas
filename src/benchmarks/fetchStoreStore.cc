#include "fetchStoreStore.h"

namespace pmwcas {

thread_local uint64_t FetchStoreStore::epochs_ = 0;

uint64_t FetchStoreStore::processByOrgMwcas(CasPtr* targetAddr, CasPtr* storeAddr, 
	   uint64_t newval, DescriptorPool* descPool)
{
    /*if(targetAddr == storeAddr) {
	    LOG(ERROR) << "target == store addr";
	}*/
	
    while(1) {
        epochProtect(descPool);
        
		uint64_t targetValue = targetAddr->GetValueProtected();
        RAW_CHECK(Descriptor::IsCleanPtr(targetValue), "targetValue not valid");
		uint64_t storeValue = storeAddr->GetValueProtected();
        RAW_CHECK(Descriptor::IsCleanPtr(storeValue), "storeValue not valid");
		
		Descriptor* descriptor = descPool->AllocateDescriptor();
		CHECK_NOTNULL(descriptor);
		
		descriptor->AddEntry((uint64_t*)(targetAddr), targetValue,
			newval);
		descriptor->AddEntry((uint64_t*)(storeAddr), storeValue,
			targetValue);
	
		if(descriptor->MwCAS()) {
            RAW_CHECK(targetValue == storeAddr->GetValueProtected(),
                "old share value not equal to private value");
            /*uint64_t storeValue = (*storeAddr);
            if(!Descriptor::IsCleanPtr(storeValue)) {
                
                std::cout << "storeValue value: " << storeValue << ", target value:" << targetValue << std::endl;
                RAW_CHECK(false, "11111111111111111");
            }*/

			 return targetValue;
		}
	}

   
}

bool FetchStoreStore::dcasByOrgMwcas(CasPtr* targetAddr1, CasPtr* targetAddr2,
        uint64_t oldVal1, uint64_t oldVal2, 
        uint64_t newVal1, uint64_t newVal2, 
        DescriptorPool* descPool)
{
    epochProtect(descPool);
        
    Descriptor* descriptor = descPool->AllocateDescriptor();
    CHECK_NOTNULL(descriptor);
		
	descriptor->AddEntry((uint64_t*)(targetAddr1), oldVal1,
			newVal1);
	descriptor->AddEntry((uint64_t*)(targetAddr2), oldVal2,
			newVal2);

    return descriptor->MwCAS();
}

uint64_t FetchStoreStore::process(FASASCasPtr* shareAddr, FASASCasPtr* privateAddr, 
	   uint64_t newval, FASASDescriptorPool* fasasDescPool)
{
    while(1) {
        epochProtect(fasasDescPool);
                
		uint64_t targetValue = shareAddr->getValueProtectedOfSharedVar();
        RAW_CHECK(Descriptor::IsCleanPtr(targetValue), "targetValue not valid");
		
		FASASDescriptor* descriptor = fasasDescPool->AllocateDescriptor();
		CHECK_NOTNULL(descriptor);

        descriptor->setOpType(FASASDescriptor::FASASOp);
		descriptor->addSharedWord((uint64_t*)(shareAddr), targetValue,
			newval);
		descriptor->setPrivateAddress((uint64_t*)(privateAddr));
	
		if(descriptor->process()) {
            RAW_CHECK(targetValue == privateAddr->getValueOfPrivateVar(),
                "old share value not equal to private value");
			return targetValue;
		}
	}
}

bool FetchStoreStore::dcas(FASASCasPtr* targetAddr1, FASASCasPtr* targetAddr2, 
   uint64_t oldVal1, uint64_t oldVal2, uint64_t newVal1, uint64_t newVal2,
   FASASDescriptorPool* fasasDescPool)
{
    epochProtect(fasasDescPool);
    
    FASASDescriptor* descriptor = fasasDescPool->AllocateDescriptor();
	CHECK_NOTNULL(descriptor);

    descriptor->setOpType(FASASDescriptor::setOpTypeFlg(newVal2, FASASDescriptor::DoubleCASOp));
    descriptor->addSharedWord((uint64_t*)(targetAddr1), oldVal1,
			newVal1);
	descriptor->setPrivateAddress((uint64_t*)(targetAddr2));

    return descriptor->process();
}

bool FetchStoreStore::recoverCas(FASASCasPtr* shareAddr, FASASCasPtr* privateAddr, 
   uint64_t oldVal, uint64_t newVal, FASASDescriptorPool* fasasDescPool)
{
    epochProtect(fasasDescPool);
    
    FASASDescriptor* descriptor = fasasDescPool->AllocateDescriptor();
	CHECK_NOTNULL(descriptor);

    descriptor->setOpType(FASASDescriptor::RecoverCASOp);
    descriptor->addSharedWord((uint64_t*)(shareAddr), oldVal,
			newVal);
	descriptor->setPrivateAddress((uint64_t*)(privateAddr));

    return descriptor->process();
}


uint64_t FetchStoreStore::fasas(uint64_t * shareAddr, uint64_t * privateAddr, 
   uint64_t newShareVal, uint32_t locId, uint16_t processId, 
   OptmizedFASASDescriptorPool * fasasDescPool)
{
    OptmizedFASASDescriptor* descriptor = fasasDescPool->getDescriptor(processId, locId);
    CHECK_NOTNULL(descriptor);

    descriptor->setSharedAddress(shareAddr);
    descriptor->setPrivateAddress(privateAddr);
    descriptor->setOpType(FASASDescriptor::FASASOp);
        
    while(1) {
        uint64_t oldShareValue = OptmizedFASASDescriptor::getValueProtectedOfSharedVar(
                shareAddr, *fasasDescPool, locId, processId);
	
		if(descriptor->process(oldShareValue, newShareVal, *fasasDescPool, locId)) {
            RAW_CHECK(oldShareValue == OptmizedFASASDescriptor::getValueOfPrivateVar(privateAddr),
                "old share value not equal to private value");
			return oldShareValue;
		}
	}
}

uint64_t FetchStoreStore::read(uint64_t * shareAddr, uint32_t locId, uint16_t processId,
	   OptmizedFASASDescriptorPool * fasasDescPool)
{
    OptmizedFASASDescriptor* descriptor = fasasDescPool->getDescriptor(processId, locId);
    CHECK_NOTNULL(descriptor);

    descriptor->setSharedAddress(shareAddr);
    
    return OptmizedFASASDescriptor::getValueProtectedOfSharedVar(
                shareAddr, *fasasDescPool, locId, processId);
}
   
bool FetchStoreStore::dcas(uint64_t * shareAddr, uint64_t * privateAddr, 
   uint64_t oldShareVal, uint64_t newShareVal,
   uint64_t newPrivateVal, uint32_t locId, uint16_t processId, 
   OptmizedFASASDescriptorPool * fasasDescPool)
{
    OptmizedFASASDescriptor* descriptor = fasasDescPool->getDescriptor(processId, locId);
    CHECK_NOTNULL(descriptor);

    descriptor->setSharedAddress(shareAddr);
    descriptor->setPrivateAddress(privateAddr);
    
    descriptor->setOpType(FASASDescriptor::setOpTypeFlg(
        newPrivateVal, FASASDescriptor::DoubleCASOp));

    return descriptor->process(oldShareVal, newShareVal, *fasasDescPool, locId);
}
   
bool FetchStoreStore::recoverCas(uint64_t * shareAddr, uint64_t * privateAddr, 
   uint64_t oldShareVal, uint64_t newShareVal, 
   uint32_t locId, uint16_t processId,
   OptmizedFASASDescriptorPool * fasasDescPool)
{
    OptmizedFASASDescriptor* descriptor = fasasDescPool->getDescriptor(processId, locId);
    CHECK_NOTNULL(descriptor);

    descriptor->setSharedAddress(shareAddr);
    descriptor->setPrivateAddress(privateAddr);

    descriptor->setOpType(FASASDescriptor::RecoverCASOp);

    return descriptor->process(oldShareVal, newShareVal, *fasasDescPool, locId);
}

void FetchStoreStore::epochProtect(BaseDescriptorPool* descPool) {
    if(++epochs_ == kEpochThreshold_) {
        descPool->GetEpoch()->Unprotect();
        descPool->GetEpoch()->Protect();
        epochs_ = 0;
    }
}














}


