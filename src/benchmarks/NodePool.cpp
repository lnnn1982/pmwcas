#include "NodePool.h"

namespace pmwcas {

NodePoolPartition::NodePoolPartition(NodeEpochManager* epoch)
{
    nodeFreeList = nullptr;
    nodeGarbageList = (NodeGarbageList*)Allocator::Get()->Allocate(
        sizeof(NodeGarbageList));

    new(nodeGarbageList) NodeGarbageList();
    nodeGarbageList->Initialize(epoch);
}

NodePool::NodePool(uint32_t poolSize, uint32_t partitionCount, PoolNode * nodeAddr, 
    size_t nodeSize, int nodeType)
    : poolSize_(poolSize),
      partitionCnt_(partitionCount),
      partitionTbl_(nullptr),
      nodes_(nodeAddr)
{
    init(nodeSize, nodeType);
}

/*
node type 0: orgNode
node type 1: queueNode
node type 2: logNode
node type 3: log entry;
*/

void NodePool::init(size_t nodeSize, int nodeType)
{
    std::cout << "NodePool::init nodeSize:" << std::dec << nodeSize 
        << ", nodeType:" << nodeType << std::endl;
    
    epochMgr_.Initialize();
    partitionTbl_ = (NodePoolPartition*)Allocator::Get()->AllocateAligned(
        sizeof(NodePoolPartition)*partitionCnt_, kCacheLineSize);

    for(uint32_t i = 0; i < partitionCnt_; ++i) {
        new(&partitionTbl_[i]) NodePoolPartition(&epochMgr_);
    }

    uint32_t nodePerPartition = poolSize_ / partitionCnt_;
    uint32_t busyNodeCnt = 0;
    uint32_t freeNodeCnt = 0;
    uint32_t lastTimeNum = 0;
    uint32_t partitionNum = 0;
    for(uint32_t i = 0; i < poolSize_; ++i) 
    {
        PoolNode * pNode = (PoolNode *)((uintptr_t)nodes_ + nodeSize*i);
        if(nodeType == 0) {
            new(pNode) OrgCasNode();
        }
        else if(nodeType == 1) {
            new(pNode) QueueNode();
        }
        else if(nodeType == 2) {
            new(pNode) LogQueueNode();
        }
        else if(nodeType == 3) {
            new(pNode) LogEntry();
        }

        pNode->initiCommon(i);

        NodePoolPartition * partition = partitionTbl_ + partitionNum;
        pNode->poolNext_ = (QueueNode *)(partition->nodeFreeList);
        partition->nodeFreeList = pNode;

        pNode->initialize();
        
        freeNodeCnt++;


        if((i + 1) % nodePerPartition == 0) {
            std::cout << "NodePool::init node count:" << std::dec << (freeNodeCnt-lastTimeNum) 
                << ", partition:" << partitionNum << std::endl;

            lastTimeNum = freeNodeCnt;
            partitionNum++;
        }
    }

    std::cout << "freeNodeCnt:" << std::dec << freeNodeCnt << ", busyNodeCnt:" 
        << std::dec << busyNodeCnt << std::endl;
}


PoolNode * NodePool::AllocateNode(size_t partitionNum) 
{
    NodePoolPartition * nodePartion = partitionTbl_ + partitionNum;
    PoolNode * node = nodePartion->nodeFreeList;
    while(!node) {
        nodePartion->nodeGarbageList->GetEpoch()->BumpCurrentEpoch();
        nodePartion->nodeGarbageList->Scavenge();
        node = nodePartion->nodeFreeList;
    }

    nodePartion->nodeFreeList = node->poolNext_;
    node->poolNext_ = NULL;
    return node;
}


void NodePool::ReleaseNode(size_t partitionNum, PoolNode * node) 
{
    NodePoolPartition * nodePartion = partitionTbl_ + partitionNum;
    nodePartion->nodeGarbageList->Push(node, nodePartion, NodePool::FreeNode);
} 


void NodePool::FreeNode(void* node, void* context) {
    PoolNode * nodeToFree = reinterpret_cast<PoolNode*>(node);
    nodeToFree->clear();

    NodePoolPartition * nodePartion = reinterpret_cast<NodePoolPartition*>(context);
    nodeToFree->poolNext_ = nodePartion->nodeFreeList;
    nodePartion->nodeFreeList = nodeToFree;
}



void NodePool::printNodePoolNum(size_t nodeSize) {
    std::cout << "printNodePoolNum:";
        
    uint32_t busyNodeCnt = 0;
    uint32_t freeNodeCnt = 0;
    for(uint32_t i = 0; i < poolSize_; ++i) {
        PoolNode * pNode = (PoolNode *)((uintptr_t)nodes_ + nodeSize*i);
        freeNodeCnt++;
    }

    std::cout << "For all nodes, busyNodeCnt:" << busyNodeCnt 
            << ", freeNodeCnt:" << freeNodeCnt << std::endl;

    for(uint32_t i = 0; i < partitionCnt_; ++i) {
        NodePoolPartition* nodePartition = partitionTbl_ + i;

        PoolNode * curNode = (PoolNode *)(nodePartition->nodeFreeList);
        uint32_t len = 0;
        while(curNode) {
            len++;
            curNode = curNode->poolNext_;
        }

        std::cout << "threadNum:" << i << ", node pool len:" << len << std::endl;
    }
}





}






