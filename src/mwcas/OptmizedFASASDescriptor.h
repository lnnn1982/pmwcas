#pragma once

#include "mwcas/mwcas.h"
#include "mwcas/FASASDescriptor.h"

//#define FETCH_WAIT

namespace pmwcas {
class OptmizedFASASDescriptorPool;

class alignas(kCacheLineSize) OptmizedFASASDescriptor  {

public:

    static const uint64_t ProcessIdFlg = (uint64_t)0XFFF << 48;
    static const uint64_t SeqIdFlg = (uint64_t)0XF << 60;
    static const uint64_t ActualValueFlg = (uint64_t)0XFFFFFFFFFFFF;

    OptmizedFASASDescriptor(uint16_t processId);
    bool process(uint64_t oldval, uint64_t newval,
        OptmizedFASASDescriptorPool & pool, uint32_t locationId);
    
    void setOpType(uint64_t opType) {
        opType_ = opType;
    }
    
    void setSharedAddress(uint64_t* sharedAddress);
    
    //recover cas ask for the initial value of private address is zero
    void setPrivateAddress(uint64_t* privateAddress) {
        privateAddress_ = privateAddress;
    }

    //void setShareValue(uint64_t oldval, uint64_t newval); 

    uint64_t getOpType() {
        return opType_;
    }

    uint64_t* getPrivateAddress() {
        return privateAddress_;
    }

    bool isPrivateValueSet() {
        return isPrivateAddrSet_;
    }

    uint64_t getNewShareVarValue() {
        return newShareVarValue_;
    }

    uint64_t getOldShareVarValue() {
        return oldShareVarValue_;
    }

    uint16_t getProcessId() {
        return processId_;
    }

    uint64_t getSeqId() {
        return (statusInfo_ & SeqIdFlg) >> 60;
    }

    void helpProcess(uint64_t val);
    void helpWithShareValue(uint64_t val);
    
    static uint64_t getValueProtectedOfSharedVar(uint64_t * addr,
        OptmizedFASASDescriptorPool & pool, uint32_t locationId, uint16_t processId);
    static uint64_t getValueOfPrivateVar(uint64_t * addr);

private:

    bool addDescriptorToShareVar(uint64_t orgVal);
    void changePrivateValueSucc();
    inline static bool isValidValue(uint64_t value) {
        return (value & ( (uint64_t)0XFFFF << 48)) == 0;
    } 

    
    friend class OptmizedFASASDescriptorPool;
    
    uint64_t* sharedAddress_;
    uint64_t oldShareVarValue_;
    uint64_t newShareVarValue_;

    uint64_t opType_;
    uint64_t* privateAddress_;

    uint16_t processId_;
    volatile bool isPrivateAddrSet_;
    volatile bool isShareVarSet_;

    volatile uint64_t statusInfo_;
};

class OptmizedFASASDescriptorPool {
public:
    typedef uint32_t (*LocationIndexMapFunc)(uint64_t * addr);

    OptmizedFASASDescriptorPool(
        uint32_t poolSize,
        uint32_t maximumThreadNum,
        OptmizedFASASDescriptor * descPtr,
        LocationIndexMapFunc func = nullptr,
        bool enable_stats = false);

    OptmizedFASASDescriptor * getDescriptor(uint16_t processId, uint32_t locationId);
    OptmizedFASASDescriptor * getDescriptor(uint16_t processId, uint64_t * addr);

    
private:
    uint32_t poolSize_;
    uint32_t maximumThreadNum_;

    /// Points to all descriptors
    OptmizedFASASDescriptor * descriptors_;

    LocationIndexMapFunc locIdMapfunc_;
  
    void createNewDescriptor();
    void recoverDescriptor();
    void recoverOneDescriptor(OptmizedFASASDescriptor & descriptor, 
        uint64_t& redo_words) ;
    void doRecoverPrivateAddrSucc(OptmizedFASASDescriptor & descriptor);

    

};

























}

