#ifdef WIN32
#include "environment/environment_windows.h"
#else
#include "environment/environment_linux.h"
#endif
#include "util/atomics.h"
#include "MSQueue.h"

namespace pmwcas {

void LogQueue::enq(LogEntry * logEntry) {
    QueueNode * newNode = logEntry->node_;
    RAW_CHECK(nullptr != newNode, "LogQueue::enq node null");

    while(true) {
        QueueNode * last = (*ptail_);
        QueueNode * next = last->next_;
        if(last == (*ptail_)) {
            if(next == NULL) {
                if(CompareExchange64((QueueNode **)(&(last->next_)), (QueueNode *)newNode, (QueueNode *)next) == next) {
                    NVRAM::Flush(sizeof(newNode), (const void*)(&(last->next_)));
                    CompareExchange64(ptail_, newNode, last);
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

bool LogQueue::deq(LogEntry * logEntry) {
    while(true) {
        LogQueueNode * first = (LogQueueNode *)(*phead_);
        LogQueueNode * last = (LogQueueNode *)(*ptail_);
        LogQueueNode * next = (LogQueueNode *)first->next_;

        if(first == (*phead_)) {
            if(first == last) {
                if(next == NULL) {
                    logEntry->status_ = 1;
                    NVRAM::Flush(sizeof(logEntry->status_), (const void*)(&(logEntry->status_)));
                    return false;
                }

                NVRAM::Flush(sizeof(LogQueue *), (const void*)(&(last->next_)));
                CompareExchange64(ptail_, (QueueNode *)next, (QueueNode *)last);
            }
            else {
                //should be first->next
                if(CompareExchange64(&(first->logRemove_), logEntry, (LogEntry *)NULL) == NULL) {
                    NVRAM::Flush(sizeof(LogEntry *), (const void*)(&(first->logRemove_)));
                    first->logRemove_->node_ = first;
                    NVRAM::Flush(sizeof(LogQueueNode *), (const void*)(&(first->logRemove_->node_)));
                    CompareExchange64(phead_, (QueueNode*)next, (QueueNode*)first);
                    return true;
                }
                else {
                    if(*phead_ == first) {
                        NVRAM::Flush(sizeof(LogEntry *), (const void*)(&(first->logRemove_)));
                        first->logRemove_->node_ = first;
                        NVRAM::Flush(sizeof(LogQueueNode *), (const void*)(&(first->logRemove_->node_)));
                        CompareExchange64(phead_, (QueueNode*)next, (QueueNode*)first);
                    }
                }
            }
        }
    }
}

}

