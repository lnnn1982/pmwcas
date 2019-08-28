#include "glog/logging.h"
#include "glog/raw_logging.h"
#include "mwcas/FASASDescriptor.h"
#include "util/atomics.h"

namespace pmwcas {

FASASDescriptor::FASASDescriptor(DescriptorPartition* partition)
    : BaseDescriptor(partition), privateAddress_(nullptr) {
    privateAddress_ = nullptr;
    opType_ = FASASOp;
    //status_ = FailedStatus;
    //isPrivateAddrSet_ = false;
    memset(&word_, 0, sizeof(BaseDescriptor::BaseWordDescriptor));
}

void FASASDescriptor::Initialize() {
    //privateAddress_ needs to be first to clear becuase it is used in recover
    privateAddress_ = nullptr;
    next_ptr_ = nullptr;
    opType_ = FASASOp;
    //status_ = FailedStatus;
    //isPrivateAddrSet_ = false;
    memset(&word_, 0, sizeof(BaseDescriptor::BaseWordDescriptor));
}

void FASASDescriptor::DeallocateMemory() {}

void FASASDescriptor::addSharedWord(uint64_t* addr, uint64_t oldval, uint64_t newval) 
{
	// IsProtected() checks are quite expensive, use DCHECK instead of RAW_CHECK.
    DCHECK(owner_partition_->garbage_list->GetEpoch()->IsProtected());
    DCHECK(BaseDescriptor::IsCleanPtr(oldval));
    DCHECK(BaseDescriptor::IsCleanPtr(newval));

    word_.address_ = addr;
    word_.old_value_ = oldval;
    word_.new_value_ = newval;
}

/*bool FASASDescriptor::processByMwcas(uint32_t calldepth, uint32_t processPos) 
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

    persistTargetFieldsStatus(descptr, my_status, kStatusUndecided);
    changeTargetAddressValue(descptr, calldepth, processPos);

    if(calldepth == 0) {
        privateAddress_ = nullptr;
	    return Cleanup();
    } else {
	    return status_ == kStatusSucceeded;
    }
}*/

//support both fetch store store and dcas
bool FASASDescriptor::process()
{
    DCHECK(owner_partition_->garbage_list->GetEpoch()->IsProtected());

    // Not visible to anyone else, persist before making the descriptor visible
    //status_ = FailedStatus;
    //isPrivateAddrSet_ = false;
	NVRAM::Flush(sizeof(FASASDescriptor), this);

    if(addDescriptorToShareVar() != word_.old_value_) {
        return Cleanup(false);
    }

    persistTargetAddrValue(word_.address_);
    persistSuccStatus();

    //the order can't change. When set private value, make sure status is already set.
    changeShareValue();
    changePrivateValueSucc();

    return Cleanup(true);
}

void FASASDescriptor::helpProcess() {
    persistTargetAddrValue(word_.address_);
    persistSuccStatus();

    changeShareValue();
}

uint64_t FASASDescriptor::addDescriptorToShareVar()
{
    uint64_t descptr = BaseDescriptor::SetFlags(this, BaseDescriptor::kMwCASFlag | BaseDescriptor::kDirtyFlag);

retry:
    uint64_t ret = CompareExchange64(word_.address_, descptr, word_.old_value_);
    if(BaseDescriptor::IsMwCASDescriptorPtr(ret)) {
#ifndef FETCH_WAIT
        FASASDescriptor* otherMWCAS = (FASASDescriptor*)BaseDescriptor::CleanPtr(ret);
        otherMWCAS->helpProcess();
        MwCASMetrics::AddHelpAttempt();
#endif
        goto retry;
    }

    return  ret;
}

void FASASDescriptor::changeShareValue() {
	uint64_t val = word_.new_value_|kDirtyFlag;

    uint64_t descptr = SetFlags(this, kMwCASFlag);
    uint64_t addrVal = *(word_.address_);
    if(addrVal == descptr) {
        CompareExchange64(word_.address_, val, descptr);
    }

    persistTargetAddrValue(word_.address_);
}

void FASASDescriptor::persistTargetAddrValue(uint64_t* address) {
    uint64_t val = *address;
    if(val & kDirtyFlag) {
        NVRAM::Flush(sizeof(uint64_t), (void*)address);
        CompareExchange64(address, val & ~kDirtyFlag, val);
    }
}

void FASASDescriptor::persistSuccStatus() {
    uint64_t cleanPrivateAddr = (uint64_t)getPrivateAddress();
    uint64_t dirtyStatusAddr = cleanPrivateAddr|StatusFlg|StatusDirtyFlg;
    // Switch to the final state, the MwCAS concludes after this point
    if((uint64_t)privateAddress_ == cleanPrivateAddr) {
        CompareExchange64((uint64_t *)&privateAddress_, 
            dirtyStatusAddr, cleanPrivateAddr);
    }

    if(isStatusDirty() == 1) {
	    NVRAM::Flush(sizeof(privateAddress_), &privateAddress_);
	    CompareExchange64((uint64_t *)&privateAddress_, 
	       cleanPrivateAddr|StatusFlg, dirtyStatusAddr);
    }
}

void FASASDescriptor::changePrivateValueSucc() {
    uint64_t * privateAddrss = getPrivateAddress();
    if(isRecoverCAS(opType_)) {
        *privateAddrss = 1;
    }
    else if(isDoubleCAS(opType_)) {
        *privateAddrss = getDCASValue(opType_);
    }
    else {
        *privateAddrss = word_.old_value_;
    }

    NVRAM::Flush(sizeof(uint64_t), (void*)privateAddrss);

    uint64_t tmpPrivateAddr = (uint64_t)(privateAddress_);
    CompareExchange64((uint64_t *)&privateAddress_, 
	       tmpPrivateAddr|IsPrivateValueSetFlg, tmpPrivateAddr);

    NVRAM::Flush(sizeof(privateAddress_), &privateAddress_);
}

bool FASASDescriptor::Cleanup(bool isSuc) {

  if(isSuc) {
    MwCASMetrics::AddSucceededUpdate();
  } else {
    MwCASMetrics::AddFailedUpdate();
  }
  doCleanup();
  
  return isSuc;
}

/*void FASASDescriptor::changeTargetAddressValue(uint64_t descptr, uint32_t calldepth, 
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
}*/

FASASDescriptorPool::FASASDescriptorPool(uint32_t pool_size, uint32_t partition_count, 
    FASASDescriptor* desc_va, bool enable_stats)
    : BaseDescriptorPool(pool_size, partition_count, enable_stats),
      descriptors_(desc_va)
{ 
    //std::cout << "FASASDescriptorPool pool_size:" << std::dec << pool_size << ", desc_va:" 
        //<< std::hex << desc_va << std::endl;

    RAW_CHECK(descriptors_, "null descriptor pool");
    
    Metadata *metadata = (Metadata*)((uint64_t)descriptors_ - sizeof(Metadata));
    //std::cout << "FASASDescriptorPool metadata:" << std::hex << metadata << std::endl;
    RAW_CHECK((uint64_t)metadata->initial_address == (uint64_t)metadata,
              "invalid initial address");
    RAW_CHECK(metadata->descriptor_count == pool_size_,
              "wrong descriptor pool size");

    //std::cout << "FASASDescriptorPool after check address and pool size" << std::endl;

    if(descriptors_[0].opType_ != 0) {
        recover(descriptors_);
    }
    else {
        std::cout << "no need to recover" << std::endl;
    }
    
    memset(descriptors_, 0, sizeof(FASASDescriptor) * pool_size_);

    // Distribute this many descriptors per partition
    RAW_CHECK(pool_size_ > partition_count_,
      "provided pool size is less than partition count");
    uint32_t desc_per_partition = pool_size_ / partition_count_;

    uint32_t partition = 0;
    for(uint32_t i = 0; i < pool_size_; ++i) {
        auto* desc = descriptors_ + i;
        DescriptorPartition* p = partition_table_ + partition;
        new(desc) FASASDescriptor(p);
        desc->next_ptr_ = p->free_list;
        p->free_list = desc;

        if((i + 1) % desc_per_partition == 0) {
          partition++;
        }
    }
}

void FASASDescriptorPool::recover(FASASDescriptor* fasasDesc) {
    uint64_t in_progress_desc = 0, redo_words = 0, undo_words = 0;
    for(uint32_t i = 0; i < pool_size_; ++i) {
        FASASDescriptor & desc = fasasDesc[i];
        if(desc.opType_ == 0) {
          // Must be a new pool - comes with everything zeroed but better
          // find this as we look at the first descriptor.
            RAW_CHECK(i == 0, "corrupted descriptor pool/data area");
            break;
        }

        if(desc.privateAddress_ == nullptr) {
            continue;
        }

        in_progress_desc++;
        uint64_t status = desc.getStatus();
        recoverForFASAS(status, desc, redo_words, undo_words);
    }

    std::cout << "recover Found " << in_progress_desc <<
        " in-progress descriptors, rolled forward " << redo_words <<
        " words, rolled back " << undo_words << " words" << std::endl;
}

void FASASDescriptorPool::recoverForFASAS(uint64_t status,
    FASASDescriptor & descriptor, uint64_t& redo_words,
    uint64_t& undo_words) 
{
    if((*descriptor.word_.address_) & BaseDescriptor::kDirtyFlag) {
        (*descriptor.word_.address_) &= ~BaseDescriptor::kDirtyFlag;
        descriptor.word_.PersistAddress();
    }

    uint64_t val = Descriptor::CleanPtr(*descriptor.word_.address_);
    if(status == FASASDescriptor::SuccStatus) {
        bool isRedoFlg = false;
        if(descriptor.isPrivateValueSet() == 0) {
            isRedoFlg = true;
            doRecoverPrivateAddrSucc(descriptor);
        }

        if(val == (uint64_t)&descriptor) {
            isRedoFlg = true;
            doRecoverShareAddrSucc(descriptor);
        }

        if(isRedoFlg) redo_words++;
    }
    else if(status == FASASDescriptor::FailedStatus && val == (uint64_t)&descriptor){
        doRecoverShareAddrFail(descriptor);
        undo_words++;
    }
}

void FASASDescriptorPool::doRecoverPrivateAddrSucc(FASASDescriptor & descriptor) 
{   
    uint64_t * privateAddress = descriptor.getPrivateAddress();
    if(FASASDescriptor::isRecoverCAS(descriptor.getOpType())) {
        *(privateAddress) = 1;
    }
    else if(FASASDescriptor::isDoubleCAS(descriptor.getOpType())) {
        *(privateAddress) = 
            FASASDescriptor::getDCASValue(descriptor.getOpType());
    }
    else if(FASASDescriptor::isFASAS(descriptor.getOpType())) {
        *(privateAddress) = descriptor.word_.old_value_;
    }

    NVRAM::Flush(sizeof(uint64_t), (void*)privateAddress);
    LOG(ERROR) << "Applied new value " << std::hex 
        << (*privateAddress) << " to private address:" 
        << std::hex << privateAddress;
}

void FASASDescriptorPool::doRecoverShareAddrSucc(FASASDescriptor & descriptor) {
    *(descriptor.word_.address_) = descriptor.word_.new_value_;
    NVRAM::Flush(sizeof(uint64_t), (void*)descriptor.word_.address_);
    LOG(ERROR) << "Applied new value " << std::hex << *(descriptor.word_.address_) 
        << " to share address:" << std::hex << descriptor.word_.address_;
}

void FASASDescriptorPool::doRecoverShareAddrFail(FASASDescriptor & descriptor) {
    *(descriptor.word_.address_) = descriptor.word_.old_value_;
    NVRAM::Flush(sizeof(uint64_t), (void*)descriptor.word_.address_);
    LOG(ERROR) << "Applied old value " << std::hex << *(descriptor.word_.address_) 
        << " to share address:" << std::hex << descriptor.word_.address_;
}

/*void FASASDescriptorPool::recoverForFASASByMwcas(uint32_t status,
    FASASDescriptor & descriptor, 
    uint64_t& redo_words, uint64_t& undo_words)
{
    if(status == Descriptor::kStatusSucceeded) {
        for(int w = 0; w < descriptor.count_; ++w) {
            auto& word = descriptor.words_[w];

            if((*word.address_) & Descriptor::kDirtyFlag) {
                (*word.address_) &= ~Descriptor::kDirtyFlag;
                word.PersistAddress();
            }

            uint64_t val = Descriptor::CleanPtr(*word.address_);
            if(val == (uint64_t)&descriptor) {
                *word.address_ = word.new_value_;
                word.PersistAddress();
                LOG(ERROR) << "Applied new value " << std::hex << word.new_value_ 
                    << " at " << std::hex << word.address_ << " w:" << w;

                redo_words++;
            }
        }
    }
    else if(status == Descriptor::kStatusUndecided || 
        status == Descriptor::kStatusFailed) {
        for(int w = 0; w < descriptor.count_; ++w) {
            auto& word = descriptor.words_[w];
            if((*word.address_) & Descriptor::kDirtyFlag) {
                (*word.address_) &= ~Descriptor::kDirtyFlag;
                word.PersistAddress();
            }
                        
            uint64_t val = Descriptor::CleanPtr(*word.address_);
            if(val == (uint64_t)&descriptor || val == (uint64_t)&word) {
                *word.address_ = word.old_value_;
                word.PersistAddress();
                LOG(ERROR) << "Applied old value " << std::hex << word.old_value_ 
                    << " at " << std::hex << word.address_ << " w:" << w;

                undo_words++;
            }
        }
    }
}*/

FASASDescriptor* FASASDescriptorPool::AllocateDescriptor()
{
    return (FASASDescriptor*)AllocateBaseDescriptor();
}

}

