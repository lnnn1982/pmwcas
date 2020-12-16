#pragma once

#pragma warning(disable: 4351)

#include <atomic>
#include <cstdint>
#include <list>
#include <mutex>
#include <gtest/gtest_prod.h>
#include "util/macros.h"

namespace pmwcas {

typedef uint64_t NodeEpoch;


struct NodeEntry
{
    NodeEntry()
        : protected_epoch(0),
          thread_id(0) 
    {
    }

    std::atomic<NodeEpoch> protected_epoch; // 8 bytes
    std::atomic<uint64_t> thread_id;    //  8 bytes

    /// Ensure that each Entry is CACHELINE_SIZE.
    char ___padding[48];

    void* operator new[](uint64_t count) {
        void *mem = nullptr;
        int n = posix_memalign(&mem, 64, count);
        return mem;
    }

    void operator delete[](void* p) {
        free(p);
    }

    void* operator new(uint64_t count);
    void operator delete(void* p);
};

class NodeEpochTable {
public:
    static const uint64_t kDefaultSize = 128;

    NodeEpochTable();
    void Initialize(uint64_t size = NodeEpochTable::kDefaultSize);
    void Protect(NodeEpoch currentEpoch);
    void Unprotect();

    NodeEpoch ComputeNewSafeToReclaimEpoch(NodeEpoch currentEpoch);

    void GetEntryForThread(NodeEntry** entry);
    NodeEntry* ReserveEntry(uint64_t startIndex, uint64_t threadId);
    NodeEntry* ReserveEntryForThread();

    NodeEntry* table_;
    uint64_t size_;
};

class NodeEpochManager {
public:
    NodeEpochManager();
    NodeEpochManager(NodeEpoch val);

    void Initialize();

    /*NodeEpoch Protect(NodeEpoch val = 0) {
        NodeEpoch protectEpoch = val;
        if(protectEpoch == 0) {
            protectEpoch = current_epoch_.load(std::memory_order_relaxed);
        }

        epoch_table_->Protect(protectEpoch);
        return protectEpoch;
    }*/

    void Protect() {
        epoch_table_->Protect(current_epoch_.load(std::memory_order_relaxed));
    }

    void Unprotect() {
        epoch_table_->Unprotect();
      }

    NodeEpoch GetCurrentEpoch() {
        return current_epoch_.load(std::memory_order_seq_cst);
    }


    bool IsSafeToReclaim(NodeEpoch epoch) {
        return epoch <= safe_to_reclaim_epoch_.load(std::memory_order_relaxed);
    }

    void BumpCurrentEpoch();
    void ComputeNewSafeToReclaimEpoch(NodeEpoch currentEpoch);

    std::atomic<NodeEpoch> current_epoch_;
    std::atomic<NodeEpoch> safe_to_reclaim_epoch_;
    NodeEpochTable* epoch_table_;

    DISALLOW_COPY_AND_MOVE(NodeEpochManager);

};

}
