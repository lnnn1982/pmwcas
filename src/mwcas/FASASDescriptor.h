#pragma once

#include "mwcas/mwcas.h"

namespace pmwcas {

class alignas(kCacheLineSize) FASASDescriptor : public Descriptor {

public:
    static const uint32_t SHARE_VAR_POS   = 0U;
    static const uint32_t STORE_VAR_POS  = 1U;
    static const uint32_t INVALID_VAR_POS  = 2U;

    FASASDescriptor(DescriptorPartition* partition);
    void addEntryByPos(uint64_t* addr, uint64_t oldval, uint64_t newval,
        int insertpos, uint32_t recycle_policy = kRecycleNever); 

    bool processByMwcas(uint32_t calldepth, uint32_t processPos);
    bool process();

    void setPrivateAddress(uint64_t* privateAddress) {
        privateAddress_ = privateAddress;
    }

    uint64_t* getPrivateAddress() {
        return privateAddress_;
    }

    void helpProcess();

private:

    
    uint64_t addDescriptorToShareVar();
    void persistTargetFieldsStatus(uint64_t descptr, uint32_t my_status);
    void changeShareValue();
    void persistTargetAddrValue(uint64_t* address);
    void changePrivateValue();
    void changeTargetAddressValue(uint64_t descptr, uint32_t calldepth, 
        uint32_t processPos);





    uint64_t* privateAddress_;

};

class FASASDescriptorPool : public DescriptorPool {
public:
  FASASDescriptorPool(
    uint32_t pool_size,
    uint32_t partition_count,
    FASASDescriptor * desc_va,
    bool enable_stats = false);

  // Get a free descriptor from the pool.
  FASASDescriptor* AllocateDescriptor(Descriptor::AllocateCallback ac,
    Descriptor::FreeCallback fc);
  
  // Allocate a free descriptor from the pool using default allocate and
  // free callbacks.
  inline FASASDescriptor* AllocateDescriptor() {
    return AllocateDescriptor(nullptr, nullptr);
  }

private:

  void assigneValue(uint32_t pool_size, uint32_t partition_count, 
    FASASDescriptor* desc_va, bool enable_stats);
};

template <typename T>
class FASASTargetField : public MwcTargetField<T>
{
public:
    FASASTargetField(void* desc = nullptr) : MwcTargetField<T>(desc) {
    }

    uint64_t getValueProtectedForMwcas(uint32_t processPos) {
        MwCASMetrics::AddRead();
retry:
        uint64_t val = (uint64_t)MwcTargetField<T>::value_;

        if(val & MwcTargetField<T>::kCondCASFlag) {
            RAW_CHECK((val & MwcTargetField<T>::kDirtyFlag) == 0, "dirty flag set on CondCAS descriptor");

            Descriptor::WordDescriptor* wd =
                (Descriptor::WordDescriptor*)Descriptor::CleanPtr(val);
            uint64_t dptr =
                Descriptor::SetFlags(wd->GetDescriptor(), MwcTargetField<T>::kMwCASFlag | MwcTargetField<T>::kDirtyFlag);
            CompareExchange64(
                wd->address_,
                *wd->status_address_ == Descriptor::kStatusUndecided ?
                dptr : wd->old_value_,
                val);
              goto retry;
        }

        if(val & MwcTargetField<T>::kMwCASFlag) {
            // While the address contains a descriptor, help along completing the CAS
            FASASDescriptor* desc = (FASASDescriptor*)Descriptor::CleanPtr(val);
            RAW_CHECK(desc, "invalid descriptor pointer");
            desc->processByMwcas(1, processPos);
            goto retry;
        }

        return getValueWithDirtyFlg(val);
    }

    uint64_t getValueProtectedOfSharedVar() {
        MwCASMetrics::AddRead();
retry:
        uint64_t val = (uint64_t)MwcTargetField<T>::value_;

        if(val & MwcTargetField<T>::kMwCASFlag) {
            // While the address contains a descriptor, help along completing the CAS
            FASASDescriptor* desc = (FASASDescriptor*)Descriptor::CleanPtr(val);
            RAW_CHECK(desc, "invalid descriptor pointer");
            desc->helpProcess();
            goto retry;
        }

        return getValueWithDirtyFlg(val);
    }
    
    uint64_t getValueOfPrivateVar() {
        uint64_t val = (uint64_t)MwcTargetField<T>::value_;
        return getValueWithDirtyFlg(val);
    }

    uint64_t getValueWithDirtyFlg(uint64_t val) {
        if(val & MwcTargetField<T>::kDirtyFlag) {
            MwcTargetField<T>::PersistValue();
            CompareExchange64((uint64_t*)&(MwcTargetField<T>::value_), val & ~MwcTargetField<T>::kDirtyFlag, val);
            val &= ~MwcTargetField<T>::kDirtyFlag;
        }

        RAW_CHECK(MwcTargetField<T>::IsCleanPtr(val), "dirty flag set on return value");
        return val;
    }

      /// Assignment operator
    FASASTargetField<T>& operator= (T rhval) {
        MwcTargetField<T>::value_ = rhval;
        return *this;
    }
};



























}

