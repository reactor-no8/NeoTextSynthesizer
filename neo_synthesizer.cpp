#include "neo_synthesizer.hpp"
#include "yaml_utils.hpp"
#include "textsampler.hpp"
#include "writer.hpp"
#include "worker.hpp"
#include "utils.hpp"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

std::string NeoTextSynthesizer::getSystemFontPath()
{
#if defined(_WIN32) || defined(_WIN64)
    return "C:\\Windows\\Fonts\\Arial.ttf";
#elif defined(__APPLE__)
    return "/System/Library/Fonts/Courier New.ttf";
#else
    // Linux
    return "/usr/share/fonts/truetype/DejaVuSans.ttf";
#endif
}

void NeoTextSynthesizer::tryAddSystemFallbackFont(int fontSize, FT_Library ftLib)
{
    std::string systemFont = getSystemFontPath();
    if (!fs::exists(systemFont))
    {
        throw std::runtime_error(
            "No fallback fonts found and system default font not available at: " + systemFont +
            "\nPlease add font files (.ttf/.otf) to the fallback_font_dir directory, "
            "or ensure the system default font is installed.");
    }

    std::cout << "No fallback fonts found in fallback_font_dir. "
              << "Using system default font: " << systemFont << "\n";

    FT_Face face;
    if (FT_New_Face(ftLib, systemFont.c_str(), 0, &face))
    {
        throw std::runtime_error("Failed to load system default font: " + systemFont);
    }
    FT_Set_Pixel_Sizes(face, 0, fontSize);

    SharedFontMeta meta;
    meta.path = systemFont;
    meta.index = defaultMeta_.size() + fallbackMeta_.size();
    meta.ascender = (int)(face->size->metrics.ascender >> 6);
    meta.descender = (int)(face->size->metrics.descender >> 6);

    FT_UInt idx;
    FT_ULong cp = FT_Get_First_Char(face, &idx);
    while (idx != 0)
    {
        meta.cmap.insert((uint32_t)cp);
        cp = FT_Get_Next_Char(face, cp, &idx);
    }
    fallbackMeta_.push_back(std::move(meta));

    FT_Done_Face(face);
}

void NeoTextSynthesizer::initResources()
{
    const json &imgCfg = config_["image_processor"];
    const json &genCfg = config_["generate"];
    int fontSize = imgCfg.value("font_size", 55);

    FT_Library tempFtLib;
    if (FT_Init_FreeType(&tempFtLib))
        throw std::runtime_error("Failed to init FreeType");

    std::string defaultFontDir = genCfg["default_font_dir"].get<std::string>();
    std::string fallbackFontDir = genCfg["fallback_font_dir"].get<std::string>();

    // Create directories if they don't exist
    if (!fs::exists(defaultFontDir))
        fs::create_directories(defaultFontDir);
    if (!fs::exists(fallbackFontDir))
        fs::create_directories(fallbackFontDir);

    defaultMeta_ = Renderer::buildSharedFontMeta(defaultFontDir, fontSize, tempFtLib, 0);
    fallbackMeta_ = Renderer::buildSharedFontMeta(fallbackFontDir, fontSize, tempFtLib, defaultMeta_.size());

    // System font fallback: if no fallback fonts found, try system default
    if (fallbackMeta_.empty())
    {
        tryAddSystemFallbackFont(fontSize, tempFtLib);
    }

    FT_Done_FreeType(tempFtLib);

    std::cout << "Loaded " << defaultMeta_.size() << " default fonts, "
              << fallbackMeta_.size() << " fallback fonts.\n";

    // Background resources
    std::string bgDir = genCfg["bg_dir"].get<std::string>();
    if (!fs::exists(bgDir))
        fs::create_directories(bgDir);
    bgRes_.bgFiles = Renderer::listBgFiles(bgDir);
}

NeoTextSynthesizer::NeoTextSynthesizer(const std::string &configStr)
{
    // Always parse the input string as YAML (YAML is a superset of JSON).
    // If the string is invalid, yaml_utils::yamlStringToJson will throw.
    try
    {
        config_ = yaml_utils::yamlStringToJson(configStr);
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error(
            std::string("Failed to parse configuration string: ") + e.what());
    }

    // Validate that required sections exist
    if (!config_.contains("image_processor"))
        throw std::runtime_error("Configuration missing required section: 'image_processor'");
    if (!config_.contains("generate"))
        throw std::runtime_error("Configuration missing required section: 'generate'");

    initResources();
}

