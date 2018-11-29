#include <unordered_map>
#include "MSQueue.h"

namespace pmwcas {

void MSQueueByOrgCas::enq(OrgCasNode ** privateAddr) {
    OrgCasNode * newNode = (*privateAddr);
    RAW_CHECK(nullptr != newNode, "MSQueueByOrgCas::enq node null");

    while(true) {
        QueueNode * last = (*ptail_);
        QueueNode * next = last->next_;
        if(last == (*ptail_)) {
            if(next == NULL) {
                if(CompareExchange64((QueueNode **)(&(last->next_)), (QueueNode *)newNode, (QueueNode *)next) == next) {
                    NVRAM::Flush(sizeof(newNode), (const void*)(&(last->next_)));
                    *(volatile QueueNode **)privateAddr = NULL;
                    //recover not need to know the acutuall operation.
                    //NVRAM::Flush(sizeof(OrgCasNode *), (const void*)(privateAddr));
                    CompareExchange64(ptail_, (QueueNode *)newNode, last);
                    return;
                }
            }
            else {
                NVRAM::Flush(sizeof(newNode), (const void*)(&(last->next_)));
                CompareExchange64(ptail_, next, last);
            }
        } 
    }
}


bool MSQueueByOrgCas::deq(OrgCasNode ** privateAddr, size_t thread_index) {                      
    while (true) {
        OrgCasNode * first = (OrgCasNode *)(*phead_);
        OrgCasNode * last = (OrgCasNode *)(*ptail_);
        OrgCasNode * next = (OrgCasNode *)first->next_;

        if(first == (*phead_)) {
            if(first == last) {
                //empty
                if(next == NULL) {
                    return false;
                }
                else {
                    NVRAM::Flush(sizeof(OrgCasNode *), (const void*)(&(last->next_)));
                    CompareExchange64(ptail_, (QueueNode *)next, (QueueNode *)last);
                }
            }
            else {
                if(first->del_thread_index_ != -1) {
                    continue;
                }
                
                *privateAddr = first;
                NVRAM::Flush(sizeof(OrgCasNode *), (const void*)(privateAddr));
            
                if(CompareExchange64(&first->del_thread_index_, thread_index, (size_t)-1) == -1) {
                    NVRAM::Flush(sizeof(first->del_thread_index_), (const void*)(&first->del_thread_index_));
                    CompareExchange64(phead_, (QueueNode*)next, (QueueNode*)first);
                    //LOG(ERROR) << "deq suc thread_index " << thread_index;
                    return true;
                }
                else {
                    CompareExchange64(phead_, (QueueNode*)next, (QueueNode*)first);
                }
            }
        }
    }
}

void MSQueueByOrgCas::recover(std::unordered_map<OrgCasNode *, OrgCasNode **> const & enqNodeMap, size_t thread_index) {
    OrgCasNode * curNode = (OrgCasNode *)(*phead_);
    while(!isRecoverFinish_) {
        checkEnqNode(enqNodeMap, curNode);

        OrgCasNode * next = (OrgCasNode *)curNode->next_;
        if(next == NULL) {
            isRecoverFinish_ = true;
            LOG(ERROR) << "isRecoverFinish_ set true. thread_index:" << thread_index << std::endl;
            break;
        }
        
        if(curNode->del_thread_index_ != -1) {
            if(*phead_ == curNode ) {
                CompareExchange64(phead_, (QueueNode*)next, (QueueNode*)curNode);
                LOG(ERROR) << "change head to next. thread index:" << thread_index << std::endl;
            }
        }

        if(*ptail_ == curNode ) {
            CompareExchange64(ptail_, (QueueNode *)next, (QueueNode *)curNode);
            LOG(ERROR) << "change tail to next. thread index:" << thread_index << std::endl;
        }
        
        curNode  = next;
    }
}

/*void MSQueueByOrgCas::checkEnqNode(OrgCasNode ** enqAddr, size_t threadCnt,
            OrgCasNode * node) 
{
    for(int i = 0; i < threadCnt; i++) {
        OrgCasNode ** curEndAddr = (OrgCasNode **)(enqAddr+i*8);
        if(*curEndAddr == node) {
            CompareExchange64(curEndAddr, (OrgCasNode *)NULL, node);
            LOG(ERROR) << "checkEnqNode set one node to null. i:" << i << std::endl;
            break;
        }
    }
}*/

void MSQueueByOrgCas::checkEnqNode(std::unordered_map<OrgCasNode *, OrgCasNode **> const & enqNodeMap,
            OrgCasNode * node) 
{
    std::unordered_map<OrgCasNode *, OrgCasNode **>::const_iterator enqNodeMapIt = enqNodeMap.find(node);
    if(enqNodeMapIt != enqNodeMap.end()) {
        OrgCasNode ** curEndAddr = enqNodeMapIt->second;
        CompareExchange64(curEndAddr, (OrgCasNode *)NULL, node);
        LOG(ERROR) << "checkEnqNode set one node to null. curEndAddr:" << curEndAddr << std::endl;
    }
}






}



