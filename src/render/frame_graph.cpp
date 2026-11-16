#include <render/frame_graph.hpp>
#include <logger.hpp>
#include <algorithm>
#ifdef VKE_ENABLE_FRAME_GRAPH_PROFILING
#include <chrono>
#endif

namespace vke_render
{

    void FrameGraph::init()
    {
        queueFamilies[RENDER_TASK] = RenderEnvironment::GetQueueFamilyIndex(GRAPHICS_QUEUE);
        queueFamilies[COMPUTE_TASK] = RenderEnvironment::GetQueueFamilyIndex(COMPUTE_QUEUE);
        queueFamilies[TRANSFER_TASK] = RenderEnvironment::GetQueueFamilyIndex(TRANSFER_QUEUE);

        for (int i = 0; i < framesInFlight; i++)
            cpuSemaphores.push_back(std::make_unique<std::atomic<bool>>(true));

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 1; i < TASK_TYPE_CNT - 1; i++)
            if (RenderEnvironment::HasQueue(QueueType(i)))
                for (int j = 0; j < framesInFlight; j++)
                    vkCreateFence(globalLogicalDevice, &fenceInfo, nullptr, &fences[i - 1][j]);

        for (int i = 0; i < TASK_TYPE_CNT - 1; i++)
            if (RenderEnvironment::HasQueue(QueueType(i)))
                for (int j = 0; j < framesInFlight; j++)
                    commandPools[i].push_back(std::make_unique<GPUCommandPool>(queueFamilies[i], VK_COMMAND_POOL_CREATE_TRANSIENT_BIT));
        for (int j = 0; j < framesInFlight; j++)
            commandPools[CPU_TASK].push_back(std::make_unique<CPUCommandPool>());

