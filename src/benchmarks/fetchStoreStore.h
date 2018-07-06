#include "mwcas_benchmark.h"


namespace pmwcas {

class FetchStoreStore {
public:
	FetchStoreStore(DescriptorPool* descriptor_pool)
		: descriptor_pool_(descriptor_pool) {}
		
	void process(CasPtr* targetAddr, CasPtr* storeAddr, 
	   uint64_t newval);

private:

    DescriptorPool* descriptor_pool_;

};









}


