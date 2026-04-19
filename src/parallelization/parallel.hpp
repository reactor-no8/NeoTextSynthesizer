#pragma once

#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>
#include <nlohmann/json.hpp>

struct GenerationResult
{
    std::vector<unsigned char> encodedData;
    std::string relPath;
    nlohmann::json json_data;
    bool isError = false;
};

template <typename T>
class BlockingQueue
{
public:
    explicit BlockingQueue(size_t capacity)
        : capacity_(capacity), closed_(false)
    {
    }

    bool push(T &&item)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        notFull_.wait(lock, [this] { return queue_.size() < capacity_ || closed_; });
        if (closed_)
        {
            return false;
        }
        queue_.push_back(std::move(item));
        notEmpty_.notify_one();
        return true;
    }

    std::optional<T> pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        notEmpty_.wait(lock, [this] { return !queue_.empty() || closed_; });
        if (queue_.empty())
        {
            return std::nullopt;
        }
        T item = std::move(queue_.front());
        queue_.pop_front();
        notFull_.notify_one();
        return item;
    }

    void close()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        closed_ = true;
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

private:
    size_t capacity_;
    std::deque<T> queue_;
    bool closed_;
    std::mutex mutex_;
    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
};

template <typename Executor>
concept GenerationTaskExecutor = requires(Executor executor) {
    { executor.executeTask() } -> std::same_as<GenerationResult>;
};

template <GenerationTaskExecutor Executor>
void parallelGenerate(int numToGen,
                      Executor &executor,
                      BlockingQueue<GenerationResult> &ioQueue,
                      std::atomic<int64_t> &globalIndex,
                      std::atomic<int64_t> &globalErrorCounter,
                      const std::vector<int64_t> &hierLevels);

class JsonlWriter;
void ioWorker(BlockingQueue<GenerationResult> &ioQueue,
              JsonlWriter &writer,
              const std::string &outDir);

#include "parallelization/parallel.tpp"