#include "generation_tasks.hpp"

#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <vector>

#include "parallelization/parallel.hpp"
#include "parallelization/tasks/singletext_task.hpp"
#include "text_synth/textsampler.hpp"
#include "text_synth/writer.hpp"

namespace fs = std::filesystem;

SingleLineTextGenerator::SingleLineTextGenerator(const std::string &configStr)
    : synthesizer_(configStr)
{
}

void SingleLineTextGenerator::generate(int total, int workers, bool showProgress)
{
    if (total <= 0)
    {
        throw std::runtime_error("total must be > 0");
    }

    int numWorkers = workers;
    if (numWorkers <= 0)
    {
        numWorkers = static_cast<int>(std::thread::hardware_concurrency());
        if (numWorkers <= 0)
        {
            numWorkers = 1;
        }
    }

    const auto &config = synthesizer_.getConfig();
    const auto &genCfg = config["generate"];

    auto samplers = TextSampler::createShards(config["random_config"], numWorkers);

    const std::string outDir = genCfg["out_dir"].get<std::string>();
    const std::string outJsonl = genCfg["out_jsonl"].get<std::string>();
    const int batchSize = genCfg.value("batchsize", 10000);

    std::vector<int64_t> hierLevels;
    if (genCfg.contains("hierarchical_structure"))
    {
        for (const auto &v : genCfg["hierarchical_structure"])
        {
            hierLevels.push_back(v.get<int64_t>());
        }
    }

    fs::create_directories(outDir);

    JsonlWriter writer(outJsonl, showProgress ? total : 0, batchSize);
    BlockingQueue<GenerationResult> ioQueue(static_cast<size_t>(numWorkers) * 8);

    std::thread ioThread(ioWorker, std::ref(ioQueue), std::ref(writer), std::cref(outDir));

    std::atomic<int64_t> globalIndex{0};
    std::vector<std::thread> renderThreads;

    const int perWorker = total / numWorkers;
    const int extra = total % numWorkers;

    for (int i = 0; i < numWorkers; ++i)
    {
        const int count = perWorker + (i < extra ? 1 : 0);
        if (count <= 0)
        {
            continue;
        }

        SingleLineGenerationTask::ExecutorResources resources{
            .synthesizer = &synthesizer_,
            .sampler = &samplers[static_cast<size_t>(i)],
        };

        renderThreads.emplace_back(
            [count, resources, &ioQueue, &globalIndex, &hierLevels]() mutable {
                SingleLineGenerationTask task(resources);
                parallelGenerate(count, task, ioQueue, globalIndex, hierLevels);
            });
    }

    for (auto &t : renderThreads)
    {
        t.join();
    }

    ioQueue.close();
    ioThread.join();
    writer.flush();
}

void SingleLineTextGenerator::generateInstanceFile(const std::string &text, const std::string &savePath) const
{
    synthesizer_.generateInstanceFile(text, savePath);
}

SingleLineTextSynthesizer::ImageResult SingleLineTextGenerator::generateInstanceExplicit(const std::string &text) const
{
    return synthesizer_.generateInstanceExplicit(text);
}

std::string SingleLineTextGenerator::getConfigJson() const
{
    return synthesizer_.getConfig().dump();
}