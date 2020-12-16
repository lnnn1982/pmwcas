#include "common/environment_internal.h"
#include "NodeEpoch.h"

namespace pmwcas {

NodeEpochTable::NodeEpochTable()
    : table_(nullptr),
      size_(0) 
{
}

void NodeEpochTable::Initialize(uint64_t size) {
    if(table_) return;
    assert(IS_POWER_OF_TWO(size));

    NodeEntry* new_table= new NodeEntry[size];
    assert(!(reinterpret_cast<uintptr_t>(new_table)& (64 - 1)));

    table_ = new_table;
    size_ = size;
}

void NodeEpochTable::Protect(NodeEpoch current_epoch) 
{
    NodeEntry* entry = nullptr;
    GetEntryForThread(&entry);

    entry->protected_epoch.store(current_epoch, std::memory_order_release);
    //std::atomic_thread_fence(std::memory_order_acquire);
}

void NodeEpochTable::Unprotect() {
    NodeEntry* entry = nullptr;
    GetEntryForThread(&entry);

    //std::atomic_thread_fence(std::memory_order_release);
    entry->protected_epoch.store(0, std::memory_order_relaxed);
}

NodeEpoch NodeEpochTable::ComputeNewSafeToReclaimEpoch(
  NodeEpoch current_epoch) 
{
    NodeEpoch oldest_call = current_epoch;
    for(uint64_t i = 0; i < size_; ++i) 
    {
        NodeEntry& entry = table_[i];
        NodeEpoch entryEpoch =
            entry.protected_epoch.load(std::memory_order_acquire);
        if(entryEpoch != 0 && entryEpoch < oldest_call) {
            oldest_call = entryEpoch;
        }
    }
    // The latest safe epoch is the one just before the earlier unsafe one.
    return oldest_call - 1;
}

void NodeEpochTable::GetEntryForThread(NodeEntry** entry) {
    thread_local NodeEntry *tls = nullptr;
    if(tls) {
        *entry = tls;
        return;
    }

    NodeEntry* reserved = ReserveEntryForThread();
    tls = *entry = reserved;
}

uint32_t NodeMurmur3(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}


NodeEntry* NodeEpochTable::ReserveEntryForThread() {
  uint64_t current_thread_id = Environment::Get()->GetThreadId();
  uint64_t startIndex = NodeMurmur3(current_thread_id);
  return ReserveEntry(startIndex, current_thread_id);
}

NodeEntry* NodeEpochTable::ReserveEntry(uint64_t start_index,
    uint64_t thread_id) 
{
    for(;;) 
    {
        // Reserve an entry in the table.
        for(uint64_t i = 0; i < size_; ++i) 
        {
            uint64_t indexToTest = (start_index + i) & (size_ - 1);
            NodeEntry& entry = table_[indexToTest];

            if(entry.thread_id == 0) {
                uint64_t expected = 0;
                bool success = entry.thread_id.compare_exchange_strong(expected,
                    thread_id, std::memory_order_relaxed);
                if(success) {
                    return &table_[indexToTest];
                }
            }
        }
    }
}


NodeEpochManager::NodeEpochManager()
    : current_epoch_(1)
    , safe_to_reclaim_epoch_(0)
    , epoch_table_(nullptr) 
{
}

NodeEpochManager::NodeEpochManager(NodeEpoch val)
    : current_epoch_(val)
    , safe_to_reclaim_epoch_(val-1)
    , epoch_table_(nullptr) 
{
}


void NodeEpochManager::Initialize() 
{
      if(epoch_table_) return;

      NodeEpochTable* new_table = new NodeEpochTable();
      new_table->Initialize();
      epoch_table_ = new_table;
}


void NodeEpochManager::BumpCurrentEpoch() {
    NodeEpoch newEpoch = current_epoch_.fetch_add(1, std::memory_order_seq_cst);
    ComputeNewSafeToReclaimEpoch(newEpoch);
}


void NodeEpochManager::ComputeNewSafeToReclaimEpoch(NodeEpoch currentEpoch) {
    safe_to_reclaim_epoch_.store(
        epoch_table_->ComputeNewSafeToReclaimEpoch(currentEpoch),
    std::memory_order_release);
}






}


