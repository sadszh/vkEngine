#ifndef QUEUE_H
#define QUEUE_H

#include <render/render_common.hpp>
#include <optional>
#include <vector>
#include <set>
#include <map>
#include <queue>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <iostream>

namespace vke_render
{
    enum QueueType
    {
        GRAPHICS_QUEUE = 0,
        COMPUTE_QUEUE,
        TRANSFER_QUEUE,
        CPU_QUEUE,
        QUEUE_TYPE_CNT
    };

    struct QueueFamilyIndices
    {
        std::optional<uint32_t> graphicsAndComputeFamily;
        std::optional<uint32_t> presentFamily;
        std::optional<uint32_t> computeOnlyFamily;
        std::optional<uint32_t> transferOnlyFamily;
        std::vector<uint32_t> uniqueQueueFamilies;

        void clear()
        {
            graphicsAndComputeFamily.reset();
            presentFamily.reset();
            computeOnlyFamily.reset();
            transferOnlyFamily.reset();
            uniqueQueueFamilies.clear();
        }

        bool isComplete()
        {
            return graphicsAndComputeFamily.has_value() && presentFamily.has_value();
        }

        void getUniqueQueueFamilies()
        {
            std::set<uint32_t> queueFamilySet = {graphicsAndComputeFamily.value(),
                                                 presentFamily.value()};
            if (computeOnlyFamily.has_value())
                queueFamilySet.insert(computeOnlyFamily.value());
            if (transferOnlyFamily.has_value())
                queueFamilySet.insert(transferOnlyFamily.value());
            for (auto &val : queueFamilySet)
                uniqueQueueFamilies.push_back(val);
        }
    };

    class CommandQueue
    {
    public:
        CommandQueue() {}
        virtual ~CommandQueue() = default;
        virtual void Submit(uint32_t submitCount, const VkSubmitInfo2 *pSubmits, VkFence fence) = 0;
    };

    class GPUCommandQueue : public CommandQueue
    {
    public:
        VkQueue queue;
        std::mutex mutex;
        GPUCommandQueue() {}
        GPUCommandQueue(VkQueue queue) : queue(queue) {}

        void WaitIdle()
        {
            std::lock_guard lock(mutex);
            VKE_VK_CHECK(vkQueueWaitIdle(queue), "Cannot wait for queue")
        }

        virtual void Submit(uint32_t submitCount, const VkSubmitInfo2 *pSubmits, VkFence fence) override
        {
            std::lock_guard lock(mutex);
            VKE_VK_CHECK(vkQueueSubmit2(queue, submitCount, pSubmits, fence), "vkQueueSubmit2 FAIL")
        }
    };

    class CPUCommandQueue : public CommandQueue
    {
    public:
        const uint64_t CPU_QUEUE_WAIT_TIMEOUT = 50000; // ns
        CPUCommandQueue() : running(false), idle(true) {}
        CPUCommandQueue(VkDevice device) : running(false), idle(true), device(device) {}

        virtual void Submit(uint32_t submitCount, const VkSubmitInfo2 *pSubmits, VkFence fence) override;

        void Start()
        {
            bool r = false;
            if (running.compare_exchange_strong(r, true))
                worker = std::thread(&CPUCommandQueue::process, this);
        }

        void Stop()
        {
            bool r = true;
            if (running.compare_exchange_strong(r, false))
            {
                queueCV.notify_one();
                if (worker.joinable())
                    worker.join();
            }
        }

        void WaitIdle()
        {
            idle.wait(false);
        }

    private:
        struct SubmitInfo
        {
            std::vector<VkSemaphoreSubmitInfo> signalSemaphoreInfos;
            std::function<void()> *callback;

            SubmitInfo() {}
            SubmitInfo(uint32_t semaphoreCnt, std::function<void()> *callback)
                : signalSemaphoreInfos(semaphoreCnt), callback(callback) {}

            SubmitInfo &operator=(SubmitInfo &&ano)
            {
                if (this != &ano)
                {
                    signalSemaphoreInfos = std::move(ano.signalSemaphoreInfos);
                    callback = ano.callback;
                    ano.callback = nullptr;
                }
                return *this;
            }

            SubmitInfo(SubmitInfo &&ano)
                : signalSemaphoreInfos(std::move(ano.signalSemaphoreInfos)),
                  callback(ano.callback)
            {
                ano.callback = nullptr;
            }
        };

        struct WaitInfo
        {
            uint64_t value;
            std::shared_ptr<uint32_t> counter;
            std::function<void()> callback;

            WaitInfo() {}
            WaitInfo(uint64_t value) : value(value) {}
            WaitInfo(uint64_t value, std::shared_ptr<uint32_t> &counter, std::function<void()> callback = nullptr)
                : value(value), counter(counter), callback(callback) {}

            bool operator<(const WaitInfo &ano) const
            {
                return value < ano.value;
            }
        };

        VkDevice device;
        std::atomic<bool> running;
        std::atomic<bool> idle;
        std::mutex queueMutex;
        std::condition_variable queueCV;
        std::thread worker;

        std::map<VkSemaphore, std::multiset<WaitInfo>> waitInfoMap;
        std::vector<SubmitInfo> submitInfos;

        void process();
    };
}

#endif