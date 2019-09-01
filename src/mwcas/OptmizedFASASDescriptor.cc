#include "glog/logging.h"
#include "glog/raw_logging.h"
#include "mwcas/OptmizedFASASDescriptor.h"
#include "util/atomics.h"

namespace pmwcas {

//processId adding 1 to avoid zero value
OptmizedFASASDescriptor::OptmizedFASASDescriptor(uint16_t processId)
    : sharedAddress_(nullptr),oldShareVarValue_(0),newShareVarValue_(0),
      opType_(FASASDescriptor::FASASOp), privateAddress_(nullptr),
      processId_(processId), isPrivateAddrSet_(false),
      isShareVarSet_(false), 
      statusInfo_(0 | FASASDescriptor::FailedStatus)
{
}

void OptmizedFASASDescriptor::setSharedAddress(uint64_t* sharedAddress) {
    sharedAddress_ = sharedAddress;
}

bool OptmizedFASASDescriptor::process(uint64_t oldval, uint64_t newval,
    OptmizedFASASDescriptorPool & pool, uint32_t locationId)
{
    //this persist not necessary
    
    /*uint64_t val = FASASDescriptor::persistTargetAddrValue(sharedAddress_);
    uint64_t actualValue = val & ActualValueFlg;
    if(actualValue != oldShareVarValue_) return false;*/

    uint64_t val = *sharedAddress_;
    uint64_t actualValue = (*sharedAddress_) & ActualValueFlg;
    if(actualValue != oldval) return false;

    uint16_t otherProcessId = (uint16_t)((uint64_t)(val & OptmizedFASASDescriptor::ProcessIdFlg)
        >> 48);
    if(otherProcessId != 0 && otherProcessId != processId_) {
        OptmizedFASASDescriptor * odescr = pool.getDescriptor(otherProcessId-1, locationId);
        odescr->helpProcess(val);
    }

    oldShareVarValue_ = oldval;
    newShareVarValue_ = newval;

    uint64_t seqId = getSeqId();
    RAW_CHECK(seqId <= 0XF, "seqId out of bound");
    uint64_t newSeqId = (seqId+1) & 0XF;
    statusInfo_ = (newSeqId << 60) | FASASDescriptor::FailedStatus;
    
    isPrivateAddrSet_ = false;
    isShareVarSet_ = false;

	NVRAM::Flush(sizeof(OptmizedFASASDescriptor), this);

    if(!addDescriptorToShareVar(val)) {
        MwCASMetrics::AddFailedUpdate();
        return false;
    }

    NVRAM::Flush(sizeof(uint64_t), sharedAddress_);
    
    uint64_t tmpStatus = statusInfo_;
    //CompareExchange64((uint64_t *)&statusInfo_, tmpStatus | FASASDescriptor::SuccStatus, 
        //tmpStatus);
    statusInfo_ = tmpStatus | FASASDescriptor::SuccStatus;
    NVRAM::Flush(sizeof(statusInfo_), (void*)&statusInfo_);

    isShareVarSet_ = true;

    changePrivateValueSucc();

    MwCASMetrics::AddSucceededUpdate();
    return true;
}

bool OptmizedFASASDescriptor::addDescriptorToShareVar(uint64_t orgVal)
{
    uint64_t newVal = ((uint64_t)processId_ << 48) | newShareVarValue_ | (getSeqId() << 60);
    uint64_t ret = CompareExchange64(sharedAddress_, newVal, orgVal);
    return  ret == orgVal;
}

void OptmizedFASASDescriptor::helpProcess(uint64_t val) {
    if(isShareVarSet_) {
        return;
    }
    uint64_t seqId = (val & OptmizedFASASDescriptor::SeqIdFlg) >>60;
    if(seqId != getSeqId()) return;

    //the values may be different.
    //uint64_t actualValue = val & OptmizedFASASDescriptor::ActualValueFlg;
    //RAW_CHECK(actualValue == newShareVarValue_, "value not match");

    if(isShareVarSet_) {
        return;
    }
    NVRAM::Flush(sizeof(uint64_t), sharedAddress_);

    uint64_t orgStatusInfo = (seqId << 60) | FASASDescriptor::FailedStatus;
    uint64_t newStatusInfo = (seqId << 60) | FASASDescriptor::SuccStatus;
    if(statusInfo_ == orgStatusInfo) {
        CompareExchange64((uint64_t *)&statusInfo_, newStatusInfo, orgStatusInfo);
    }

    if(isShareVarSet_) {
        return;
    }
    NVRAM::Flush(sizeof(statusInfo_), (void*)&statusInfo_);

    //isShareVarSet_ = true;
}

void OptmizedFASASDescriptor::helpWithShareValue(uint64_t val) {
    if(isShareVarSet_) {
        return;
    }
    uint64_t seqId = (val & OptmizedFASASDescriptor::SeqIdFlg) >>60;
    if(seqId != getSeqId()) return;

    //uint64_t actualValue = val & OptmizedFASASDescriptor::ActualValueFlg;
    //RAW_CHECK(actualValue == newShareVarValue_, "value not match");

    if(isShareVarSet_) {
        return;
    }
    NVRAM::Flush(sizeof(uint64_t), sharedAddress_);
}

void OptmizedFASASDescriptor::changePrivateValueSucc() {
    if(FASASDescriptor::isRecoverCAS(opType_)) {
        *privateAddress_ = 1;
    }
    else if(FASASDescriptor::isDoubleCAS(opType_)) {
        *privateAddress_ = FASASDescriptor::getDCASValue(opType_);
    }
    else {
        *privateAddress_ = newShareVarValue_;
    }

    NVRAM::Flush(sizeof(uint64_t), (void*)privateAddress_);

    isPrivateAddrSet_ = true;
    NVRAM::Flush(sizeof(isPrivateAddrSet_), (void*)&isPrivateAddrSet_);
}

uint64_t OptmizedFASASDescriptor::getValueProtectedOfSharedVar(uint64_t * addr,
    OptmizedFASASDescriptorPool & pool, uint32_t locationId, uint16_t processId) 
{
    uint64_t val = (*addr);
    uint16_t otherProcessId = (uint16_t)((uint64_t)(val & OptmizedFASASDescriptor::ProcessIdFlg)
        >> 48);

    if(otherProcessId != 0 && otherProcessId != processId) {
        OptmizedFASASDescriptor * odescr = pool.getDescriptor(otherProcessId-1, locationId);
        odescr->helpWithShareValue(val);
    }
    
    uint64_t actualValue = val & OptmizedFASASDescriptor::ActualValueFlg;
    return actualValue;
}

uint64_t OptmizedFASASDescriptor::getValueOfPrivateVar(uint64_t * addr) {
    return *addr;
}

OptmizedFASASDescriptorPool::OptmizedFASASDescriptorPool(uint32_t poolSize,
    uint32_t maximumThreadNum, OptmizedFASASDescriptor* descPtr, 
    LocationIndexMapFunc func, bool enableStats)
    : poolSize_(poolSize), maximumThreadNum_(maximumThreadNum), 
      descriptors_(descPtr),locIdMapfunc_(func)
{ 
    MwCASMetrics::enabled = enableStats;

    auto s = MwCASMetrics::Initialize();
    RAW_CHECK(s.ok(), "failed initializing metric objects");

    RAW_CHECK(descriptors_, "null descriptor pool");
    
    BaseDescriptorPool::Metadata *metadata = 
        (BaseDescriptorPool::Metadata*)((uint64_t)descriptors_ - sizeof(BaseDescriptorPool::Metadata));
    //std::cout << "OptmizedFASASDescriptorPool metadata:" << std::hex << metadata << std::endl;

    RAW_CHECK((uint64_t)metadata->initial_address == (uint64_t)metadata,
              "invalid initial address");
    RAW_CHECK(metadata->descriptor_count == poolSize_,
              "wrong descriptor pool size");

    if(descriptors_[0].opType_ != 0) {
        std::cout << "need to recover" << std::endl;
        recoverDescriptor();
    }
    else {
        std::cout << "no need to recover" << std::endl;
        createNewDescriptor();
    }
}

void OptmizedFASASDescriptorPool::createNewDescriptor() {
    uint16_t processId = 0;
    for(uint32_t i = 0; i < poolSize_; ++i) {
        OptmizedFASASDescriptor * desc = descriptors_ + i;
        //processId add one avoiding zero value
        new(desc) OptmizedFASASDescriptor(processId+1);
        processId = (processId+1)%maximumThreadNum_;
    }
}

void OptmizedFASASDescriptorPool::recoverDescriptor() {
    uint64_t in_progress_desc = 0, redo_words = 0;
    for(uint32_t i = 0; i < poolSize_; ++i) {
        OptmizedFASASDescriptor * pdesc = descriptors_ + i;
        if(pdesc->opType_ == 0) {
            RAW_CHECK(false, "corrupted descriptor pool/data area");
            break;
        }

        if(pdesc->sharedAddress_ == nullptr) {
            continue;
        }

        in_progress_desc++;
        recoverOneDescriptor(*pdesc, redo_words);
    }

    std::cout << "recover Found " << in_progress_desc <<
        " in-progress descriptors, rolled forward " << redo_words <<
        " words" << std::endl;
}

void OptmizedFASASDescriptorPool::recoverOneDescriptor(OptmizedFASASDescriptor & descriptor, 
    uint64_t& redo_words) 
{
    uint64_t val = *descriptor.sharedAddress_;
    uint64_t actualValue = val & OptmizedFASASDescriptor::ActualValueFlg;
    uint16_t processId = (uint16_t)((uint64_t)(val & OptmizedFASASDescriptor::ProcessIdFlg) >> 48);
    uint64_t seqId = (val & OptmizedFASASDescriptor::SeqIdFlg) >>60;

    if(processId == descriptor.getProcessId() && seqId == descriptor.getSeqId()) {
        RAW_CHECK(actualValue == descriptor.newShareVarValue_, "value not match");
    }

    if(descriptor.isPrivateAddrSet_) return;

    if(descriptor.statusInfo_ & FASASDescriptor::SuccStatus 
        == FASASDescriptor::SuccStatus) 
    {
       LOG(ERROR) << "recover. descriptor:" << &descriptor << " status succ";
       doRecoverPrivateAddrSucc(descriptor);
       redo_words++;
    }
    else {
        if(processId == descriptor.getProcessId() && seqId == descriptor.getSeqId()) {
            descriptor.statusInfo_ = descriptor.statusInfo_ | FASASDescriptor::SuccStatus;
            NVRAM::Flush(sizeof(descriptor.statusInfo_), (void *)&descriptor.statusInfo_);
            LOG(ERROR) << "recover descriptor:" << &descriptor << " status not succ but value match";
            doRecoverPrivateAddrSucc(descriptor);
            redo_words++;
        }
    }

    descriptor.isShareVarSet_ = true;
}

void OptmizedFASASDescriptorPool::doRecoverPrivateAddrSucc(
    OptmizedFASASDescriptor & descriptor) 
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
        *(privateAddress) = descriptor.getOldShareVarValue();
    }

    NVRAM::Flush(sizeof(uint64_t), (void*)privateAddress);

    descriptor.isPrivateAddrSet_ = true;
    NVRAM::Flush(sizeof(descriptor.isPrivateAddrSet_), 
            (void*)&(descriptor.isPrivateAddrSet_));
    
    LOG(ERROR) << "Applied new value " << std::hex 
        << (*privateAddress) << " to private address:" 
        << std::hex << privateAddress;
}

OptmizedFASASDescriptor * OptmizedFASASDescriptorPool::getDescriptor(uint16_t processId, 
        uint32_t locationId)
{
    RAW_CHECK(processId < maximumThreadNum_, "processId not right ");

    uint64_t offset = maximumThreadNum_*locationId + processId;
    if(offset >= poolSize_) return nullptr;

    OptmizedFASASDescriptor * pdesc = descriptors_ + offset;
    RAW_CHECK(pdesc->getProcessId() == processId+1, "corrupted descriptor pool/data area");

    return pdesc;
}
        
OptmizedFASASDescriptor * OptmizedFASASDescriptorPool::getDescriptor(uint16_t processId, 
        uint64_t * addr)
{
    if(locIdMapfunc_ == nullptr) return nullptr;
    uint32_t locationId = locIdMapfunc_(addr);
    return getDescriptor(processId, locationId);
}

}