        for (int j = 0; j < framesInFlight; j++)
            semaphorePools[j] = std::make_unique<SemaphorePool>();

#ifdef VKE_ENABLE_FRAME_GRAPH_PROFILING
        initProfiler();
#endif
    }

    void FrameGraph::ensureTaskSemaphore(const uint32_t currentFrame, TaskNode &taskNode)
    {
        if (taskNode.HasCurrentSemaphore())
            return;

        SemaphorePool &semaphorePool = *semaphorePools[currentFrame];
        taskNode.currentSemaphore = semaphorePool.AllocateSemaphore(taskNode.currentSemaphoreValue);
    }

    bool FrameGraph::assignNextTaskSemaphore(const uint32_t currentFrame, TaskNode &taskNode, TaskNode &nextTaskNode)
    {
        if (nextTaskNode.HasCurrentSemaphore())
            return false;

        nextTaskNode.currentSemaphore = taskNode.currentSemaphore;
        nextTaskNode.currentSemaphoreValue = taskNode.currentSemaphoreValue + 1;
        semaphorePools[currentFrame]->SetValue(nextTaskNode.currentSemaphore, nextTaskNode.currentSemaphoreValue);
        return true;
    }

    void FrameGraph::checkCrossQueue(ResourceRef &ref, TaskNode &taskNode)
    {
        ResourceNode &resourceNode = *resourceNodes[ref.GetResourceNodeID()];
        auto &resource = resources[resourceNode.resourceID];

        if (resource->tmpPrevUsedTask != nullptr && resource->tmpPrevUsedTask->actualTaskType != taskNode.actualTaskType)
        {
            resource->tmpPrevUsedTask->needQueueSubmit = true;
            taskNode.needQueueSubmit = true;
            resource->tmpPrevUsedRef->crossQueueTask = &taskNode;
        }

        resource->tmpPrevUsedTask = &taskNode;
        resource->tmpPrevUsedRef = &ref;
    }

    void FrameGraph::Compile()
    {
        VKE_LOG_INFO("-------------------------- BEGIN COMPILE-----------------------")
        orderedTasks.clear();
        std::set<vke_ds::id32_t> visited;
        std::queue<vke_ds::id32_t> taskQueue;

        // find valid tasks

        VKE_LOG_INFO("task node cnt {}", taskNodes.size())

        for (auto &kv : taskNodes)
        {
            TaskNode &node = *kv.second;
            node.Reset();

            uint32_t odeg = 0;
            for (auto &ref : node.resourceRefs)
                if (ref.second.storeOp == VK_ATTACHMENT_STORE_OP_STORE)
                    ++odeg;

            for (auto &[resourceID, ref] : node.resourceRefs)
            {
                if (ref.storeOp == VK_ATTACHMENT_STORE_OP_STORE)
                {
                    ResourceNode &resourceNode = *resourceNodes[ref.outResourceNodeID];
                    if (resourceNode.dstTaskIDs.size() == 0 && targetResources.find(resourceNode.resourceID) != targetResources.end())
                    {
                        taskQueue.push(kv.first);
                        visited.insert(kv.first);
                        break;
                    }
                }
                else if (ref.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD && odeg == 0)
                {
                    ResourceNode &resourceNode = *resourceNodes[ref.inResourceNodeID];
                    if (targetResources.find(resourceID) != targetResources.end())
                    {
                        taskQueue.push(kv.first);
                        visited.insert(kv.first);
                        break;
                    }
                }
            }
        }
        VKE_LOG_INFO("final out cnt {}", taskQueue.size())

        while (!taskQueue.empty())
        {
            TaskNode &node = *taskNodes[taskQueue.front()];
            taskQueue.pop();
            node.valid = true;

            for (auto &[resourceID, ref] : node.resourceRefs)
            {
                ref.crossQueueTask = nullptr;
                if (ref.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
                {
                    ResourceNode &resourceNode = *resourceNodes[ref.inResourceNodeID];
                    vke_ds::id32_t srcTaskID = resourceNode.srcTaskID;
                    if (srcTaskID != 0)
                    {
                        ++node.indeg;
                        if (visited.find(srcTaskID) == visited.end())
                        {
                            taskQueue.push(srcTaskID);
                            visited.insert(srcTaskID);
                        }
                    }
                }
            }
        }

        // sort tasks
        for (auto &kv : taskNodes)
        {
            TaskNode &node = *kv.second;
            if (node.valid && node.indeg == 0)
                taskQueue.push(kv.first);
        }

        while (!taskQueue.empty())
        {
            TaskNode &node = *taskNodes[taskQueue.front()];
            taskQueue.pop();
            orderedTasks.push_back(node.taskID);

            for (auto &ref : node.resourceRefs)
            {
                if (ref.second.storeOp == VK_ATTACHMENT_STORE_OP_STORE)
                {
                    vke_ds::id32_t resourceNodeID = ref.second.outResourceNodeID;
                    ResourceNode &resourceNode = *resourceNodes[resourceNodeID];
                    for (auto dstTaskID : resourceNode.dstTaskIDs)
                    {
                        TaskNode &dstNode = *taskNodes[dstTaskID];
                        if (dstNode.valid)
                        {
                            dstNode.indeg--;
                            if (dstNode.indeg == 0)
                                taskQueue.push(dstTaskID);
                        }
                    }
                }
            }
        }

        for (auto &kv : resources)
            kv.second->ResetTmpValues();

        for (int i = 0; i < TASK_TYPE_CNT; ++i)
            submitCntEstimates[i] = 0;

        for (auto taskID : orderedTasks)
        {
            TaskNode &taskNode = *taskNodes[taskID];
            for (auto &ref : taskNode.resourceRefs)
                checkCrossQueue(ref.second, taskNode);
        }

        // construct first access
        for (auto taskID : orderedTasks)
        {
            TaskNode &taskNode = *taskNodes[taskID];
            for (auto &[resourceID, ref] : taskNode.resourceRefs)
            {
                ResourceNode &resourceNode = *resourceNodes[ref.outResourceNodeID == 0 ? ref.inResourceNodeID : ref.outResourceNodeID]; // first use out
                auto &resource = resources[resourceID];
                if (resource->firstAccessTaskID == 0)
                    resource->firstAccessTaskID = taskID;
            }
        }
        // construct last access
        for (int i = orderedTasks.size() - 1; i >= 0; --i)
        {
            vke_ds::id32_t taskID = orderedTasks[i];
            TaskNode &taskNode = *taskNodes[taskID];
            for (auto &[resourceID, ref] : taskNode.resourceRefs)
            {
                ResourceNode &resourceNode = *resourceNodes[ref.outResourceNodeID == 0 ? ref.inResourceNodeID : ref.outResourceNodeID]; // last use out
                auto &resource = resources[resourceID];
                if (resource->lastAccessTaskID == 0)
                    resource->lastAccessTaskID = taskID;
            }

            uint32_t &cnt = submitCntEstimates[taskNode.actualTaskType];
            if (cnt == 0)
            {
                taskNode.isFinalTask = true;
                ++cnt;
            }
            else if (taskNode.needQueueSubmit)
                ++cnt;
        }

        // calculate transient memory usage
        {
            transientMemoryAllocationMap.clear();
            transientMemorySimulator.Reset();

            VkMemoryRequirements memoryReq;
            for (auto taskID : orderedTasks)
            {
                TaskNode &taskNode = *taskNodes[taskID];
                for (auto &[resourceID, ref] : taskNode.resourceRefs)
                {
                    ResourceNode &resourceNode = *resourceNodes[ref.GetResourceNodeID()];
                    auto &resource = resources[resourceID];
                    if (!resource->isTransient)
                        continue;
                    if (resource->firstAccessTaskID == taskID) // transient first access must be RENDER/COMPUTE TASK
                    {
                        VKE_FATAL_IF((taskNode.actualTaskType != RENDER_TASK) && (taskNode.actualTaskType != COMPUTE_TASK), "transient first access must be RENDER/COMPUTE TASK")
                        recreateTransientResource(resourceID, *resource, 0);
                        if (resource->resourceType == IMAGE_RESOURCE)
                        {
                            ImageResource *imageResource = (ImageResource *)resource.get();
                            vkGetImageMemoryRequirements(globalLogicalDevice, imageResource->images[0], &memoryReq);
                        }
                        else
                        {
                            BufferResource *bufferResource = (BufferResource *)resource.get();
                            vkGetBufferMemoryRequirements(globalLogicalDevice, bufferResource->buffers[0], &memoryReq);
                        }
                        transientMemoryAllocationMap[resourceID] = transientMemorySimulator.PreAllocMemory(taskNode.actualTaskType, memoryReq);
                    }
                }

                for (auto &[resourceID, ref] : taskNode.resourceRefs)
                {
                    ResourceNode &resourceNode = *resourceNodes[ref.GetResourceNodeID()];
                    auto &resource = resources[resourceID];
                    if (!resource->isTransient)
                        continue;
                    if (resource->lastAccessTaskID == taskID)
                    {
                        TransientMemoryAllocation &allocation = transientMemoryAllocationMap[resourceID];
                        VKE_FATAL_IF(allocation.type != taskNode.actualTaskType, "transient resource must be de/allocated from the same queue")
                        transientMemorySimulator.PreDeallocMemory(allocation);
                    }
                }
            }
            transientMemoryUpdateCnt = framesInFlight;
        }
        needRecompile = false;
        VKE_LOG_INFO("orderedTask cnt {}", orderedTasks.size())
        for (auto taskID : orderedTasks)
            VKE_LOG_INFO("task id {}", taskID)
        VKE_LOG_INFO("-------------------------- END COMPILE-----------------------")
    }

    void FrameGraph::cleanupTransientResource(RenderResource &resource)
    {
        if (!resource.isTransient)
            return;

        if (resource.resourceType == IMAGE_RESOURCE)
        {
            ImageResource *imageResource = static_cast<ImageResource *>(&resource);
            for (VkImage &image : imageResource->images)
            {
                if (image != VK_NULL_HANDLE)
                {
                    vkDestroyImage(globalLogicalDevice, image, nullptr);
                    image = VK_NULL_HANDLE;
                }
            }
        }
        else
        {
            BufferResource *bufferResource = static_cast<BufferResource *>(&resource);
            for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
            {
                if (bufferResource->buffers[i] != VK_NULL_HANDLE)
                {
                    vkDestroyBuffer(globalLogicalDevice, bufferResource->buffers[i], nullptr);
                    bufferResource->buffers[i] = VK_NULL_HANDLE;
                }
            }
        }
    }

    void FrameGraph::cleanupTransientResources()
    {
        for (auto &[id, resource] : resources)
            cleanupTransientResource(*resource);
    }

    void FrameGraph::recreateTransientResource(const vke_ds::id32_t resourceID, RenderResource &resource, const uint32_t currentFrame)
    {
        VKE_FATAL_IF(!resource.isTransient, "resource not transient")

        if (resource.resourceType == IMAGE_RESOURCE)
        {
            ImageResource *imageResource = static_cast<ImageResource *>(&resource);
            VKE_FATAL_IF(transientImageCreateInfos.find(resourceID) == transientImageCreateInfos.end(), "transient image create info not found")
            if (imageResource->images[currentFrame] != VK_NULL_HANDLE)
                vkDestroyImage(globalLogicalDevice, imageResource->images[currentFrame], nullptr);
            VKE_VK_CHECK(vkCreateImage(globalLogicalDevice, &transientImageCreateInfos[resourceID], nullptr, &imageResource->images[currentFrame]),
                         "failed to create transient image!")
        }
        else
        {
            BufferResource *bufferResource = static_cast<BufferResource *>(&resource);
            VKE_FATAL_IF(transientBufferCreateInfos.find(resourceID) == transientBufferCreateInfos.end(), "transient buffer create info not found")
            if (bufferResource->buffers[currentFrame] != VK_NULL_HANDLE)
                vkDestroyBuffer(globalLogicalDevice, bufferResource->buffers[currentFrame], nullptr);
            VKE_VK_CHECK(vkCreateBuffer(globalLogicalDevice, &transientBufferCreateInfos[resourceID], nullptr, &bufferResource->buffers[currentFrame]),
                         "failed to create transient buffer!")
        }
    }

    void FrameGraph::syncResources(const uint32_t currentFrame, const uint32_t imageIndex,
                                   ResourceRef &ref, TaskNode &taskNode,
                                   bool &needQueueSubmit,
                                   std::vector<VkBufferMemoryBarrier2> &bufferMemoryBarriers,
                                   std::vector<VkImageMemoryBarrier2> &imageMemoryBarriers,
                                   std::map<VkSemaphore, uint64_t> &waitSemaphoreMap,
                                   VkPipelineStageFlags2 &waitDstStageMask)
    {
        ResourceNode &resourceNode = *resourceNodes[ref.GetResourceNodeID()];
        auto &resource = resources[resourceNode.resourceID];
        // std::cout << "----SYNC RESOURCE " << "TASK " << taskNode.name << " RESOURCE_NODE " << resourceNode.name << " RESOURCE " << resource->name << "\n";
        // std::cout << "resource prevUsedTask " << resource->prevUsedTask << " " << resource->prevUsedRef << "\n";
        bool layoutChanged = false;
        if (resource->firstAccessTaskID == taskNode.taskID) // first use of resource
        {
            if (resource->isTransient)
            {
                layoutChanged = true;
                if (resource->resourceType == IMAGE_RESOURCE)
                {
                    ImageResource *imageResource = (ImageResource *)resource.get();
                    imageMemoryBarriers.emplace_back();
                    VkImageMemoryBarrier2 &barrier = imageMemoryBarriers.back();
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                    barrier.srcAccessMask = VK_ACCESS_2_NONE;
                    barrier.dstStageMask = ref.stageMask;
                    barrier.dstAccessMask = ref.accessMask;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    barrier.newLayout = ref.imageLayout;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = imageResource->GetCurrentImage(currentFrame, imageIndex);
                    barrier.subresourceRange.aspectMask = imageResource->aspectMask;
                    barrier.subresourceRange.baseMipLevel = 0;
                    barrier.subresourceRange.levelCount = imageResource->mipLevelCnt;
                    barrier.subresourceRange.baseArrayLayer = 0;
                    barrier.subresourceRange.layerCount = imageResource->layerCnt;
                }
                else // BUFFER_RESOURCE
                {
                    BufferResource *bufferResource = (BufferResource *)resource.get();
                    bufferMemoryBarriers.emplace_back();
                    VkBufferMemoryBarrier2 &barrier = bufferMemoryBarriers.back();
                    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                    barrier.srcAccessMask = VK_ACCESS_NONE;
                    barrier.dstStageMask = ref.stageMask;
                    barrier.dstAccessMask = ref.accessMask;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.buffer = bufferResource->GetCurrentBuffer(currentFrame, imageIndex);
                    barrier.offset = bufferResource->offset;
                    barrier.size = bufferResource->size;
                }
            }
            else if (resource->resourceType == IMAGE_RESOURCE &&
                     permanentResourceStates[resourceNode.resourceID].stImageLayout.has_value())
            {
                PermanentResourceState &state = permanentResourceStates[resourceNode.resourceID];
                if (state.stImageLayout.has_value() &&
                    state.stImageLayout.value() != ref.imageLayout)
                {
                    layoutChanged = true;
                    // std::cout << "  FIRST IMAGE USE\n";
                    ImageResource *imageResource = (ImageResource *)resource.get();
                    VKE_LOG_DEBUG("FIRST IMAGE USE {} {}", imageResource->name, (void *)(imageResource->GetCurrentImage(currentFrame, imageIndex)))
                    imageMemoryBarriers.emplace_back();
                    VkImageMemoryBarrier2 &barrier = imageMemoryBarriers.back();
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    barrier.pNext = nullptr;
                    barrier.srcStageMask = state.stStage;
                    barrier.srcAccessMask = VK_ACCESS_NONE;
                    barrier.dstStageMask = ref.stageMask;
                    barrier.dstAccessMask = ref.accessMask;
                    barrier.oldLayout = state.stImageLayout.value();
                    barrier.newLayout = ref.imageLayout;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = imageResource->GetCurrentImage(currentFrame, imageIndex);
                    barrier.subresourceRange.aspectMask = imageResource->aspectMask;
                    barrier.subresourceRange.baseMipLevel = 0;
                    barrier.subresourceRange.levelCount = imageResource->mipLevelCnt;
                    barrier.subresourceRange.baseArrayLayer = 0;
                    barrier.subresourceRange.layerCount = imageResource->layerCnt;
                }
            }
        }
        else if (resource->prevUsedTask != nullptr)
        {
            ResourceRef &prevUsedRef = *(resource->prevUsedRef);
            TaskNode &prevUsedTask = *(resource->prevUsedTask);
            // std::cout << "PREVUSED TASK " << prevUsedTask.name << "\n";
            if (prevUsedTask.actualTaskType != taskNode.actualTaskType) // cross queue
            {
                // VKE_LOG_DEBUG("CROSS QUEUE {} {}", (uint32_t)prevUsedRef.imageLayout, (uint32_t)ref.imageLayout)
                needQueueSubmit = true;
                waitDstStageMask |= ref.stageMask;

                VKE_FATAL_IF(!prevUsedTask.HasCurrentSemaphore() && !prevUsedTask.HasLastSemaphore(), "cross queue wait without previous semaphore")
                VkSemaphore prevSemaphore = prevUsedTask.HasCurrentSemaphore() ? prevUsedTask.currentSemaphore : prevUsedTask.lastSemaphore;
                const uint64_t prevSemaphoreValue = prevUsedTask.HasCurrentSemaphore() ? prevUsedTask.currentSemaphoreValue : prevUsedTask.lastSemaphoreValue;
                auto it = waitSemaphoreMap.find(prevSemaphore);
                if (it == waitSemaphoreMap.end())
                    waitSemaphoreMap[prevSemaphore] = prevSemaphoreValue;
                else
                    waitSemaphoreMap[prevSemaphore] = std::max(it->second, prevSemaphoreValue);

                if (resource->resourceType == IMAGE_RESOURCE &&
                    ((resource->prevWrite && ref.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD) ||
                     (prevUsedRef.imageLayout != ref.imageLayout)))
                {
                    // std::cout << "  CROSS QUEUE IMAGE RECEIVE\n";
                    layoutChanged = prevUsedRef.imageLayout != ref.imageLayout;
                    if (layoutChanged)
                        waitDstStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; // prevent layout transfer conflict with prev write op in another queue
                    ImageResource *imageResource = (ImageResource *)resource.get();
                    VKE_LOG_DEBUG("CROSS QUEUE IMAGE RECEIVE {} {}", imageResource->name, (void *)(imageResource->GetCurrentImage(currentFrame, imageIndex)))
                    imageMemoryBarriers.emplace_back();
                    VkImageMemoryBarrier2 &barrier = imageMemoryBarriers.back();
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    barrier.pNext = nullptr;
                    barrier.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                    barrier.srcAccessMask = VK_ACCESS_NONE;
                    barrier.dstStageMask = ref.stageMask;
                    barrier.dstAccessMask = ref.accessMask;
                    barrier.oldLayout = prevUsedRef.imageLayout;
                    barrier.newLayout = ref.imageLayout;
                    if (prevUsedTask.actualTaskType != CPU_TASK && taskNode.actualTaskType != CPU_TASK &&
                        resource->prevWrite && ref.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
                    {
                        VKE_LOG_DEBUG("REAL CROSS QUEUE IMAGE RECEIVE")
                        barrier.srcQueueFamilyIndex = queueFamilies[resource->prevUsedTask->actualTaskType];
                        barrier.dstQueueFamilyIndex = queueFamilies[taskNode.actualTaskType];
                    }
                    else
                    {
                        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    }
                    barrier.image = imageResource->GetCurrentImage(currentFrame, imageIndex);
                    barrier.subresourceRange.aspectMask = imageResource->aspectMask;
                    barrier.subresourceRange.baseMipLevel = 0;
                    barrier.subresourceRange.levelCount = imageResource->mipLevelCnt;
                    barrier.subresourceRange.baseArrayLayer = 0;
                    barrier.subresourceRange.layerCount = imageResource->layerCnt;
                }
            }
            else
            {
                // std::cout << "  SAME QUEUE\n";
                if ((ref.storeOp == VK_ATTACHMENT_STORE_OP_STORE) ||
                    resource->prevWrite ||
                    ((resource->resourceType == IMAGE_RESOURCE) && (prevUsedRef.imageLayout != ref.imageLayout)))
                {
                    bool war = (!resource->prevWrite &&
                                prevUsedRef.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD &&
                                ref.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
                    // std::cout << "  NEED SYNC\n";
                    if (resource->resourceType == IMAGE_RESOURCE)
                    {
                        layoutChanged = prevUsedRef.imageLayout != ref.imageLayout;
                        war = war && prevUsedRef.imageLayout == ref.imageLayout;
                        // std::cout << "  IMAGE SYNC\n";
                        ImageResource *imageResource = (ImageResource *)resource.get();
                        imageMemoryBarriers.emplace_back();
                        VkImageMemoryBarrier2 &barrier = imageMemoryBarriers.back();
                        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                        barrier.pNext = nullptr;
                        barrier.srcStageMask = prevUsedRef.stageMask;
                        barrier.srcAccessMask = war ? VK_ACCESS_NONE : prevUsedRef.accessMask;
                        barrier.dstStageMask = ref.stageMask;
                        barrier.dstAccessMask = war ? VK_ACCESS_NONE : ref.accessMask;
                        barrier.oldLayout = prevUsedRef.imageLayout;
                        barrier.newLayout = ref.imageLayout;
                        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.image = imageResource->GetCurrentImage(currentFrame, imageIndex);
                        barrier.subresourceRange.aspectMask = imageResource->aspectMask;
                        barrier.subresourceRange.baseMipLevel = 0;
                        barrier.subresourceRange.levelCount = imageResource->mipLevelCnt;
                        barrier.subresourceRange.baseArrayLayer = 0;
                        barrier.subresourceRange.layerCount = imageResource->layerCnt;
                    }
                    else
                    {
                        // std::cout << "  BUFFER SYNC\n";
                        BufferResource *bufferResource = (BufferResource *)resource.get();
                        bufferMemoryBarriers.emplace_back();
                        VkBufferMemoryBarrier2 &barrier = bufferMemoryBarriers.back();
                        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                        barrier.pNext = nullptr;
                        barrier.srcStageMask = prevUsedRef.stageMask;
                        barrier.srcAccessMask = war ? VK_ACCESS_NONE : prevUsedRef.accessMask;
                        barrier.dstStageMask = ref.stageMask;
                        barrier.dstAccessMask = war ? VK_ACCESS_NONE : ref.accessMask;
                        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.buffer = bufferResource->GetCurrentBuffer(currentFrame, imageIndex);
                        barrier.offset = bufferResource->offset;
                        barrier.size = bufferResource->size;
                    }
                }
            }
        }
        resource->prevWrite = (ref.storeOp == VK_ATTACHMENT_STORE_OP_STORE) || layoutChanged;
        resource->prevUsedRef = &ref;
        resource->prevUsedTask = &taskNode;
    }

    void FrameGraph::endResourcesUse(const uint32_t currentFrame, const uint32_t imageIndex,
                                     ResourceRef &ref, TaskNode &taskNode,
                                     bool &needQueueSubmit,
                                     std::vector<VkBufferMemoryBarrier2> &bufferMemoryBarriers,
                                     std::vector<VkImageMemoryBarrier2> &imageMemoryBarriers)
    {
        ResourceNode &resourceNode = *resourceNodes[ref.outResourceNodeID == 0 ? ref.inResourceNodeID : ref.outResourceNodeID]; // first use out
        auto &resource = resources[resourceNode.resourceID];
        // std::cout << "----END RESOURCE " << "TASK " << taskNode.name << " RESOURCE_NODE " << resourceNode.name << " RESOURCE " << resourceNode.name << "\n";
        if (ref.crossQueueTask != nullptr)
        {
            // std::cout << "  CROSS QUEUE\n";
            ResourceRef &crossQueueRef = ref.crossQueueTask->resourceRefs[resourceNode.resourceID];
            needQueueSubmit = true;
            if (resource->resourceType == IMAGE_RESOURCE &&
                resource->prevWrite && // been set by sync resource
                crossQueueRef.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
            {
                ImageResource *imageResource = (ImageResource *)resource.get();
                VKE_LOG_DEBUG("CROSS QUEUE IMAGE RELEASE {} {}", imageResource->name, (void *)(imageResource->GetCurrentImage(currentFrame, imageIndex)))
                imageMemoryBarriers.emplace_back();
                VkImageMemoryBarrier2 &barrier = imageMemoryBarriers.back();
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                barrier.pNext = nullptr;
                barrier.srcStageMask = ref.stageMask;
                barrier.srcAccessMask = ref.accessMask;
                barrier.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                barrier.dstAccessMask = VK_ACCESS_NONE;
                barrier.oldLayout = ref.imageLayout;
                barrier.newLayout = ref.imageLayout;
                if (taskNode.actualTaskType != CPU_TASK &&
                    ref.crossQueueTask->actualTaskType != CPU_TASK)
                {
                    VKE_LOG_DEBUG("REAL CROSS QUEUE IMAGE RELEASE")
                    barrier.srcQueueFamilyIndex = queueFamilies[taskNode.actualTaskType];
                    barrier.dstQueueFamilyIndex = queueFamilies[ref.crossQueueTask->actualTaskType];
                }
                else
                {
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                }

                barrier.image = imageResource->GetCurrentImage(currentFrame, imageIndex);
                barrier.subresourceRange.aspectMask = imageResource->aspectMask;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = imageResource->mipLevelCnt;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = imageResource->layerCnt;
            }
        }
        else if (!resource->isTransient && resource->lastAccessTaskID == taskNode.taskID && resource->resourceType == IMAGE_RESOURCE) // TODO final op 如何判断是最后一个？尤其是最后一个task是读的情况，此时没有任何对应的resourceNode.dstTaskIDs.size() == 0
        {
            PermanentResourceState &state = permanentResourceStates[resourceNode.resourceID];
            if (state.enImageLayout.has_value() &&
                state.enImageLayout.value() != ref.imageLayout)
            {
                // std::cout << "  FINAL IMAGE USE\n";
                ImageResource *imageResource = (ImageResource *)resource.get();
                imageMemoryBarriers.emplace_back();
                VkImageMemoryBarrier2 &barrier = imageMemoryBarriers.back();
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                barrier.pNext = nullptr;
                barrier.srcStageMask = ref.stageMask;
                barrier.srcAccessMask = ref.accessMask;
                barrier.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                barrier.dstAccessMask = VK_ACCESS_NONE;
                barrier.oldLayout = ref.imageLayout;
                barrier.newLayout = state.enImageLayout.value();
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = imageResource->GetCurrentImage(currentFrame, imageIndex);
                barrier.subresourceRange.aspectMask = imageResource->aspectMask;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = imageResource->mipLevelCnt;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = imageResource->layerCnt;
            }
        }
    }

    void FrameGraph::Sync(const uint32_t currentFrame)
    {
        for (int i = 1; i < TASK_TYPE_CNT - 1; ++i)
            if (RenderEnvironment::HasQueue(QueueType(i)) && vkGetFenceStatus(globalLogicalDevice, fences[i - 1][currentFrame]) == VK_NOT_READY)
            {
                VKE_LOG_DEBUG("WAIT FOR FENCE0 {} {}", i, (void *)(fences[i - 1][currentFrame]))
                vkWaitForFences(globalLogicalDevice, 1, &(fences[i - 1][currentFrame]), VK_TRUE, UINT64_MAX);
                VKE_LOG_DEBUG("WAIT FOR FENCE1 {} {}", i, (void *)(fences[i - 1][currentFrame]))
            }

        if (!cpuSemaphores[currentFrame]->load())
        {
            VKE_LOG_DEBUG("WAIT FOR CPU0 {}", (void *)(cpuSemaphores[currentFrame].get()))
            cpuSemaphores[currentFrame]->wait(false);
            VKE_LOG_DEBUG("WAIT FOR CPU1 {}", (void *)(cpuSemaphores[currentFrame].get()))
        }
    }

    void FrameGraph::updateTransientMemory(const uint32_t currentFrame)
    {
        if (transientMemoryUpdateCnt == 0)
            return;
        --transientMemoryUpdateCnt;

        RenderEnvironment *env = RenderEnvironment::GetInstance();

        for (auto &kv : resources)
        {
            if (kv.second->isTransient)
                recreateTransientResource(kv.first, *kv.second, currentFrame);
        }

        transientMemoryManager.Realloc(RENDER_TASK, currentFrame, transientMemorySimulator);
        transientMemoryManager.Realloc(COMPUTE_TASK, currentFrame, transientMemorySimulator);

        for (auto &kv : resources)
        {
            if (!kv.second->isTransient)
                continue;
            vke_ds::id32_t resourceID = kv.first;
            RenderResource *resource = kv.second.get();

            auto allocationIt = transientMemoryAllocationMap.find(resourceID);
            if (allocationIt == transientMemoryAllocationMap.end())
                continue;

            const TransientMemoryAllocation &transientAllocation = allocationIt->second;
            VmaAllocation poolAllocation = transientMemoryManager.GetPoolVmaAllocation(transientAllocation.type, currentFrame, transientAllocation.idx);
            VkDeviceSize bindOffset = static_cast<VkDeviceSize>(transientAllocation.offset);

            if (resource->resourceType == IMAGE_RESOURCE)
            {
                ImageResource *imageResource = (ImageResource *)resource;
                VKE_VK_CHECK(vmaBindImageMemory2(env->vmaAllocator, poolAllocation, bindOffset, imageResource->images[currentFrame], nullptr),
                             "failed to bind transient image memory!")
                VKE_LOG_INFO("IMAGE BIND MEMORY {} {}", resource->name, (void *)(imageResource->images[currentFrame]))
            }
            else
            {
                BufferResource *bufferResource = (BufferResource *)resource;
                VKE_VK_CHECK(vmaBindBufferMemory2(env->vmaAllocator, poolAllocation, bindOffset, bufferResource->buffers[currentFrame], nullptr),
                             "failed to bind transient buffer memory!")
            }
        }

        for (auto &kv : transientReadyCallbacks)
            kv.second(*this, currentFrame);
    }

    void FrameGraph::PrepareForExecute(const uint32_t currentFrame)
    {
        if (needRecompile)
            Compile();
        updateTransientMemory(currentFrame);
    }

    void FrameGraph::Execute(const uint32_t currentFrame, const uint32_t imageIndex)
    {
        semaphorePools[currentFrame]->Reset();
        for (auto taskID : orderedTasks)
            taskNodes[taskID]->ResetCurrentSemaphore();

#ifdef VKE_ENABLE_FRAME_GRAPH_PROFILING
        FrameProfile &frameProfile = frameProfiles[currentFrame];
        frameProfile.frameNumber = profileFrameNumber++;
        frameProfile.pending = false;
        frameProfile.tasks.clear();
        frameProfile.tasks.reserve(orderedTasks.size());
        uint32_t profileTaskCounts[TASK_TYPE_CNT - 1]{};
#endif

        VkCommandBuffer commandBuffers[TASK_TYPE_CNT] = {nullptr, nullptr, nullptr, nullptr};
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        for (int i = 0; i < TASK_TYPE_CNT; ++i)
            if (RenderEnvironment::HasQueue(QueueType(i)) && (submitCntEstimates[i] > 0))
            {
                CommandPool *commandPool = commandPools[i][currentFrame].get();
                commandPool->Reset();
                commandPool->PreAllocateCommandBuffer(submitCntEstimates[i]);
                commandBuffers[i] = commandPool->AllocateAndBegin(&beginInfo);
#ifdef VKE_ENABLE_FRAME_GRAPH_PROFILING
                if (i != CPU_TASK && profileQueryPools[i][currentFrame] != VK_NULL_HANDLE)
                    vkCmdResetQueryPool(commandBuffers[i], profileQueryPools[i][currentFrame],
                                        0, PROFILE_QUERY_POOL_SIZE);
#endif
            }
        VKE_LOG_DEBUG("---------------------EXE-------------------")
        uint32_t actualSubmitCnts[TASK_TYPE_CNT] = {0, 0, 0, 0};
        VkPipelineStageFlags2 waitDstStageMask = 0;
        std::vector<VkBufferMemoryBarrier2> bufferMemoryBarriers;
        std::vector<VkImageMemoryBarrier2> imageMemoryBarriers;
        std::map<VkSemaphore, uint64_t> waitSemaphoreMap;
        std::vector<VkSemaphoreSubmitInfo> waitSemaphoreInfos;
        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.pNext = nullptr;

        VkCommandBufferSubmitInfo commandBufferSubmitInfo{};
        commandBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandBufferSubmitInfo.pNext = nullptr;
        commandBufferSubmitInfo.deviceMask = 0;

        VkSemaphoreSubmitInfo signalSemaphoreInfo{};
        signalSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalSemaphoreInfo.pNext = nullptr;
        signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalSemaphoreInfo.deviceIndex = 0;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.pNext = nullptr; //&tlsSubmitInfo;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandBufferSubmitInfo;
        submitInfo.signalSemaphoreInfoCount = 1;
        submitInfo.pSignalSemaphoreInfos = &signalSemaphoreInfo;

        for (auto taskID : orderedTasks)
        {
            TaskNode &taskNode = *taskNodes[taskID];
            TaskType actualTaskType = taskNode.actualTaskType;
            bool needQueueSubmit = taskNode.isFinalTask;
            VkCommandBuffer commandBuffer = commandBuffers[actualTaskType];
#ifdef VKE_ENABLE_FRAME_GRAPH_PROFILING
            const bool profileTask = actualTaskType != CPU_TASK;
            const bool hasGpuTimestamps = profileTask &&
                                          profileQueryPools[actualTaskType][currentFrame] != VK_NULL_HANDLE;
            uint32_t profileQueryIndex = 0;
            std::chrono::steady_clock::time_point cpuRecordBegin;
            if (profileTask)
            {
                cpuRecordBegin = std::chrono::steady_clock::now();
                if (hasGpuTimestamps)
                {
                    VKE_FATAL_IF(profileTaskCounts[actualTaskType] >= PROFILE_MAX_TASKS_PER_QUEUE,
                                 "too many FrameGraph tasks for profile query pool")
                    profileQueryIndex = profileTaskCounts[actualTaskType]++ * PROFILE_QUERY_COUNT_PER_TASK;
                }
            }
#endif
            VKE_LOG_DEBUG("-----------TASK <{}> TYPE {} ACTUAL {}", taskNode.name, (uint32_t)taskNode.taskType, (uint32_t)actualTaskType)
            bufferMemoryBarriers.clear();
            imageMemoryBarriers.clear();
            for (auto &ref : taskNode.resourceRefs)
                syncResources(currentFrame, imageIndex, ref.second, taskNode, needQueueSubmit,
                              bufferMemoryBarriers, imageMemoryBarriers,
                              waitSemaphoreMap, waitDstStageMask);
            dependencyInfo.bufferMemoryBarrierCount = bufferMemoryBarriers.size();
            dependencyInfo.pBufferMemoryBarriers = bufferMemoryBarriers.data();
            dependencyInfo.imageMemoryBarrierCount = imageMemoryBarriers.size();
            dependencyInfo.pImageMemoryBarriers = imageMemoryBarriers.data();
#ifdef VKE_ENABLE_FRAME_GRAPH_PROFILING
            const bool hasPreBarrier = dependencyInfo.bufferMemoryBarrierCount > 0 || dependencyInfo.imageMemoryBarrierCount > 0;
            if (hasGpuTimestamps)
                vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                     profileQueryPools[actualTaskType][currentFrame], profileQueryIndex);
#endif
            if (dependencyInfo.bufferMemoryBarrierCount > 0 || dependencyInfo.imageMemoryBarrierCount > 0)
                vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

#ifdef VKE_ENABLE_FRAME_GRAPH_PROFILING
            if (hasGpuTimestamps)
                vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                     profileQueryPools[actualTaskType][currentFrame], profileQueryIndex + 1);
#endif
            taskNode.executeCallback(taskNode, *this, commandBuffer, currentFrame, imageIndex);
#ifdef VKE_ENABLE_FRAME_GRAPH_PROFILING
            if (hasGpuTimestamps)
                vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                     profileQueryPools[actualTaskType][currentFrame], profileQueryIndex + 2);
#endif

            bufferMemoryBarriers.clear();
            imageMemoryBarriers.clear();
            for (auto &ref : taskNode.resourceRefs)
                endResourcesUse(currentFrame, imageIndex, ref.second, taskNode, needQueueSubmit,
                                bufferMemoryBarriers, imageMemoryBarriers);

            if (needQueueSubmit)
            {
                ensureTaskSemaphore(currentFrame, taskNode);
                bool nextAllocated = false;
                for (auto &ref : taskNode.resourceRefs)
                {
                    if (ref.second.storeOp == VK_ATTACHMENT_STORE_OP_STORE)
                    {
                        vke_ds::id32_t resourceNodeID = ref.second.outResourceNodeID;
                        ResourceNode &resourceNode = *resourceNodes[resourceNodeID];
                        for (auto dstTaskID : resourceNode.dstTaskIDs)
                        {
                            TaskNode &dstTaskNode = *taskNodes[dstTaskID];
                            if (!dstTaskNode.valid || dstTaskNode.HasCurrentSemaphore())
                                continue;

                            nextAllocated = assignNextTaskSemaphore(currentFrame, taskNode, dstTaskNode);
                            if (nextAllocated)
                                break;
                        }
                    }
                    if (nextAllocated)
                        break;
                }
            }

            dependencyInfo.bufferMemoryBarrierCount = bufferMemoryBarriers.size();
            dependencyInfo.pBufferMemoryBarriers = bufferMemoryBarriers.data();
            dependencyInfo.imageMemoryBarrierCount = imageMemoryBarriers.size();
            dependencyInfo.pImageMemoryBarriers = imageMemoryBarriers.data();
#ifdef VKE_ENABLE_FRAME_GRAPH_PROFILING
            const bool hasPostBarrier = dependencyInfo.bufferMemoryBarrierCount > 0 || dependencyInfo.imageMemoryBarrierCount > 0;
#endif
            if (dependencyInfo.bufferMemoryBarrierCount > 0 || dependencyInfo.imageMemoryBarrierCount > 0)
                vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
#ifdef VKE_ENABLE_FRAME_GRAPH_PROFILING
            if (profileTask)
            {
                if (hasGpuTimestamps)
                    vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                         profileQueryPools[actualTaskType][currentFrame], profileQueryIndex + 3);
                const auto cpuRecordEnd = std::chrono::steady_clock::now();
                frameProfile.tasks.push_back(TaskProfile{
                    .taskID = taskNode.taskID,
                    .name = taskNode.name,
                    .actualTaskType = actualTaskType,
                    .queryIndex = profileQueryIndex,
                    .cpuRecordMs = std::chrono::duration<double, std::milli>(cpuRecordEnd - cpuRecordBegin).count(),
                    .hasGpuTimestamps = hasGpuTimestamps,
                    .hasPreBarrier = hasPreBarrier,
                    .hasPostBarrier = hasPostBarrier});
            }
#endif

            if (needQueueSubmit)
            {
                if (actualTaskType != CPU_TASK)
                    vkEndCommandBuffer(commandBuffer);

                commandBufferSubmitInfo.commandBuffer = commandBuffer;

                for (auto [k, v] : waitSemaphoreMap)
                {
                    waitSemaphoreInfos.push_back(VkSemaphoreSubmitInfo{
                        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                        .semaphore = k,
                        .value = v,
                        .stageMask = waitDstStageMask,
                        .deviceIndex = 0});
                    VKE_LOG_DEBUG("------TASK <{}> WAIT FOR {} {}", taskNode.name, (void *)(k), v)
                }

                submitInfo.waitSemaphoreInfoCount = waitSemaphoreMap.size();
                submitInfo.pWaitSemaphoreInfos = waitSemaphoreInfos.data();

                uint64_t signalSemaphoreValue = taskNode.currentSemaphoreValue;
                signalSemaphoreInfo.semaphore = taskNode.currentSemaphore;
                signalSemaphoreInfo.value = signalSemaphoreValue;

                CommandQueue *queue = RenderEnvironment::GetQueue(QueueType(actualTaskType));

                VKE_LOG_DEBUG("------TASK <{}> SUBMIT TO QUEUE {} COMMAND BUFFER {} SIGNAL {} {}", taskNode.name, (void *)queue, (void *)commandBuffer, (void *)(signalSemaphoreInfo.semaphore), signalSemaphoreValue)
                ++actualSubmitCnts[actualTaskType];

                if (taskNode.isFinalTask)
                {
                    VkFence fence = nullptr;
                    if (actualTaskType == CPU_TASK)
                    {
                        VKE_LOG_DEBUG("RESET CPU FENCE {}", (void *)(cpuSemaphores[currentFrame].get()))
                        cpuSemaphores[currentFrame]->store(false);
                        fence = (VkFence)(cpuSemaphores[currentFrame].get());
                    }
                    else if (actualTaskType != RENDER_TASK)
                    {
                        fence = fences[actualTaskType - 1][currentFrame];
                        VKE_LOG_DEBUG("RESET FENCE {} {}", (uint32_t)actualTaskType, (void *)fence)
                        vkResetFences(globalLogicalDevice, 1, &fence);
                        VKE_LOG_DEBUG("RESET FENCE {} {}", (uint32_t)actualTaskType, (void *)fence)
                    }
                    queue->Submit(1, &submitInfo, fence);
                }
                else
                {
                    queue->Submit(1, &submitInfo, VK_NULL_HANDLE);
                    commandBuffers[actualTaskType] = commandPools[actualTaskType][currentFrame]->AllocateAndBegin(&beginInfo);
                }

                waitDstStageMask = 0;
                waitSemaphoreMap.clear();
                waitSemaphoreInfos.clear();
                taskNode.lastSemaphore = taskNode.currentSemaphore;
                taskNode.lastSemaphoreValue = taskNode.currentSemaphoreValue;
            }
        }
        for (int i = 0; i < TASK_TYPE_CNT; i++)
            submitCntEstimates[i] = actualSubmitCnts[i];

        for (auto &[id, resource] : resources)
            if (resource->isTransient || resource->framesInFlight)
                resource->ResetPrev();
#ifdef VKE_ENABLE_FRAME_GRAPH_PROFILING
        frameProfile.pending = !frameProfile.tasks.empty();
#endif
    }
}
