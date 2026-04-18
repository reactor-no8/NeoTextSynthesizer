#include "text_synth/textsampler.hpp"
#include "utils/utf8_helper.hpp"
#include "utils/utils.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <array>
#include <filesystem>

namespace fs = std::filesystem;


// ---- CharConverter ----

CharConverter::CharConverter(const std::string &mappingFile)
{
    std::ifstream f(mappingFile, std::ios::binary);
    if (!f)
    {
        std::cerr << "Warning: cannot open s2t mapping: " << mappingFile << "\n";
        return;
    }

    std::string line;
    while (std::getline(f, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (line.empty())
            continue;

        size_t openParen = line.find('(');
        size_t closeParen = line.find(')');
        if (openParen == std::string::npos || closeParen == std::string::npos)
            continue;
        if (closeParen < openParen)
            continue;

        std::string prefix = line.substr(0, openParen);
        auto prefixChars = UTF8Helper::Split(prefix);
        if (prefixChars.empty())
            continue;
        std::string simplified = prefixChars.back();

        std::string inner = line.substr(openParen + 1, closeParen - openParen - 1);
        std::vector<std::string> variants;
        {
            std::istringstream ss(inner);
            std::string token;
            while (std::getline(ss, token, ','))
            {
                size_t s = token.find_first_not_of(" \t\r\n");
                size_t e = token.find_last_not_of(" \t\r\n");
                if (s == std::string::npos)
                    continue;
                token = token.substr(s, e - s + 1);
                if (!token.empty())
                    variants.push_back(token);
            }
        }

        std::sort(variants.begin(), variants.end());
        variants.erase(std::unique(variants.begin(), variants.end()), variants.end());

        if (!variants.empty())
            mapping_[simplified] = std::move(variants);
    }
}

std::string CharConverter::convert(const std::string &ch) const
{
    auto it = mapping_.find(ch);
    if (it == mapping_.end())
        return ch;
    const auto &choices = it->second;
    return choices[randInt(0, (int)choices.size() - 1)];
}

std::string CharConverter::convertString(const std::string &input) const
{
    if (mapping_.empty())
        return input;
    auto chars = UTF8Helper::Split(input);
    std::string result;
    result.reserve(input.size());
    for (const auto &ch : chars)
        result += convert(ch);
    return result;
}

// ---- Build cache key ----
static std::string cacheKey(const json &item)
{
    std::string key;
    if (item.contains("from_file"))
        key = item["from_file"].get<std::string>();
    else if (item.contains("from_string"))
        key = "str:" + item["from_string"].get<std::string>();
    if (item.contains("section"))
    {
        auto &sec = item["section"];
        key += "_sec_";
        key += sec[0].is_null() ? "null" : std::to_string(sec[0].get<int>());
        key += "_";
        key += sec[1].is_null() ? "null" : std::to_string(sec[1].get<int>());
    }
    // Shard identity: if shard_offset is present, append it so that each shard
    // gets its own SequentialState entry in seqStates_.
    if (item.contains("shard_offset"))
        key += "_shard_" + std::to_string(item["shard_offset"].get<int64_t>());
    return key;
}

// ---- TextSampler ----

TextSampler::TextSampler(const json &config)
    : config_(config), converter_("s2t.txt")
{
    std::vector<double> probs;
    for (auto &item : config_)
        probs.push_back(item.value("prob", 0.0));
    normalizeProbsVec(probs);
    for (size_t i = 0; i < config_.size(); i++)
        config_[i]["prob"] = probs[i];
}

// static
std::vector<TextSampler> TextSampler::createShards(const json &config, int numShards)
{
    if (numShards <= 0)
        numShards = 1;

    // Build per-shard config copies.  Start with identical copies of `config`.
    std::vector<json> shardConfigs(numShards, config);

    // Walk the top-level items; only "sequential" entries are sharded.
    for (size_t itemIdx = 0; itemIdx < config.size(); ++itemIdx)
    {
        const json &item = config[itemIdx];
        if (item.value("type", "") != "sequential")
            continue;
        if (!item.contains("from_file"))
            continue;

        std::string path = item["from_file"].get<std::string>();

        // Get the file size in bytes.
        std::error_code ec;
        int64_t fileSize = (int64_t)fs::file_size(path, ec);
        if (ec || fileSize <= 0)
        {
            // Cannot determine size; leave this item un-sharded in all copies.
            std::cerr << "Warning: cannot stat " << path << "; skipping shard split.\n";
            continue;
        }

        int64_t shardSize   = fileSize / numShards;
        // Ensure each shard is at least 1 byte so we always make forward progress.
        if (shardSize < 1)
            shardSize = 1;

        for (int s = 0; s < numShards; ++s)
        {
            int64_t offset = (int64_t)s * shardSize;
            // Last shard gets any remainder bytes.
            int64_t size   = (s == numShards - 1) ? (fileSize - offset) : shardSize;

            shardConfigs[s][itemIdx]["shard_offset"] = offset;
            shardConfigs[s][itemIdx]["shard_size"]   = size;
        }
    }

    std::vector<TextSampler> samplers;
    samplers.reserve(numShards);
    for (int s = 0; s < numShards; ++s)
        samplers.emplace_back(shardConfigs[s]);
    return samplers;
}

const std::vector<std::string> &TextSampler::getDataLines(const json &item)
{
    std::string key = cacheKey(item);
    if (dataCache_.count(key))
        return dataCache_[key];

    std::vector<std::string> lines;
    if (item.contains("from_file"))
    {
        std::string path = item["from_file"].get<std::string>();
        std::ifstream f(path, std::ios::binary);
        if (!f) {
            std::cout << "\033[33mWarning: Cannot open file: " << path << ". This item will be ignored.\033[0m\n";
            dataCache_[key] = lines;
            return dataCache_[key];
        }
        std::string line;
        while (std::getline(f, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                line.pop_back();
            if (!line.empty())
                lines.push_back(line);
        }
        if (item.contains("section"))
        {
            auto &sec = item["section"];
            int start = sec[0].is_null() ? 0 : sec[0].get<int>();
            int end = sec[1].is_null() ? (int)lines.size() : sec[1].get<int>();
            start = std::max(0, start);
            end = std::min((int)lines.size(), end);
            if (start <= end)
                lines = std::vector<std::string>(lines.begin() + start, lines.begin() + end);
            else
                lines.clear();
        }
    }
    else if (item.contains("from_string"))
    {
        lines = UTF8Helper::Split(item["from_string"].get<std::string>());
    }
    dataCache_[key] = std::move(lines);
    return dataCache_[key];
}

TextSampler::SequentialState &TextSampler::getOrCreateSeqState(const json &item)
{
    std::string key = cacheKey(item);
    auto it = seqStates_.find(key);
    if (it != seqStates_.end())
        return it->second;

    SequentialState st;
    st.path = item["from_file"].get<std::string>();

    if (item.contains("shard_offset"))
        st.shardOffset = item["shard_offset"].get<int64_t>();
    if (item.contains("shard_size"))
        st.shardSize = item["shard_size"].get<int64_t>();

    st.stream.open(st.path, std::ios::binary);
    if (!st.stream) {
        std::cout << "\033[33mWarning: Cannot open file: " << st.path << ". This item will be ignored.\033[0m\n";
    }

    // Seek to the start of this shard.
    if (st.shardOffset > 0)
        st.stream.seekg(st.shardOffset, std::ios::beg);

    seqStates_[key] = std::move(st);
    return seqStates_[key];
}

std::string TextSampler::readSequentialUtf8(SequentialState &st, int targetChars)
{
    std::string out;
    if (!st.stream.is_open()) return out;
    
    out.reserve((size_t)targetChars * 4);

    // Helper: how many bytes remain in the current shard pass.
    // Returns -1 when there is no shard limit.
    auto shardRemaining = [&]() -> int64_t {
        if (st.shardSize < 0)
            return -1; // unlimited
        return st.shardSize - st.bytesConsumed;
    };

    while (UTF8Helper::Length(out) < targetChars)
    {
        if (st.pendingBytes.empty())
        {
            // How many bytes can we read in this chunk?
            int64_t remaining = shardRemaining();
            if (remaining == 0)
            {
                // Reached end of shard – wrap around to the shard start.
                st.stream.clear();
                st.stream.seekg(st.shardOffset, std::ios::beg);
                st.bytesConsumed = 0;
                st.pendingBytes.clear();
                remaining = shardRemaining(); // == st.shardSize now
                if (remaining == 0)
                    break; // zero-size shard – should not happen
            }

            constexpr std::streamsize kBufSize = 1 << 16;
            std::streamsize toRead = (remaining < 0)
                                         ? kBufSize
                                         : std::min((int64_t)kBufSize, remaining);

            std::array<char, 1 << 16> buf{};
            st.stream.read(buf.data(), toRead);
            std::streamsize got = st.stream.gcount();

            if (got <= 0)
            {
                // Hit EOF of the physical file before the shard end.
                // Wrap to shard start and retry once.
                st.stream.clear();
                st.stream.seekg(st.shardOffset, std::ios::beg);
                st.bytesConsumed = 0;
                st.pendingBytes.clear();

                st.stream.read(buf.data(), toRead);
                got = st.stream.gcount();
                if (got <= 0)
                    break; // file is empty or unreadable
            }

            st.bytesConsumed += (int64_t)got;
            st.pendingBytes.append(buf.data(), (size_t)got);
        }

        // Consume valid UTF-8 codepoints from pendingBytes.
        size_t i = 0;
        while (i < st.pendingBytes.size() && UTF8Helper::Length(out) < targetChars)
        {
            unsigned char lead = (unsigned char)st.pendingBytes[i];

            // Skip continuation bytes
            if (!UTF8Helper::IsValidUtf8Start(lead))
            {
                i += 1;
                continue;
            }

            size_t len = UTF8Helper::CharLenFromLead(lead);

            // Character is incomplete – need more data in the next chunk.
            if (i + len > st.pendingBytes.size())
                break;

            // Check that all continuation bytes are valid.
            bool valid = true;
            for (size_t k = 1; k < len; ++k)
            {
                if ((((unsigned char)st.pendingBytes[i + k]) & 0xC0) != 0x80)
                {
                    valid = false;
                    break;
                }
            }
            if (!valid)
            {
                // Malformed sequence – skip the lead byte and retry.
                i += 1;
                continue;
            }

            if (lead == '\n' || lead == '\r')
            {
                out.push_back(' ');
            }
            else
            {
                out.append(st.pendingBytes, i, len);
            }
            i += len;
        }

        if (i == 0 && !st.pendingBytes.empty())
        {
            // Couldn't make any progress (incomplete char at the end of pending).
            // Discard one byte to avoid an infinite loop.
            st.pendingBytes.erase(0, 1);
        }
        else
        {
            st.pendingBytes.erase(0, i);
        }
    }

    return out;
}

std::string TextSampler::sampleRecursive(const json &items, double roll)
{
    std::vector<double> probs;
    for (auto &it : items)
        probs.push_back(it.value("prob", 0.0));
    normalizeProbsVec(probs);

    if (roll < 0.0)
        roll = randDouble(0.0, 1.0);

    double cumulative = 0.0;
    for (size_t idx = 0; idx < items.size(); idx++)
    {
        cumulative += probs[idx];
        if (roll <= cumulative)
        {
            const json &item = items[idx];

            if (item.contains("sub_items"))
            {
                std::string res = sampleRecursive(item["sub_items"]);
                if (item.contains("traditional_prob") && randDouble(0.0, 1.0) < item["traditional_prob"].get<double>())
                    res = converter_.convertString(res);
                return res;
            }

            if (item.value("type", "") == "sequential")
            {
                SequentialState &st = getOrCreateSeqState(item);
                int length = randInt(5, 20);
                std::string res = readSequentialUtf8(st, length);
                return res;
            }

            const auto &data = getDataLines(item);
            std::string res;
            if (item.contains("len_range"))
            {
                int lmin = item["len_range"][0].get<int>();
                int lmax = item["len_range"][1].get<int>();
                int len = randInt(lmin, lmax);
                for (int i = 0; i < len; i++)
                {
                    if (!data.empty())
                        res += data[randInt(0, (int)data.size() - 1)];
                }
            }
            else
            {
                if (!data.empty())
                    res = data[randInt(0, (int)data.size() - 1)];
            }

            if (item.contains("traditional_prob") && randDouble(0.0, 1.0) < item["traditional_prob"].get<double>())
                res = converter_.convertString(res);
            return res;
        }
    }
    return "";
}

std::string TextSampler::generateString(int targetLen)
{
    std::string current;
    double roll = randDouble(0.0, 1.0);
    while (UTF8Helper::Length(current) < targetLen)
    {
        current += sampleRecursive(config_, roll);
    }
    return UTF8Helper::Truncate(current, targetLen);
}