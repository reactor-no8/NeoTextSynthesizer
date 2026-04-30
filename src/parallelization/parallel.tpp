#pragma once

#include <atomic>

#include "utils/utils.hpp"

template <GenerationTaskExecutor Executor>
void parallelGenerate(int numToGen,
                      bool retryOnError,
                      Executor &executor,
                      BlockingQueue<GenerationResult> &ioQueue,
                      std::atomic<int64_t> &globalIndex,
                      std::atomic<int64_t> &globalErrorCounter,
                      const std::vector<int64_t> &hierLevels)
{
    auto tryOnce = [&]() -> std::optional<GenerationResult> {
        try {
            auto res = executor.executeTask();
            if (!res.isError && !res.json_data.empty()) return res;
        } catch (...) { }
        
        globalErrorCounter.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    };

    for (int i = 0; i < numToGen; ) {
        if (auto result = tryOnce()) {
            const int64_t idx = globalIndex.fetch_add(1, std::memory_order_relaxed);
            result->relPath = indexToHierarchicalPath(idx, hierLevels);
            ioQueue.push(std::move(*result));
            ++i;
        } else if (!retryOnError) {
            ++i;
        }
    }
}