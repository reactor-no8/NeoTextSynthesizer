#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Converts simplified Chinese characters to traditional
class CharConverter
{
public:
    explicit CharConverter(const std::string &mappingFile);

    // Convert a single UTF-8 character; returns original if not in mapping.
    std::string convert(const std::string &ch) const;

    // Convert each character in a UTF-8 string individually.
    std::string convertString(const std::string &input) const;

private:
    // key: single UTF-8 simplified char, value: list of traditional variants
    std::map<std::string, std::vector<std::string>> mapping_;
};

class TextSampler
{
public:
    explicit TextSampler(const json &config);
    TextSampler(const TextSampler &) = delete;
    TextSampler &operator=(const TextSampler &) = delete;
    TextSampler(TextSampler &&) noexcept = default;
    TextSampler &operator=(TextSampler &&) noexcept = default;

    // Generate a UTF-8 string of exactly targetLen Unicode codepoints.
    // Thread-safe to call on independent instances; no locking required.
    std::string generateString(int targetLen);

    // Create `numShards` independent TextSampler instances that together cover
    // all sequential corpus files.  Each instance holds an exclusive byte-range
    // slice of every sequential file so that they can run fully in parallel
    // without any shared state.
    static std::vector<TextSampler> createShards(const json &config, int numShards);

private:
    json config_;

    struct SequentialState
    {
        std::string  path;
        std::ifstream stream;
        std::string  pendingBytes;    // incomplete UTF-8 sequences across chunk boundaries

        // Shard boundaries (byte offsets into the file).
        // shardSize == -1  means "entire file" (no shard limit).
        int64_t shardOffset   = 0;
        int64_t shardSize     = -1;
        int64_t bytesConsumed = 0;    // bytes read from this shard so far

        SequentialState() = default;
        SequentialState(const SequentialState &) = delete;
        SequentialState &operator=(const SequentialState &) = delete;
        SequentialState(SequentialState &&) noexcept = default;
        SequentialState &operator=(SequentialState &&) noexcept = default;
    };

    std::map<std::string, std::vector<std::string>> dataCache_; // key -> lines / chars
    std::map<std::string, SequentialState>          seqStates_; // key -> streaming state
    CharConverter converter_;

    // Load data for a config item (lazy)
    const std::vector<std::string> &getDataLines(const json &item);
    SequentialState &getOrCreateSeqState(const json &item);
    std::string readSequentialUtf8(SequentialState &st, int targetChars);

    // Recursively sample from a config array
    std::string sampleRecursive(const json &items, double roll = -1.0);
};