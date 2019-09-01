#pragma once

#include "mwcas/mwcas.h"

//#define FETCH_WAIT

namespace pmwcas {

class alignas(kCacheLineSize) FASASDescriptor : public BaseDescriptor {

public:
    
    static const uint64_t RecoverCASOp = (uint64_t)1 << 63;
    static const uint64_t DoubleCASOp = (uint64_t)1 << 62;
    static const uint64_t FASASOp = (uint64_t)1 << 61;

    static const uint64_t SuccStatus = 1;
    static const uint64_t FailedStatus = 0;
    /*static const uint8_t FASASOpStatusDirtyFlg  = 1ULL << 7;*/

    static const uint64_t IsPrivateValueSetFlg = (uint64_t)1 << 61;
    static const uint64_t StatusFlg = (uint64_t)1 << 62;
    static const uint64_t StatusDirtyFlg = (uint64_t)1 << 63;


    FASASDescriptor(DescriptorPartition* partition);

    virtual void Initialize();
    virtual void DeallocateMemory();
    
    void addSharedWord(uint64_t* addr, uint64_t oldval, uint64_t newval); 

    //bool processByMwcas(uint32_t calldepth, uint32_t processPos);
    bool process();

    
    //recover cas ask for the initial value of private address is zero
    void setPrivateAddress(uint64_t* privateAddress) {
        privateAddress_ = privateAddress;
    }

    bool Cleanup(bool isSuc);

    void setOpType(uint64_t opType) {
        opType_ = opType;
    }

    uint64_t getOpType() {
        return opType_;
    }

    void helpProcess();

    inline static bool isRecoverCAS(uint64_t value) {
        return value & RecoverCASOp;
    }

    inline static bool isDoubleCAS(uint64_t value) {
        return value & DoubleCASOp;
    }

    inline static bool isFASAS(uint64_t value) {
        return value & FASASOp;
    }

    inline static uint64_t getDCASValue(uint64_t value) {
        return value & ~(DoubleCASOp);
    }

    inline static uint64_t setOpTypeFlg(uint64_t value, uint64_t flags) {
        RAW_CHECK((flags & ~(DoubleCASOp | FASASOp | RecoverCASOp)) == 0,
            "invalid flags");
        return value | flags;
    }

    inline uint64_t* getPrivateAddress() {
        uint64_t ptr =  (uint64_t)privateAddress_ & 
            ~(StatusDirtyFlg | StatusFlg | IsPrivateValueSetFlg);
        return (uint64_t*)(ptr);
    }

    inline uint64_t isPrivateValueSet() {
        return (uint64_t)privateAddress_ & IsPrivateValueSetFlg;
    }

    inline uint64_t getStatus() {
        return (uint64_t)privateAddress_ & StatusFlg;
    }

    inline uint64_t isStatusDirty() {
        return (uint64_t)privateAddress_ & StatusDirtyFlg;
    }

    static uint64_t persistTargetAddrValue(uint64_t* address);

    

private:
    friend class FASASDescriptorPool;
    
    uint64_t addDescriptorToShareVar();
    
    void changeShareValue();
    void changeShareValueWithoutFlush();
    
    void changePrivateValueSucc();

    void persistSuccStatus();

    BaseDescriptor::BaseWordDescriptor word_;
    uint64_t opType_;
    
    volatile uint64_t* privateAddress_;
    //uint8_t status_;
    //bool isPrivateAddrSet_;

};

class FASASDescriptorPool : public BaseDescriptorPool {
public:
    FASASDescriptorPool(
        uint32_t pool_size,
        uint32_t partition_count,
        FASASDescriptor * desc_va,
        bool enable_stats = false);

      // Get a free descriptor from the pool.
      FASASDescriptor* AllocateDescriptor();
  

private:
    /// Points to all descriptors
    FASASDescriptor * descriptors_;
  
    void recover(FASASDescriptor* fasasDesc);
    void recoverForFASAS(uint64_t status,
        FASASDescriptor & descriptor, uint64_t& redo_words,
        uint64_t& undo_words);
    void doRecoverPrivateAddrSucc(FASASDescriptor & descriptor);
    void doRecoverShareAddrSucc(FASASDescriptor & descriptor);
    void doRecoverShareAddrFail(FASASDescriptor & descriptor);
};

template <typename T>
class FASASTargetField
{
public:
    FASASTargetField(void* desc = nullptr) {
        value_ = T(desc);
    }

    //when no dirty flag, no need to flush. 
    //Because if it is not descriptor, the descriptor is already flushed.
    uint64_t getValueProtectedOfSharedVar() {
        //MwCASMetrics::AddRead();
retry:
        uint64_t val = (uint64_t)value_;

        if(val & BaseDescriptor::kMwCASFlag) {
#ifndef FETCH_WAIT
            // While the address contains a descriptor, help along completing the CAS
            FASASDescriptor* desc = (FASASDescriptor*)BaseDescriptor::CleanPtr(val);
            RAW_CHECK(desc, "invalid descriptor pointer");
            desc->helpProcess();
#endif
            goto retry;
        }

        return getValueWithDirtyFlg(val);
    }
    
    uint64_t getValueOfPrivateVar() {
        return value_;
    }

    uint64_t getValueWithDirtyFlg(uint64_t val) {
        if(val & BaseDescriptor::kDirtyFlag) {
            NVRAM::Flush(sizeof(uint64_t), (const void*)&value_);
            CompareExchange64((uint64_t*)&(value_), val & ~BaseDescriptor::kDirtyFlag, val);
            val &= ~BaseDescriptor::kDirtyFlag;
        }

        RAW_CHECK(BaseDescriptor::IsCleanPtr(val), "dirty flag set on return value");
        return val;
    }

    /// Return an integer representation of the target word
    operator uint64_t() {
        return uint64_t(value_);
    }

    /// Copy operator
    FASASTargetField<T>& operator= (FASASTargetField<T>& rhval) {
        value_ = rhval.value_;
        return *this;
    }

    /// Address-of operator
    T* operator& () {
        return const_cast<T*>(&value_);
    }

    /// Assignment operator
    FASASTargetField<T>& operator= (T rhval) {
        value_ = rhval;
        return *this;
    }

    /// Content-of operator
    T& operator* () {
        return *value_;
    }

    /// Dereference operator
    T* operator-> () {
        return value_;
    }

    volatile T value_;
};



























}

