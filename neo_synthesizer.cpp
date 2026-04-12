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

void NeoTextSynthesizer::initResources()
{
    const json &textCfg = config_["text_sampler"];
    const json &bgCfg = config_["bg_sampler"];
    const json &genCfg = config_["generate"];
    int fontSize = textCfg.value("font_size", 55);

    FT_Library tempFtLib;
    if (FT_Init_FreeType(&tempFtLib))
        throw std::runtime_error("Failed to init FreeType");

    std::vector<std::string> fontList;
    if (textCfg.contains("font_list")) {
        for (const auto& item : textCfg["font_list"]) {
            fontList.push_back(item.get<std::string>());
        }
    }

    for (const auto& fontPath : fontList) {
        if (fs::is_directory(fontPath)) {
            auto metas = Renderer::buildSharedFontMeta(fontPath, fontSize, tempFtLib, defaultMeta_.size());
            defaultMeta_.insert(defaultMeta_.end(), std::make_move_iterator(metas.begin()), std::make_move_iterator(metas.end()));
        } else if (fs::is_regular_file(fontPath)) {
            // TODO: Handle single font file
        }
    }

    if (defaultMeta_.empty())
    {
        throw std::runtime_error("No valid fonts found and default font could not be loaded.");
    }

    std::string sampleStrategy = textCfg.value("sample_strategy", "font-first");
    if (sampleStrategy == "sample-first" || sampleStrategy == "auto-fallback") {
        multiFontBitmap_ = std::make_shared<MultiFontBitmap<256>>();
        for (size_t i = 0; i < defaultMeta_.size(); ++i) {
            auto cps = defaultMeta_[i].bitmap->get_all_codepoints();
            for (uint32_t cp : cps) {
                multiFontBitmap_->set(cp, i);
            }
        }
    }

    FT_Done_FreeType(tempFtLib);

    std::cout << "\033[32mLoaded " << defaultMeta_.size() << " fonts.\033[0m\n";

    // Background resources
    std::vector<std::string> bgList;
    if (bgCfg.contains("bg_list")) {
        for (const auto& item : bgCfg["bg_list"]) {
            bgList.push_back(item.get<std::string>());
        }
    }

    for (const auto& bgPath : bgList) {
        if (fs::is_directory(bgPath)) {
            auto files = Renderer::listBgFiles(bgPath);
            bgRes_.bgFiles.insert(bgRes_.bgFiles.end(), files.begin(), files.end());
        } else if (fs::is_regular_file(bgPath)) {
            bgRes_.bgFiles.push_back(bgPath);
        }
    }

    if (bgRes_.bgFiles.empty() && bgCfg.value("bg_image_prob", 0.0) > 0.0) {
        std::cout << "\033[33mWarning: bg_image_prob > 0 but no background images found.\033[0m\n";
    }
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
    if (!config_.contains("text_sampler"))
        throw std::runtime_error("Configuration missing required section: 'text_sampler'");
    if (!config_.contains("bg_sampler"))
        throw std::runtime_error("Configuration missing required section: 'bg_sampler'");
    if (!config_.contains("post_process"))
        throw std::runtime_error("Configuration missing required section: 'post_process'");
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

    const json &postCfg = config_["post_process"];
    const json &bgCfg = config_["bg_sampler"];
    const json &genCfg = config_["generate"];

    // Text samplers
    auto samplers = TextSampler::createShards(config_["random_config"], numWorkers);

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
                                       std::cref(defaultMeta_),
                                       multiFontBitmap_,
                                       std::ref(glyphCache_),
                                       std::ref(bgRes_),
                                       std::cref(config_),
                                       std::ref(ioQueue),
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
                                   std::shared_ptr<MultiFontBitmap<256>> multiFontBitmap,
                                   GlyphCache &glyphCache,
                                   SharedBgResources &bgRes,
                                   const std::string &text)
{
    const json &postCfg = config["post_process"];
    const json &genCfg = config["generate"];
    const json &textCfg = config["text_sampler"];
    const json &bgCfg = config["bg_sampler"];
    
    Renderer renderer(postCfg, bgCfg, defaultMeta, multiFontBitmap, glyphCache, bgRes);

    int outputHeight = genCfg["output_height"].get<int>();
    int fontSize = textCfg.value("font_size", 55);
    
    const json &pasteCfg = postCfg["text_paste"];
    double scaleMin = pasteCfg.contains("scale_range") ? pasteCfg["scale_range"][0].get<double>() : 1.0;
    double scaleMax = pasteCfg.contains("scale_range") ? pasteCfg["scale_range"][1].get<double>() : 1.0;
    bool recomputeWidth = pasteCfg.value("recompute_width", false);
    int mMin = pasteCfg["margin_range"][0].get<int>();
    int mMax = pasteCfg["margin_range"][1].get<int>();
    double offsetProb = pasteCfg.value("offset_prob", 0.0);
    int hOffMin = pasteCfg.contains("h_offset_range") ? pasteCfg["h_offset_range"][0].get<int>() : 0;
    int hOffMax = pasteCfg.contains("h_offset_range") ? pasteCfg["h_offset_range"][1].get<int>() : 0;
    int vOffMin = pasteCfg.contains("v_offset_range") ? pasteCfg["v_offset_range"][0].get<int>() : 0;
    int vOffMax = pasteCfg.contains("v_offset_range") ? pasteCfg["v_offset_range"][1].get<int>() : 0;

    std::string sampleStrategy = textCfg.value("sample_strategy", "font-first");
    std::string mutableText = text;

    BgInfo bgInfo = renderer.getRandomBgPredict();
    cv::Vec3b approxColor = renderer.getBgApproxColor(bgInfo);
    cv::Mat textImg = renderer.renderTightText(mutableText, approxColor, sampleStrategy);
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
    cv::Mat img = generateSingleImage(config_, defaultMeta_, multiFontBitmap_,
                                      glyphCache_, bgRes_, text);
    // Ensure parent directory exists
    fs::path p(savePath);
    if (p.has_parent_path())
        fs::create_directories(p.parent_path());
    cv::imwrite(savePath, img);
}

NeoTextSynthesizer::ImageResult NeoTextSynthesizer::generateInstanceExplicit(const std::string &text)
{
    cv::Mat img = generateSingleImage(config_, defaultMeta_, multiFontBitmap_,
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