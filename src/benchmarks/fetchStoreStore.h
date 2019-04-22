#pragma once

#include "mwcas_common.h"


namespace pmwcas {

class FetchStoreStore {
public:
		
	uint64_t processByOrgMwcas(CasPtr* targetAddr, CasPtr* storeAddr, 
	   uint64_t newval, DescriptorPool* descPool);
    bool dcasByOrgMwcas(CasPtr* targetAddr1, CasPtr* targetAddr2,
        uint64_t oldVal1, uint64_t oldVal2, 
        uint64_t newVal1, uint64_t newVal2, DescriptorPool* descPool);
       
    uint64_t process(FASASCasPtr* shareAddr, FASASCasPtr* privateAddr, 
	   uint64_t newval, FASASDescriptorPool* fasasDescPool);
    
    uint64_t processByMwcas(FASASCasPtr* targetAddr, FASASCasPtr* storeAddr, 
	   uint64_t newval, FASASDescriptorPool* fasasDescPool);

    bool dcas(FASASCasPtr* targetAddr1, FASASCasPtr* targetAddr2, 
	   uint64_t oldVal1, uint64_t oldVal2, 
	   uint64_t newVal1, uint64_t newVal2, FASASDescriptorPool* fasasDescPool);    


private:
    void epochProtect(DescriptorPool* descPool);
        
    static thread_local uint64_t epochs_;
    const uint64_t kEpochThreshold_ = 100;
};









}


