#pragma once

#include <stdio.h>
#include <assert.h>
#include <cstdint>
#include <mutex>
#include <gtest/gtest_prod.h>
#include "glog/logging.h"
#include "glog/raw_logging.h"
#include "common/allocator_internal.h"
#include "common/environment_internal.h"
#include "util/nvram.h"
#include "NodeEpoch.h"
#include "NodeGarbageList.h"
#include "MSQueue.h"

namespace pmwcas {

struct alignas(kCacheLineSize) NodePoolPartition {
    NodePoolPartition(NodeEpochManager* epoch);

    PoolNode * nodeFreeList;
    NodeGarbageList * nodeGarbageList;
};




class NodePool {
public:
    uint32_t poolSize_;
    uint32_t partitionCnt_;
    NodePoolPartition * partitionTbl_;
    PoolNode * nodes_;

    NodeEpochManager epochMgr_;

    NodePool(uint32_t poolSize, uint32_t partitionCount, PoolNode * nodeAddr, 
        size_t nodeSize, int nodeType);

    void init(size_t nodeSize, int nodeType);

    PoolNode * AllocateNode(size_t partitionNum);
    void ReleaseNode(size_t partitionNum, PoolNode * node);

    void printNodePoolNum(size_t nodeSize);

    NodeEpochManager* GetEpoch() {
        return &epochMgr_;
    }

    static void FreeNode(void* node, void* context);

};





}