NeoTextSynthesizer::~NeoTextSynthesizer() = default;

void NeoTextSynthesizer::generate(int total, int workers, bool showProgress)
{
    if (total <= 0)
        throw std::runtime_error("total must be > 0");

    int numWorkers = workers;
    if (numWorkers <= 0)
    {
        numWorkers = std::thread::hardware_concurrency();
        if (numWorkers <= 0)
            numWorkers = 1;
    }

    const json &imgCfg = config_["image_processor"];
    const json &genCfg = config_["generate"];

    // Text samplers
    auto samplers = TextSampler::createShards(config_["text_sampler"], numWorkers);

    std::string outDir = genCfg["out_dir"].get<std::string>();
    std::string outPq = genCfg["out_jsonl"].get<std::string>();
    int batchSize = genCfg.value("batchsize", 10000);

    std::vector<int64_t> hierLevels;
    if (genCfg.contains("hierarchical_structure"))
        for (auto &v : genCfg["hierarchical_structure"])
            hierLevels.push_back(v.get<int64_t>());

    fs::create_directories(outDir);

    // Use a very large batch size when progress is disabled to avoid overhead
    JsonlWriter writer(outPq, showProgress ? total : 0, batchSize);

    const size_t queueCapacity = static_cast<size_t>(numWorkers) * 8;
    BlockingQueue ioQueue(queueCapacity);

    // Start I/O thread
    std::thread ioThread(ioWorker, std::ref(ioQueue), std::ref(writer), std::cref(outDir));

    // Launch render workers
    std::atomic<int64_t> globalIndex{0};
    std::vector<std::thread> renderThreads;
    int perWorker = total / numWorkers;
    int extra = total % numWorkers;

    for (int i = 0; i < numWorkers; i++)
    {
        int count = perWorker + (i < extra ? 1 : 0);
        if (count > 0)
            renderThreads.emplace_back(workerTask, count,
                                       std::ref(samplers[i]),
                                       std::cref(imgCfg),
                                       std::cref(defaultMeta_),
                                       std::cref(fallbackMeta_),
                                       std::ref(glyphCache_),
                                       std::ref(bgRes_),
                                       std::ref(ioQueue),
                                       std::cref(config_),
                                       std::ref(globalIndex),
                                       std::cref(hierLevels));
    }

    for (auto &t : renderThreads)
        t.join();

    ioQueue.close();
    ioThread.join();
    writer.flush();
}

