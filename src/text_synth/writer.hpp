#pragma once

#include <cstdint>
#include <fstream>
#include <indicators/progress_bar.hpp>
#include <mutex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class JsonlWriter
{
public:
    explicit JsonlWriter(const std::string &path, int total, size_t batchSize = 128);
    ~JsonlWriter();

    void write(const nlohmann::json &jsonLine);
    void flush();

private:
    std::ofstream file_;
    std::vector<nlohmann::json> buffer_;
    size_t batchSize_;
    std::mutex mutex_;
    int total_;
    int written_ = 0;
    indicators::ProgressBar bar_;

    void flushLocked();
};