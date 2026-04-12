#pragma once

#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <atomic>
#include <vector>
#include <string>

using json = nlohmann::json;

#include "paged_bitmap.hpp"

// Forward declarations
class TextSampler;
class Renderer;
class JsonlWriter;
class GlyphCache;
struct SharedFontMeta;
struct SharedBgResources;

struct WriteTask
{
    std::vector<uchar> encodedData;
    std::string relPath;
    std::string text;
    int width;
};

// Bounded blocking queue
class BlockingQueue
{
public:
    explicit BlockingQueue(size_t capacity);

    // Push an item, blocking when the queue is at capacity.
    // Returns false if the queue has already been closed.
    bool push(WriteTask &&item);

    // Pop an item, blocking when the queue is empty.
    // Returns nullopt when the queue is closed and drained.
    std::optional<WriteTask> pop();

    // Signal that no more items will be pushed.
    // Unblocks any waiting pop() calls once the queue drains.
    void close();

private:
    size_t capacity_;
    std::deque<WriteTask> queue_;
    bool closed_;
    mutable std::mutex mutex_;
    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
};

// Render worker function.
// Each worker creates its own thread-local Renderer from the shared resources.
void workerTask(int numToGen,
                TextSampler &sampler,
                const std::vector<SharedFontMeta> &defaultMeta,
                std::shared_ptr<MultiFontBitmap<256>> multiFontBitmap,
                GlyphCache &glyphCache,
                SharedBgResources &bgRes,
                const json &config,
                BlockingQueue &ioQueue,
                std::atomic<int64_t> &globalIndex,
                const std::vector<int64_t> &hierLevels);

// I/O worker function
void ioWorker(BlockingQueue &ioQueue,
              JsonlWriter &writer,
              const std::string &outDir);

// Utility functions
int randInt(int min, int max);
double randDouble(double min, double max);
std::string indexToHierarchicalPath(int64_t idx, const std::vector<int64_t> &hierLevels);