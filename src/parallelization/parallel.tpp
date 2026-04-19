#pragma once

#include <atomic>

#include "utils/utils.hpp"

template <GenerationTaskExecutor Executor>
void parallelGenerate(int numToGen,
                      Executor &executor,
                      BlockingQueue<GenerationResult> &ioQueue,
                      std::atomic<int64_t> &globalIndex,
                      std::atomic<int64_t> &globalErrorCounter,
                      const std::vector<int64_t> &hierLevels)
{
    for (int i = 0; i < numToGen; ++i)
    {
        GenerationResult result;
        try
        {
            result = executor.executeTask();
        }
        catch (...)
        {
            globalErrorCounter.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        if (result.isError || result.json_string.empty())
        {
            globalErrorCounter.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        const int64_t idx = globalIndex.fetch_add(1, std::memory_order_relaxed);
        result.relPath = indexToHierarchicalPath(idx, hierLevels);
        ioQueue.push(std::move(result));
    }
}