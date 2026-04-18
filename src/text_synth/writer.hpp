#pragma once

#include <cstdint>
#include <fstream>
#include <indicators/progress_bar.hpp>
#include <mutex>
#include <string>
#include <vector>

class JsonlWriter
{
public:
    explicit JsonlWriter(const std::string &path, int total, size_t batchSize = 128);
    ~JsonlWriter();

    void write(const std::string &jsonLine);
    void flush();

private:
    std::ofstream file_;
    std::vector<std::string> buffer_;
    size_t batchSize_;
    std::mutex mutex_;
    int total_;
    int written_ = 0;
    indicators::ProgressBar bar_;

    void flushLocked();
};