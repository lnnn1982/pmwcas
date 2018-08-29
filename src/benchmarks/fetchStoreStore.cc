#include "fetchStoreStore.h"

using namespace pmwcas::benchmark;

namespace pmwcas {


void FetchStoreStore::processByOrgMwcas(CasPtr* targetAddr, CasPtr* storeAddr, 
	   uint64_t newval, DescriptorPool* descPool)
{
    /*if(targetAddr == storeAddr) {
	    LOG(ERROR) << "target == store addr";
	}*/
	
    while(1) {
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

			break;
		}
	}
}


bool FetchStoreStore::dcasByOrgMwcas(CasPtr* targetAddr1, CasPtr* targetAddr2,
        uint64_t oldVal1, uint64_t oldVal2, 
        uint64_t newVal1, uint64_t newVal2, 
       DescriptorPool* descPool)
{
    Descriptor* descriptor = descPool->AllocateDescriptor();
    CHECK_NOTNULL(descriptor);
		
	descriptor->AddEntry((uint64_t*)(targetAddr1), oldVal1,
			newVal1);
	descriptor->AddEntry((uint64_t*)(targetAddr2), oldVal2,
			newVal2);

    return descriptor->MwCAS();
}

void FetchStoreStore::process(FASASCasPtr* shareAddr, FASASCasPtr* privateAddr, 
	   uint64_t newval, FASASDescriptorPool* fasasDescPool)
{
    while(1) {
		uint64_t targetValue = shareAddr->getValueProtectedOfSharedVar();
        RAW_CHECK(Descriptor::IsCleanPtr(targetValue), "targetValue not valid");
		
		FASASDescriptor* descriptor = fasasDescPool->AllocateDescriptor();
		CHECK_NOTNULL(descriptor);
		
		descriptor->addEntryByPos((uint64_t*)(shareAddr), targetValue,
			newval, FASASDescriptor::SHARE_VAR_POS);
		descriptor->setPrivateAddress((uint64_t*)(privateAddr));
	
		if(descriptor->process()) {
            RAW_CHECK(targetValue == privateAddr->getValueOfPrivateVar(),
                "old share value not equal to private value");
			break;
		}
	}
}

void FetchStoreStore::processByMwcas(FASASCasPtr* targetAddr, FASASCasPtr* storeAddr, 
	   uint64_t newval, FASASDescriptorPool* fasasDescPool)
{
    while(1) {
		uint64_t targetValue = targetAddr->getValueProtectedForMwcas(
            FASASDescriptor::SHARE_VAR_POS);
        RAW_CHECK(Descriptor::IsCleanPtr(targetValue), "targetValue not valid");
		uint64_t storeValue = storeAddr->getValueProtectedForMwcas(
            FASASDescriptor::STORE_VAR_POS);
        RAW_CHECK(Descriptor::IsCleanPtr(storeValue), "storeValue not valid");
		
		FASASDescriptor* descriptor = fasasDescPool->AllocateDescriptor();
		CHECK_NOTNULL(descriptor);
		
		descriptor->addEntryByPos((uint64_t*)(targetAddr), targetValue,
			newval, FASASDescriptor::SHARE_VAR_POS);
		descriptor->addEntryByPos((uint64_t*)(storeAddr), storeValue,
			targetValue, FASASDescriptor::STORE_VAR_POS);
	
		if(descriptor->processByMwcas(0, FASASDescriptor::INVALID_VAR_POS)) {
            RAW_CHECK(targetValue == storeAddr->getValueProtectedForMwcas(
                FASASDescriptor::STORE_VAR_POS), 
                "old share value not equal to private value");
			break;
		}
	}

}   

bool FetchStoreStore::dcasByMwcas(FASASCasPtr* targetAddr1, FASASCasPtr* targetAddr2, 
   uint64_t oldVal1, uint64_t oldVal2, 
   uint64_t newVal1, uint64_t newVal2,
   FASASDescriptorPool* fasasDescPool)
{
    FASASDescriptor* descriptor = fasasDescPool->AllocateDescriptor();
	CHECK_NOTNULL(descriptor);
		
	descriptor->addEntryByPos((uint64_t*)(targetAddr1), oldVal1,
			newVal1, FASASDescriptor::SHARE_VAR_POS);
	descriptor->addEntryByPos((uint64_t*)(targetAddr2), oldVal2,
			newVal2, FASASDescriptor::STORE_VAR_POS);

    return descriptor->processByMwcas(0, FASASDescriptor::INVALID_VAR_POS);
}   














}


