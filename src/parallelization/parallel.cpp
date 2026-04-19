#include "parallelization/parallel.hpp"

#include <filesystem>
#include <fstream>
#include <unordered_set>

#include "text_synth/writer.hpp"

namespace fs = std::filesystem;

void ioWorker(BlockingQueue<GenerationResult> &ioQueue,
              JsonlWriter &writer,
              const std::string &outDir)
{
    std::unordered_set<std::string> createdDirs;

    while (true)
    {
        auto maybeTask = ioQueue.pop();
        if (!maybeTask)
        {
            break;
        }

        GenerationResult &task = *maybeTask;

        // Skip error/empty results — no image file, no JSONL entry.
        if (task.isError || task.json_string.empty())
        {
            continue;
        }

        fs::path fullPath = fs::path(outDir) / task.relPath;
        const std::string parentDir = fullPath.parent_path().string();

        if (!createdDirs.contains(parentDir))
        {
            fs::create_directories(fullPath.parent_path());
            createdDirs.insert(parentDir);
        }

        std::ofstream file(fullPath.string(), std::ios::binary);
        if (file)
        {
            file.write(reinterpret_cast<const char *>(task.encodedData.data()),
                       static_cast<std::streamsize>(task.encodedData.size()));
        }

        writer.write(task.json_string);
    }
}