#pragma once

#include <mutex>
#include <atomic>
#include <iostream>
#include "util/macros.h"
#include "util/atomics.h"
#include "common/allocator_internal.h"
#include "NodeEpoch.h"

namespace pmwcas {

class NodeGarbageList {
public:

    typedef void (*DestroyCallback)(void* object, void* callbackContext);

    struct Item {
        NodeEpoch removalEpoch;
        DestroyCallback destroyCallback;
        void* removedItem;
        void* callbackContext;
    };

    NodeGarbageList()
        : epochManager_(NULL)
        , tail_(0)
        , itemCnt_(0)
        , items_(NULL)
    {
    }

    void Initialize(NodeEpochManager* epochManager, size_t itemCnt = 128 * 1024)
    {
        if(epochManager_) return;
        assert(IS_POWER_OF_TWO(itemCnt));

        size_t nItemArraySize = sizeof(*items_) * itemCnt;
        items_ = reinterpret_cast<Item*>(Allocator::Get()->AllocateAligned(
            nItemArraySize, 64));
        for(size_t i = 0; i < itemCnt; ++i) {
            new(&items_[i]) Item();
            items_[i].removalEpoch = 0;
        }

        itemCnt_ = itemCnt;
        tail_ = 0;
        epochManager_ = epochManager;
    }

    void Push(void* removedItem, void* callbackContext, DestroyCallback callback) 
    {
        NodeEpoch removalEpoch = epochManager_->GetCurrentEpoch();

        for(;;) 
        {
            int64_t slot = (tail_++) & (itemCnt_ - 1);

            if(((slot << 2) & (itemCnt_ - 1)) == 0) {
                epochManager_->BumpCurrentEpoch();
            }

            Item& item = items_[slot];

            NodeEpoch priorItemEpoch = item.removalEpoch;
            if(priorItemEpoch) {
                if(!epochManager_->IsSafeToReclaim(priorItemEpoch)) {
                    continue;
                }
                item.destroyCallback(item.removedItem, item.callbackContext);
            }

            item.destroyCallback = callback;
            item.removedItem = removedItem;
            item.callbackContext = callbackContext;
            item.removalEpoch = removalEpoch;
            return;
        }
    }

    void Scavenge() 
    {
        for(int64_t slot = 0; slot < itemCnt_; ++slot) 
        {
            Item & item = items_[slot];
            NodeEpoch priorItemEpoch = item.removalEpoch;
            if(priorItemEpoch == 0 ) {
                continue;
            }

            if(priorItemEpoch) {
                if(!epochManager_->IsSafeToReclaim(priorItemEpoch)) {
                    continue;
                }
                item.destroyCallback(item.removedItem, item.callbackContext);
            }

            item.destroyCallback = nullptr;
            item.removedItem = nullptr;
            item.callbackContext = nullptr;
            *((volatile NodeEpoch*) &item.removalEpoch) = 0;
        }
    }

    NodeEpochManager* GetEpoch() {
        return epochManager_;
    }




    NodeEpochManager* epochManager_;
    int64_t tail_;
    size_t itemCnt_;
    Item* items_;


};

}




