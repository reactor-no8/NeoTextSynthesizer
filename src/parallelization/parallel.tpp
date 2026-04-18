#pragma once

#include <atomic>

#include "utils/utils.hpp"

template <GenerationTaskExecutor Executor>
void parallelGenerate(int numToGen,
                      Executor &executor,
                      BlockingQueue<GenerationResult> &ioQueue,
                      std::atomic<int64_t> &globalIndex,
                      const std::vector<int64_t> &hierLevels)
{
    for (int i = 0; i < numToGen; ++i)
    {
        GenerationResult result = executor.executeTask();
        const int64_t idx = globalIndex.fetch_add(1, std::memory_order_relaxed);
        result.relPath = indexToHierarchicalPath(idx, hierLevels);
        ioQueue.push(std::move(result));
    }
}