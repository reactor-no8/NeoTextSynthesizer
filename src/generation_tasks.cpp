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
#include "utils/yaml_utils.hpp"

namespace fs = std::filesystem;

SingleLineTextGenerator::SingleLineTextGenerator(const std::string &configStr)
    : synthesizer_(yaml_utils::yamlStringToJson(configStr))
{
    config_ = yaml_utils::yamlStringToJson(configStr);
    
    const json &textCfg = config_["text_sampler"];
    const json &bgCfg = config_["bg_sampler"];
    const int fontSize = textCfg.value("font_size", 55);
    
    // Build font metadata from paths
    std::vector<std::string> fontList;
    if (textCfg.contains("font_list"))
    {
        for (const auto &item : textCfg["font_list"])
        {
            fontList.push_back(item.get<std::string>());
        }
    }
    
    fontMetas_ = FontSelector::buildSharedFontMeta(fontList, fontSize);
    if (fontMetas_.empty())
    {
        throw std::runtime_error("No valid fonts found.");
    }

    globalLibrary_ = std::make_unique<FontLibrary>();
    fontSelector_ = std::make_unique<FontSelector>(fontMetas_, *globalLibrary_);
    glyphCache_ = std::make_unique<GlyphCache>();
    bgResources_ = std::make_unique<BackgroundResources>();
    
    renderer_ = std::make_unique<SingleLineRenderer>(config_, *glyphCache_, *fontSelector_);
    
    std::vector<std::string> bgList;
    if (bgCfg.contains("bg_list"))
    {
        for (const auto &item : bgCfg["bg_list"])
        {
            bgList.push_back(item.get<std::string>());
        }
    }
    
    for (const auto &bgPath : bgList)
    {
        bgResources_->addToList(bgPath);
    }
}

std::pair<int64_t, int64_t> SingleLineTextGenerator::generate(int total, int workers, bool showProgress)
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

    std::vector<FontLibrary> libraries(numWorkers);
    std::vector<FontSelector> fontSelectors = fontSelector_->createThreadSelectors(libraries);

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
    std::atomic<int64_t> globalErrorCounter{0};
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

        renderThreads.emplace_back(
            [this, i, count, &samplers, &fontSelectors, &ioQueue, &globalIndex, &globalErrorCounter, &hierLevels]() {
                SingleLineRenderer threadRenderer(this->config_, *(this->glyphCache_), fontSelectors[static_cast<size_t>(i)]);
                SingleLineGenerationTask::ExecutorResources resources{
                    .synthesizer = &(this->synthesizer_),
                    .sampler = &samplers[static_cast<size_t>(i)],
                    .renderer = &threadRenderer,
                    .fontSelector = fontSelectors[static_cast<size_t>(i)],
                    .bgResources = *(this->bgResources_)
                };
                SingleLineGenerationTask task(resources);
                parallelGenerate<SingleLineGenerationTask>(count, task, ioQueue, globalIndex, globalErrorCounter, hierLevels);
            });
    }

    for (auto &t : renderThreads)
    {
        t.join();
    }

    ioQueue.close();
    ioThread.join();
    writer.flush();

    const int64_t totalErrors = globalErrorCounter.load(std::memory_order_relaxed);
    const int64_t totalGenerated = globalIndex.load(std::memory_order_relaxed);
    return {totalGenerated, totalErrors};
}

void SingleLineTextGenerator::generateInstanceFile(const std::string &text, const std::string &savePath) const
{
    synthesizer_.generateInstanceFile(text, savePath, *renderer_, *fontSelector_, *bgResources_);
}

SingleLineTextSynthesizer::ImageResult SingleLineTextGenerator::generateInstanceExplicit(const std::string &text) const
{
    return synthesizer_.generateInstanceExplicit(text, *renderer_, *fontSelector_, *bgResources_);
}

std::string SingleLineTextGenerator::getConfigJson() const
{
    return synthesizer_.getConfig().dump();
}