static cv::Mat generateSingleImage(const json &config,
                                   const std::vector<SharedFontMeta> &defaultMeta,
                                   const std::vector<SharedFontMeta> &fallbackMeta,
                                   GlyphCache &glyphCache,
                                   SharedBgResources &bgRes,
                                   const std::string &text)
{
    const json &imgCfg = config["image_processor"];
    const json &genCfg = config["generate"];

    Renderer renderer(imgCfg, defaultMeta, fallbackMeta, glyphCache, bgRes);

    int outputHeight = genCfg["output_height"].get<int>();
    int fontSize = imgCfg.value("font_size", 55);
    double scaleMin = imgCfg.contains("scale_range") ? imgCfg["scale_range"][0].get<double>() : 1.0;
    double scaleMax = imgCfg.contains("scale_range") ? imgCfg["scale_range"][1].get<double>() : 1.0;
    bool recomputeWidth = imgCfg.value("recompute_width", false);
    int mMin = imgCfg["margin_range"][0].get<int>();
    int mMax = imgCfg["margin_range"][1].get<int>();
    double offsetProb = imgCfg.value("offset_prob", 0.0);
    int hOffMin = imgCfg.contains("h_offset_range") ? imgCfg["h_offset_range"][0].get<int>() : 0;
    int hOffMax = imgCfg.contains("h_offset_range") ? imgCfg["h_offset_range"][1].get<int>() : 0;
    int vOffMin = imgCfg.contains("v_offset_range") ? imgCfg["v_offset_range"][0].get<int>() : 0;
    int vOffMax = imgCfg.contains("v_offset_range") ? imgCfg["v_offset_range"][1].get<int>() : 0;

    BgInfo bgInfo = renderer.getRandomBgPredict();
    cv::Vec3b approxColor = renderer.getBgApproxColor(bgInfo);
    cv::Mat textImg = renderer.renderTightText(text, approxColor);
    if (textImg.empty())
        throw std::runtime_error("Failed to render text: " + text);

    int origW = textImg.cols;
    int origH = textImg.rows;
    double fontScale = (double)origH / fontSize;

    double scale = randDouble(scaleMin, scaleMax);
    if (std::abs(scale - 1.0) > 1e-4)
    {
        int nw = (int)(origW * scale), nh = (int)(origH * scale);
        cv::resize(textImg, textImg, cv::Size(nw, nh), 0, 0, cv::INTER_CUBIC);
    }

    int scaledW = textImg.cols;
    int scaledH = textImg.rows;
    int baseCw = recomputeWidth ? scaledW : origW;
    int baseCh = origH;

    int marginX = (int)(randInt(mMin, mMax) * fontScale);
    int marginY = (int)(randInt(mMin, mMax) * fontScale);
    int cw = baseCw + marginX + marginY;
    int ch = baseCh;

    int drawX = marginX + (baseCw - scaledW) / 2;
    int drawY = (baseCh - scaledH) / 2;
    drawX += (int)(randInt(-5, 5) * fontScale);
    drawY += (int)(randInt(-3, 3) * fontScale);

    if (randDouble(0, 1) < offsetProb)
    {
        drawX += (int)(randInt(hOffMin, hOffMax) * fontScale);
        drawY += (int)(randInt(vOffMin, vOffMax) * fontScale);
    }

    cw = std::max(10, cw);
    ch = std::max(10, ch);

    auto [finalBg, _unused] = renderer.getBgCropAndColor(bgInfo, cw, ch);
    cv::Mat finalBgOut = finalBg;
    for (int r = 0; r < textImg.rows; r++)
    {
        const cv::Vec4b *srcRow = textImg.ptr<cv::Vec4b>(r);
        int dy = drawY + r;
        if (dy < 0 || dy >= finalBgOut.rows)
            continue;
        cv::Vec3b *dstRow = finalBgOut.ptr<cv::Vec3b>(dy);
        for (int c = 0; c < textImg.cols; c++)
        {
            int dx = drawX + c;
            if (dx < 0 || dx >= finalBgOut.cols)
                continue;
            const cv::Vec4b &src = srcRow[c];
            uint8_t alpha = src[3];
            if (alpha == 0)
                continue;
            cv::Vec3b &dst = dstRow[dx];
            float a = alpha / 255.0f;
            dst[0] = (uint8_t)(src[0] * a + dst[0] * (1 - a));
            dst[1] = (uint8_t)(src[1] * a + dst[1] * (1 - a));
            dst[2] = (uint8_t)(src[2] * a + dst[2] * (1 - a));
        }
    }

    int outW = (int)((double)finalBgOut.cols * outputHeight / finalBgOut.rows);
    cv::Mat finalImg;
    cv::resize(finalBgOut, finalImg, cv::Size(outW, outputHeight), 0, 0, cv::INTER_CUBIC);
    return finalImg;
}

void NeoTextSynthesizer::generateInstanceFile(const std::string &text, const std::string &savePath)
{
    cv::Mat img = generateSingleImage(config_, defaultMeta_, fallbackMeta_,
                                      glyphCache_, bgRes_, text);
    // Ensure parent directory exists
    fs::path p(savePath);
    if (p.has_parent_path())
        fs::create_directories(p.parent_path());
    cv::imwrite(savePath, img);
}

NeoTextSynthesizer::ImageResult NeoTextSynthesizer::generateInstanceExplicit(const std::string &text)
{
    cv::Mat img = generateSingleImage(config_, defaultMeta_, fallbackMeta_,
                                      glyphCache_, bgRes_, text);

    // Convert BGR → RGB
    cv::Mat rgb;
    cv::cvtColor(img, rgb, cv::COLOR_BGR2RGB);

    ImageResult result;
    result.height = rgb.rows;
    result.width = rgb.cols;
    result.data.assign(rgb.data, rgb.data + (size_t)rgb.rows * rgb.cols * 3);
    return result;
}