#include <iostream>
#include <atomic>
#include "textsampler.hpp"
#include "renderer.hpp"
#include "glyph_cache.hpp"
#include "writer.hpp"
#include "utils.hpp"
#include "worker.hpp"
#include "yaml_utils.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

int main(int argc, char **argv)
{
    int total = 0;
    int numWorkers = std::thread::hardware_concurrency();
    if (numWorkers <= 0)
        numWorkers = 1;
    std::string configPath = "config.json";

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--total" && i + 1 < argc)
            total = std::stoi(argv[++i]);
        else if (arg == "--workers" && i + 1 < argc)
            numWorkers = std::stoi(argv[++i]);
        else if (arg == "--config" && i + 1 < argc)
            configPath = argv[++i];
    }
    if (total <= 0)
    {
        std::cerr << "Usage: generator --total <N> [--workers <M>] [--config <path>]\n";
        return 1;
    }

    json config;
    try
    {
        config = yaml_utils::loadConfigFile(configPath);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Cannot load config file '" << configPath << "': " << e.what() << "\n";
        return 1;
    }

    const json &imgCfg = config["image_processor"];
    const json &genCfg = config["generate"];
    int fontSize = imgCfg.value("font_size", 55);

    // Build shared font metadata
    FT_Library tempFtLib;
    if (FT_Init_FreeType(&tempFtLib))
    {
        std::cerr << "Failed to init FreeType\n";
        return 1;
    }

    std::string defaultFontDir = genCfg["default_font_dir"].get<std::string>();
    std::string fallbackFontDir = genCfg["fallback_font_dir"].get<std::string>();

    auto defaultMeta = Renderer::buildSharedFontMeta(defaultFontDir, fontSize, tempFtLib, 0);
    auto fallbackMeta = Renderer::buildSharedFontMeta(fallbackFontDir, fontSize, tempFtLib, defaultMeta.size());

    // System font fallback if no fallback fonts found
    if (fallbackMeta.empty())
    {
        std::string systemFont;
#if defined(_WIN32) || defined(_WIN64)
        systemFont = "C:\\Windows\\Fonts\\Arial.ttf";
#elif defined(__APPLE__)
        systemFont = "/System/Library/Fonts/Courier New.ttf";
#else
        systemFont = "/usr/share/fonts/truetype/DejaVuSans.ttf";
#endif
        if (fs::exists(systemFont))
        {
            std::cout << "No fallback fonts found. Using system default: " << systemFont << "\n";
            FT_Face face;
            if (!FT_New_Face(tempFtLib, systemFont.c_str(), 0, &face))
            {
                FT_Set_Pixel_Sizes(face, 0, fontSize);
                SharedFontMeta meta;
                meta.path = systemFont;
                meta.index = defaultMeta.size();
                meta.ascender = (int)(face->size->metrics.ascender >> 6);
                meta.descender = (int)(face->size->metrics.descender >> 6);
                FT_UInt idx;
                FT_ULong cp = FT_Get_First_Char(face, &idx);
                while (idx != 0)
                {
                    meta.cmap.insert((uint32_t)cp);
                    cp = FT_Get_Next_Char(face, cp, &idx);
                }
                fallbackMeta.push_back(std::move(meta));
                FT_Done_Face(face);
            }
        }
        else
        {
            std::cerr << "No fallback fonts found and system default not available at: " << systemFont << "\n";
            std::cerr << "Please add font files to the fallback_font_dir directory.\n";
            FT_Done_FreeType(tempFtLib);
            return 1;
        }
    }

    FT_Done_FreeType(tempFtLib);

    std::cout << "Loaded " << defaultMeta.size() << " default fonts, "
              << fallbackMeta.size() << " fallback fonts.\n";

    // Build shared glyph cache
    GlyphCache glyphCache;

    // Build shared background resources
    SharedBgResources bgRes;
    bgRes.bgFiles = Renderer::listBgFiles(genCfg["bg_dir"].get<std::string>());

    // Text samplers
    auto samplers = TextSampler::createShards(config["text_sampler"], numWorkers);

    std::string outDir = genCfg["out_dir"].get<std::string>();
    std::string outPq = genCfg["out_parquet"].get<std::string>();
    int batchSize = genCfg.value("batchsize", 10000);

    std::vector<int64_t> hierLevels;
    if (genCfg.contains("hierarchical_structure"))
        for (auto &v : genCfg["hierarchical_structure"])
            hierLevels.push_back(v.get<int64_t>());

    fs::create_directories(outDir);

    JsonlWriter writer(outPq, total, batchSize);

    const size_t queueCapacity = static_cast<size_t>(numWorkers) * 8;
    BlockingQueue ioQueue(queueCapacity);

    // Start the dedicated I/O thread before the render workers.
    std::thread ioThread(ioWorker, std::ref(ioQueue), std::ref(writer), std::cref(outDir));

    // Launch render workers
    std::atomic<int64_t> globalIndex{0};
    std::vector<std::thread> workers;
    int perWorker = total / numWorkers;
    int extra = total % numWorkers;

    for (int i = 0; i < numWorkers; i++)
    {
        int count = perWorker + (i < extra ? 1 : 0);
        if (count > 0)
            workers.emplace_back(workerTask, count,
                                 std::ref(samplers[i]),
                                 std::cref(imgCfg),
                                 std::cref(defaultMeta),
                                 std::cref(fallbackMeta),
                                 std::ref(glyphCache),
                                 std::ref(bgRes),
                                 std::ref(ioQueue),
                                 std::cref(config),
                                 std::ref(globalIndex),
                                 std::cref(hierLevels));
    }

    // Wait for all render workers to finish, then signal the I/O thread.
    for (auto &t : workers)
        t.join();

    ioQueue.close();
    ioThread.join();

    writer.flush();
    return 0;
}