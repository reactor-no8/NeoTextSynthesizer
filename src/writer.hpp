#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <indicators/progress_bar.hpp>

// A thread-safe batched JSONL writer.
// Each record contains: image filename, text content, and image width.
// Records are buffered and flushed either when the batch is full or explicitly.
class JsonlWriter
{
public:
    // Opens (or creates) the output file at the given path.
    // batchSize controls how many records to buffer before auto-flushing.
    explicit JsonlWriter(const std::string &path, int total, size_t batchSize = 128);
    ~JsonlWriter();

    // Add one record to the buffer; flushes automatically if batch is full.
    void write(const std::string &image, const std::string &text, int length);

    // Force flush all buffered records to disk.
    void flush();

private:
    std::ofstream file_;
    std::vector<std::string> buffer_;
    size_t batchSize_;
    std::mutex mutex_;
    int total_;
    int written_ = 0;
    indicators::ProgressBar bar_;

    // Serialize one record to a JSON line string.
    static std::string makeJsonLine(const std::string &image,
                                    const std::string &text,
                                    int length);

    // Write all buffered lines to disk, caller must hold mutex_.
    void flushLocked();
};
