#include "glog/logging.h"
#include "glog/raw_logging.h"
#include "mwcas/FASASDescriptor.h"
#include "util/atomics.h"

namespace pmwcas {

FASASDescriptor::FASASDescriptor(DescriptorPartition* partition)
    : Descriptor(partition), privateAddress_(nullptr) {
}

void FASASDescriptor::addEntryByPos(uint64_t* addr, uint64_t oldval, uint64_t newval,
	 int insertpos, uint32_t recycle_policy) 
{
	// IsProtected() checks are quite expensive, use DCHECK instead of RAW_CHECK.
    DCHECK(owner_partition_->garbage_list->GetEpoch()->IsProtected());
    DCHECK(IsCleanPtr(oldval));
    DCHECK(IsCleanPtr(newval) || newval == kNewValueReserved);

    words_[insertpos].address_ = addr;
    words_[insertpos].old_value_ = oldval;
    words_[insertpos].new_value_ = newval;
    words_[insertpos].status_address_ = &status_;
    words_[insertpos].recycle_policy_ = recycle_policy;

    count_++;
}

bool FASASDescriptor::processByMwcas(uint32_t calldepth, uint32_t processPos) 
{
    DCHECK(owner_partition_->garbage_list->GetEpoch()->IsProtected());

    // Not visible to anyone else, persist before making the descriptor visible
    if(calldepth == 0) {
        status_ = kStatusUndecided;
	    NVRAM::Flush(sizeof(Descriptor), this);
    }

    uint64_t descptr = SetFlags(this, kMwCASFlag | kDirtyFlag);
    uint32_t my_status = kStatusSucceeded;

    uint32_t i = 0;
    if(processPos == SHARE_VAR_POS) {
        i = STORE_VAR_POS;
    }
    else if(processPos == SHARE_VAR_POS) {
        i = INVALID_VAR_POS;
    }
    
    for(; i < INVALID_VAR_POS && my_status == kStatusSucceeded; ++i) {
	    WordDescriptor* wd = &words_[i];
retry_entry:
	    auto rval = CondCAS(i, kDirtyFlag);

        // Ok if a) we succeeded to swap in a pointer to this descriptor or b) some
        // other thread has already done so. Need to persist all fields (which point
        // to descriptors) before switching to final status, so that recovery will
        // know reliably whether to roll forward or back for this descriptor.
	    if(rval == wd->old_value_ || CleanPtr(rval) == (uint64_t)this) {
	        continue;
	    }

	    // Do we need to help another MWCAS operation?
	    if(IsMwCASDescriptorPtr(rval)) {
	        // Clashed with another MWCAS; help complete the other MWCAS if it is
	        // still in flight.
	        FASASDescriptor* otherMWCAS = (FASASDescriptor*)CleanPtr(rval);
	        otherMWCAS->processByMwcas(calldepth + 1, i);
	        MwCASMetrics::AddHelpAttempt();
	        goto retry_entry;
	    } else {
	        // rval must be another value, we failed
	        my_status = kStatusFailed;
	    }
    }

    persistTargetFieldsStatus(descptr, my_status);
    changeTargetAddressValue(descptr, calldepth, processPos);

    if(calldepth == 0) {
        privateAddress_ = nullptr;
	    return Cleanup();
    } else {
	    return status_ == kStatusSucceeded;
    }
}

bool FASASDescriptor::process()
{
    DCHECK(owner_partition_->garbage_list->GetEpoch()->IsProtected());

    // Not visible to anyone else, persist before making the descriptor visible
    status_ = kStatusFailed;
	NVRAM::Flush(sizeof(Descriptor), this);

    WordDescriptor* shareWd = &words_[SHARE_VAR_POS];
    if(addDescriptorToShareVar() != shareWd->old_value_) {
        return Cleanup();
    }

    uint64_t descptr = SetFlags(this, kMwCASFlag | kDirtyFlag);
    persistTargetFieldsStatus(descptr, kStatusSucceeded);

    changePrivateValue();
    changeShareValue();

    return Cleanup();
}

void FASASDescriptor::helpProcess() {
    uint64_t descptr = SetFlags(this, kMwCASFlag | kDirtyFlag);
    persistTargetFieldsStatus(descptr, kStatusSucceeded);
    changeShareValue();
}

uint64_t FASASDescriptor::addDescriptorToShareVar()
{
    WordDescriptor* wd = &words_[SHARE_VAR_POS];
    uint64_t descptr = SetFlags(this, kMwCASFlag | kDirtyFlag);

retry:
    uint64_t ret = CompareExchange64(wd->address_, descptr, wd->old_value_);
    
    if(IsMwCASDescriptorPtr(ret)) {
        FASASDescriptor* otherMWCAS = (FASASDescriptor*)CleanPtr(ret);
        otherMWCAS->helpProcess();
        MwCASMetrics::AddHelpAttempt();
        goto retry;
    }
    
    return  ret;
}

void FASASDescriptor::persistTargetFieldsStatus(uint64_t descptr, uint32_t my_status)
{
    // Persist all target fields if we successfully installed mwcas descriptor on
    // all fields.
    if(my_status == kStatusSucceeded) {
	    for (uint32_t i = 0; i < count_; ++i) {
	        WordDescriptor* wd = &words_[i];
	        uint64_t val = *wd->address_;
            //avoid multiple persist
	        if(val == descptr) {
		        wd->PersistAddress();
		        CompareExchange64(wd->address_, descptr & ~kDirtyFlag, descptr);
	        }
	    }
    }

    // Switch to the final state, the MwCAS concludes after this point
    CompareExchange32(&status_, my_status | kStatusDirtyFlag, kStatusUndecided);

    // Now the MwCAS is concluded - status is either succeeded or failed, and
    // no observers will try to help finish it, so do a blind flush and reset
    // the dirty bit.
    //RAW_CHECK((status_ & ~kStatusDirtyFlag) != kStatusUndecided, "invalid status");
    auto status = status_;
    if(status & kStatusDirtyFlag) {
	    PersistStatus();
	    CompareExchange32(&status_, status & ~kStatusDirtyFlag, status);
    }
    // No need to flush again, recovery does not care about the dirty bit
}


void FASASDescriptor::changeShareValue() {
    WordDescriptor* wd = &words_[SHARE_VAR_POS];
	uint64_t val = wd->new_value_|kDirtyFlag;

    uint64_t descptr = SetFlags(this, kMwCASFlag);
    uint64_t addrVal = *(wd->address_);
    if(addrVal == descptr) {
        CompareExchange64(wd->address_, val, descptr);
    }

    persistTargetAddrValue(wd->address_);
}

void FASASDescriptor::persistTargetAddrValue(uint64_t* address) {
    uint64_t val = *address;
    if(val & kDirtyFlag) {
        NVRAM::Flush(sizeof(uint64_t), (void*)address);
        CompareExchange64(address, val & ~kDirtyFlag, val);
    }
}

void FASASDescriptor::changePrivateValue() {
    WordDescriptor* wd = &words_[SHARE_VAR_POS];
    *privateAddress_ = wd->old_value_|kDirtyFlag;
    persistTargetAddrValue(privateAddress_);
}

void FASASDescriptor::changeTargetAddressValue(uint64_t descptr, uint32_t calldepth, 
    uint32_t processPos)
{
    bool succeeded = (status_ == kStatusSucceeded);
    uint64_t cmpDescptr = succeeded ? descptr & ~kDirtyFlag : descptr;
    for(uint32_t i = 0; i < count_; i++) {
        if(calldepth != 0 && i != processPos) {
            //help thread only change the address of processing;
            continue;
        }
    
	    WordDescriptor* wd = &words_[i];
	    uint64_t val = succeeded ? wd->new_value_ : wd->old_value_;
	    val |= kDirtyFlag;

        uint64_t addrVal = *(wd->address_);
        if(addrVal == cmpDescptr) {
            CompareExchange64(wd->address_, val, cmpDescptr);
        }

        persistTargetAddrValue(wd->address_);
    }
}








}

